/*
 *  sys_x.c - X11 specific stuff
 *
 *  Basilisk (C) 1996-1997 Christian Bauer
 */

#include "sysconfig.h"
#include "sysdeps.h"
#include "config.h"
#include "options.h"
#include "memory.h"
#include "readcpu.h"
#include "newcpu.h"
#include "patches.h"
#include "sony.h"
#include "filedisk.h"
#include "via.h"
#include "debug.h"
#include "gui.h"
#include "xwin.h"
#include "zfile.h"
#include "main.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/extensions/XShm.h>

#include <sys/time.h>


// Mac Classic screen dimensions
const int DISPLAY_X = 512;
const int DISPLAY_Y = 342;

// Pointer to Mac screen buffer
static UBYTE *MacScreenBuf;

// Pointer to bitmap data
static UBYTE *the_bits;

// X11 stuff
static Display *display;
static int screen;
static Window rootwin, mainwin;
static GC black_gc;
static XVisualInfo visualInfo;
static Visual *vis;
static XImage *img;
static XShmSegmentInfo shminfo;
static XImage *cursor_image, *cursor_mask_image;
static Pixmap cursor_map, cursor_mask_map;
static Cursor mac_cursor;
static GC cursor_gc, cursor_mask_gc;
static XColor black, white;
static int black_pixel, white_pixel;
static const int eventmask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | EnterWindowMask | LeaveWindowMask;

// Update interrupts
static struct sigaction tick_sa;
static struct itimerval tick_tv;

// Tables for converting 1 Bit -> 8 Bit bitmaps
static ULONG ExpTabHi[0x100], ExpTabLo[0x100];

// Mouse pointer in window?
static int inwindow = 1;

// Cursor image data
static UBYTE the_cursor[64];

int quit_program;


// Prototypes
static void graphics_exit(void);
static void tick_handler();
static int event2keycode(XKeyEvent *ev);
static int kc_decode(KeySym ks);
static int decode_us(KeySym ks);
static int decode_de(KeySym ks);


/*
 *  Main function
 */

int main(int argc, char **argv)
{
	// Print banner
	printf("Basilisk V%d.%d by Christian Bauer\n", BASILISK_VERSION, BASILISK_REVISION);

	// Parse command line options
	parse_cmdline(argc, argv);

	// Initialize everything        
	memory_init();
	load_pram();
	sony_init();
	filedisk_init();
	init_m68k();
	m68k_reset();
	if (use_debugger && debuggable())
		activate_debugger();

	// Get address of Mac screen buffer
	MacScreenBuf = get_real_address(0x3fa700);

	// Open window
	if (!graphics_init())
		return 1;

	// Run emulation
	m68k_go(1);

	// Exit
	graphics_exit();
	dump_counts();
	filedisk_exit();
	sony_exit();
	save_pram();
	zfile_exit();

	return 0;
}


/*
 *  Init graphics, open window
 */

int graphics_init(void)
{
	int i;
	char *display_name = NULL;
	XSetWindowAttributes wattr;
	XSizeHints *hints;

	// Open display
	display = XOpenDisplay(display_name);
	if (display == 0) {
		fprintf(stderr, "Can't connect to X server %s\n", XDisplayName(display_name));
		return 0;
	}

	// Find screen and root window
	screen = XDefaultScreen(display);
	rootwin = XRootWindow(display, screen);

	// Find black and white colors
	XParseColor(display, DefaultColormap(display, screen), "rgb:00/00/00", &black);
	XAllocColor(display, DefaultColormap(display, screen), &black);
	XParseColor(display, DefaultColormap(display, screen), "rgb:ff/ff/ff", &white);
	XAllocColor(display, DefaultColormap(display, screen), &white);
	black_pixel = BlackPixel(display, screen);
	white_pixel = WhitePixel(display, screen);

	// 8 bit visual?
	if (!(XMatchVisualInfo(display, screen, 8, PseudoColor, &visualInfo) ||
	      XMatchVisualInfo(display, screen, 8, GrayScale, &visualInfo))) {
		fprintf(stderr, "Can't obtain 8 bit X visual\n");
		return 0;
	}
	if (visualInfo.depth != 8) {
		fprintf(stderr, "Can't obtain 8 bit X visual\n");
		return 0;
	}
	vis = visualInfo.visual;

	// Create and attach SHM image
	img = XShmCreateImage(display, vis, 8, ZPixmap, 0, &shminfo, DISPLAY_X, DISPLAY_Y);
	shminfo.shmid = shmget(IPC_PRIVATE, DISPLAY_Y * img->bytes_per_line, IPC_CREAT | 0777);
	shminfo.shmaddr = img->data = the_bits = (char *)shmat(shminfo.shmid, 0, 0);
	shminfo.readOnly = False;
	XShmAttach(display, &shminfo);
	XSync(display, 0);
	shmctl(shminfo.shmid, IPC_RMID, 0);

	// Create window
	wattr.event_mask = eventmask;
	wattr.background_pixel = black_pixel;
	wattr.backing_store = Always;
	wattr.backing_planes = 8;
	wattr.border_pixmap = None;
	wattr.border_pixel = black_pixel;

	mainwin = XCreateWindow(display, rootwin, 0, 0, DISPLAY_X, DISPLAY_Y, 0, 8,
		InputOutput, vis, CWEventMask | CWBackPixel | CWBorderPixel |
		CWBackingStore | CWBackingPlanes, &wattr);
	XMapWindow(display, mainwin);
	XStoreName(display, mainwin, "Basilisk");

	// Make window unresizable
	if ((hints = XAllocSizeHints()) != NULL) {
		hints->min_width = DISPLAY_X;
		hints->max_width = DISPLAY_X;
		hints->min_height = DISPLAY_Y;
		hints->max_height = DISPLAY_Y;
		hints->flags = PMinSize | PMaxSize;
		XSetWMNormalHints(display, mainwin, hints);
		XFree((char *)hints);
	}

	// Create GC
	black_gc = XCreateGC(display, mainwin, 0, 0);
	XSetForeground(display, black_gc, black_pixel);

	// Create cursor
	cursor_image = XCreateImage(display, vis, 1, ZPixmap, 0, the_cursor, 16, 16, 16, 2);
	cursor_mask_image = XCreateImage(display, vis, 1, ZPixmap, 0, the_cursor+32, 16, 16, 16, 2);
	cursor_map = XCreatePixmap(display, mainwin, 16, 16, 1);
	cursor_mask_map = XCreatePixmap(display, mainwin, 16, 16, 1);
	cursor_gc = XCreateGC(display, cursor_map, 0, 0);
	cursor_mask_gc = XCreateGC(display, cursor_mask_map, 0, 0);
	mac_cursor = XCreatePixmapCursor(display, cursor_map, cursor_mask_map, &black, &white, 0, 0);

	// Initialize ExpTabs
	for (i=0; i<0x100; i++) {
		ExpTabHi[i] = ((i & 0x80) ? black_pixel << 24 : white_pixel << 24)
			    | ((i & 0x40) ? black_pixel << 16 : white_pixel << 16)
			    | ((i & 0x20) ? black_pixel << 8 : white_pixel << 8)
			    | ((i & 0x10) ? black_pixel : white_pixel);
		ExpTabLo[i] = ((i & 0x08) ? black_pixel << 24 : white_pixel << 24)
			    | ((i & 0x04) ? black_pixel << 16 : white_pixel << 16)
			    | ((i & 0x02) ? black_pixel << 8 : white_pixel << 8)
			    | ((i & 0x01) ? black_pixel : white_pixel);
        }

	// Start 60Hz interrupt
	tick_sa.sa_handler = tick_handler;
	tick_sa.sa_flags = 0;
	sigemptyset(&tick_sa.sa_mask);
	sigaction(SIGALRM, &tick_sa, NULL);
	tick_tv.it_interval.tv_sec = 0;
	tick_tv.it_interval.tv_usec = 16666;
	tick_tv.it_value.tv_sec = 0;
	tick_tv.it_value.tv_usec = 16666;
	setitimer(ITIMER_REAL, &tick_tv, NULL);

	return 1;
}


/*
 *  Exit graphics, close window
 */

static void graphics_exit(void)
{
	XSync(display, 0);
}


/*
 *  Refresh screen, handle events
 */

void tick_handler()
{
	int i;
	static int frame_counter = 1;
	static int tick_counter = 1;

	if (++frame_counter > framerate) {

		// Convert 1 bit -> 8 bit
		UBYTE *p = MacScreenBuf;
		ULONG *q = (ULONG *)the_bits;
		for (i=0; i<DISPLAY_X*DISPLAY_Y/8; i++) {
			*q++ = ExpTabHi[*p];
			*q++ = ExpTabLo[*p++];
		}

		// Refresh display
		XSync(display, 0);
		XShmPutImage(display, mainwin, black_gc, img, 0, 0, 0, 0, DISPLAY_X, DISPLAY_Y, 0);

		// Has the Mac started?
		if (get_long(0xcfc) == 0x574c5343 && inwindow) {	// 'WLSC': Mac warm start flag

			// Set new cursor image if it was changed
			if (memcmp(the_cursor, get_real_address(0x844), 64)) {
				memcpy(the_cursor, get_real_address(0x844), 64);
				memcpy(cursor_image->data, the_cursor, 32);
				memcpy(cursor_mask_image->data, the_cursor+32, 32);
				XFreeCursor(display, mac_cursor);
				XPutImage(display, cursor_map, cursor_gc, cursor_image, 0, 0, 0, 0, 16, 16);
				XPutImage(display, cursor_mask_map, cursor_mask_gc, cursor_mask_image, 0, 0, 0, 0, 16, 16);
				mac_cursor = XCreatePixmapCursor(display, cursor_map, cursor_mask_map, &black, &white, get_byte(0x885), get_byte(0x887));
				XDefineCursor(display, mainwin, mac_cursor);
			}
		}

		// Handle events
		for (;;) {
			XEvent event;
			int keycode;

			if (!XCheckMaskEvent(display, eventmask, &event))
				break;

			switch (event.type) {

				// Keyboard
				case KeyPress:
					if (get_long(0xcfc) == 0x574c5343 && inwindow)
						if ((keycode = event2keycode((XKeyEvent *)&event)) != -1)
							key_states[keycode >> 3] |= 1 << (~keycode & 7);
					break;
				case KeyRelease:
					if (get_long(0xcfc) == 0x574c5343 && inwindow)
						if ((keycode = event2keycode((XKeyEvent *)&event)) != -1)
							key_states[keycode >> 3] &= 0xfe << (~keycode & 7);
					break;

				// Mouse button
				case ButtonPress:
					if (get_long(0xcfc) == 0x574c5343 && inwindow)
						mousebutton = 1;
					break;
				case ButtonRelease:
					if (get_long(0xcfc) == 0x574c5343 && inwindow)
						mousebutton = 0;
					break;

				// Mouse moved
				case EnterNotify:
					inwindow = 1;
					mousex = ((XMotionEvent *)&event)->x;
					mousey = ((XMotionEvent *)&event)->y;
					break;
				case LeaveNotify:
					inwindow = 0;
					break;
				case MotionNotify:
					if (inwindow) {
						mousex = ((XMotionEvent *)&event)->x;
						mousey = ((XMotionEvent *)&event)->y;
					}
					break;
			}
		}

		frame_counter = 1;
	}

	// Trigger Mac VBL/Second interrupt
	TriggerVBL();
	if (++tick_counter > 60)
		TriggerSec();
}


/*
 *  Translate key event to (BeOS) keycode
 */

static int event2keycode(XKeyEvent *ev)
{
	KeySym ks;
	int as;
	int i = 0;

	do {
		ks = XLookupKeysym(ev, i++);
		as = kc_decode(ks);
		if (as == -1)
			as = decode_de(ks);
		if (as != -1)
			return as;
	} while (ks != NoSymbol);

	return -1;
}

static int kc_decode(KeySym ks)
{
	switch (ks) {
		case XK_A: case XK_a: return 0x3c;
		case XK_B: case XK_b: return 0x50;
		case XK_C: case XK_c: return 0x4e;
		case XK_D: case XK_d: return 0x3e;
		case XK_E: case XK_e: return 0x29;
		case XK_F: case XK_f: return 0x3f;
		case XK_G: case XK_g: return 0x40;
		case XK_H: case XK_h: return 0x41;
		case XK_I: case XK_i: return 0x2e;
		case XK_J: case XK_j: return 0x42;
		case XK_K: case XK_k: return 0x43;
		case XK_L: case XK_l: return 0x44;
		case XK_M: case XK_m: return 0x52;
		case XK_N: case XK_n: return 0x51;
		case XK_O: case XK_o: return 0x2f;
		case XK_P: case XK_p: return 0x30;
		case XK_Q: case XK_q: return 0x27;
		case XK_R: case XK_r: return 0x2a;
		case XK_S: case XK_s: return 0x3d;
		case XK_T: case XK_t: return 0x2b;
		case XK_U: case XK_u: return 0x2d;
		case XK_V: case XK_v: return 0x4f;
		case XK_W: case XK_w: return 0x28;
		case XK_X: case XK_x: return 0x4d;
		case XK_Y: case XK_y: return 0x2c;
		case XK_Z: case XK_z: return 0x4c;

		case XK_0: return 0x1b;
		case XK_1: return 0x12;
		case XK_2: return 0x13;
		case XK_3: return 0x14;
		case XK_4: return 0x15;
		case XK_5: return 0x16;
		case XK_6: return 0x17;
		case XK_7: return 0x18;
		case XK_8: return 0x19;
		case XK_9: return 0x1a;

		case XK_space: return 0x5e;
		case XK_grave: return 0x11;
		case XK_backslash: return 0x33;
		case XK_comma: return 0x53;
		case XK_period: return 0x54;

		case XK_Escape: return 0x01;
		case XK_Tab: return 0x26;
		case XK_Return: return 0x47;
		case XK_BackSpace: return 0x1e;
		case XK_Delete: return 0x34;
		case XK_Insert: return 0x1f;
		case XK_Home: case XK_Help: return 0x20;
		case XK_End: return 0x35;
#ifdef __hpux
		case XK_Prior: return 0x21;
		case XK_Next: return 0x36;
#else
		case XK_Page_Up: return 0x21;
		case XK_Page_Down: return 0x36;
#endif

		case XK_Control_L: return 0x5c;
		case XK_Control_R: return 0x60;
		case XK_Shift_L: return 0x4b;
		case XK_Shift_R: return 0x56;
		case XK_Alt_L: return 0x5d;
		case XK_Alt_R: return 0x5f;
		case XK_Caps_Lock: return 0x3b;
		case XK_Num_Lock: return 0x22;

		case XK_Up: return 0x57;
		case XK_Down: return 0x62;
		case XK_Left: return 0x61;
		case XK_Right: return 0x63;

		case XK_F1: return 0x02;
		case XK_F2: return 0x03;
		case XK_F3: return 0x04;
		case XK_F4: return 0x05;
		case XK_F5: return 0x06;
		case XK_F6: return 0x07;
		case XK_F7: return 0x08;
		case XK_F8: return 0x09;
		case XK_F9: return 0x0a;
		case XK_F10: return 0x0b;
		case XK_F11: return 0x0c;
		case XK_F12: return 0x0d;

#if defined(XK_KP_Prior) && defined(XK_KP_Left) && defined(XK_KP_Insert) && defined (XK_KP_End)
		case XK_KP_0: case XK_KP_Insert: return 0x64;
		case XK_KP_1: case XK_KP_End: return 0x58;
		case XK_KP_2: case XK_KP_Down: return 0x59;
		case XK_KP_3: case XK_KP_Next: return 0x5a;
		case XK_KP_4: case XK_KP_Left: return 0x48;
		case XK_KP_5: case XK_KP_Begin: return 0x49;
		case XK_KP_6: case XK_KP_Right: return 0x4a;
		case XK_KP_7: case XK_KP_Home: return 0x37;
		case XK_KP_8: case XK_KP_Up: return 0x38;
		case XK_KP_9: case XK_KP_Prior: return 0x39;
		case XK_KP_Decimal: case XK_KP_Delete: return 0x65;
#else
		case XK_KP_0: return 0x64;
		case XK_KP_1: return 0x58;
		case XK_KP_2: return 0x59;
		case XK_KP_3: return 0x5a;
		case XK_KP_4: return 0x48;
		case XK_KP_5: return 0x49;
		case XK_KP_6: return 0x4a;
		case XK_KP_7: return 0x37;
		case XK_KP_8: return 0x38;
		case XK_KP_9: return 0x39;
		case XK_KP_Decimal: return 0x65;
#endif

		case XK_KP_Add: return 0x3a;
		case XK_KP_Subtract: return 0x25;
		case XK_KP_Multiply: return 0x24;
		case XK_KP_Divide: return 0x23;
		case XK_KP_Enter: return 0x5b;
	}
	return -1;
}

static int decode_us(KeySym ks)
{
	switch(ks) {	// US specific
		case XK_minus: return 0x1c;
		case XK_equal: return 0x1d;
		case XK_bracketleft: return 0x31;
		case XK_bracketright: return 0x32;
		case XK_semicolon: return 0x45;
		case XK_apostrophe: return 0x46;
		case XK_slash: return 0x55;
	}
	return -1;
}

static int decode_de(KeySym ks)
{
	switch(ks) {	// DE specific
		case XK_ssharp: return 0x1c;
		case XK_apostrophe: return 0x1d;
		case XK_Udiaeresis: case XK_udiaeresis: return 0x31;
		case XK_plus: return 0x32;
		case XK_Odiaeresis: case XK_odiaeresis: return 0x45;
		case XK_Adiaeresis: case XK_adiaeresis: return 0x46;
		case XK_minus: return 0x55;
	}
	return -1;
}


/*
 *  Mac floppy ejected, inform user
 */

void gui_disk_unmounted(int num)
{
}


int debuggable(void)
{
	return 1;
}
