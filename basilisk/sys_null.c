/*
 *  sys_x.c - X11 specific stuff
 *
 *  Basilisk (C) 1996-1997 Christian Bauer
 */

#include "sysconfig.h"
#include "sysdeps.h"
#include "..\config.h"
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

#include <SDL/SDL.h>
#include <SDL/SDL_events.h>

#undef main

#include <signal.h>
#include <time.h>

//////////////

#ifdef __cplusplus
static RETSIGTYPE sigbrkhandler(...)
#else
static RETSIGTYPE sigbrkhandler(int foo)
#endif
{
    activate_debugger();

#if !defined(__unix) || defined(__NeXT__)
    signal(SIGINT, sigbrkhandler);
#endif
}

void setup_brkhandler(void)
{
#if defined(__unix) && !defined(__NeXT__)
    struct sigaction sa;
    sa.sa_handler = sigbrkhandler;
    sa.sa_flags = 0;
#ifdef SA_RESTART
    sa.sa_flags = SA_RESTART;
#endif
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
#else
    signal(SIGINT, sigbrkhandler);
#endif    
}
///////////////


// Mac Classic screen dimensions
const int DISPLAY_X = 512;
const int DISPLAY_Y = 342;

SDL_Surface *screen;
SDL_Event ev;
static int bytes_per_pixel = 1;
static int black_pixel, white_pixel;


// Pointer to Mac screen buffer
static UBYTE *MacScreenBuf;

// Pointer to bitmap data
static UBYTE *the_bits;


// Update interrupts
//static struct sigaction tick_sa;
//static struct itimerval tick_tv;

// Tables for converting 1 Bit -> 8 Bit bitmaps
static ULONG ExpTabHi[0x100], ExpTabLo[0x100];

// Mouse pointer in window?
static int inwindow = 1;

// Cursor image data
static UBYTE the_cursor[64];

int quit_program;


// Prototypes
void graphics_exit(void);
//static void tick_handler();



static int init_colors(void)
{
SDL_Color colors[256];
int i;
for(i=0;i<256;i++){
  colors[i].r=i;
  colors[i].g=i;
  colors[i].b=i;
}
SDL_SetColors(screen, colors, 0, 256);
printf("init_colors\n");
}






/*
 *  Main function
 */

int main(int argc, char **argv)
{
	// Print banner
	printf("Basilisk V%d.%d by Christian Bauer\n", BASILISK_VERSION, BASILISK_REVISION);
	printf("sys_null.c\n");
	// Parse command line options
	parse_cmdline(argc, argv);

	// Initialize everything        
	memory_init();
	load_pram();
	sony_init();
	filedisk_init();
	init_m68k();
	setup_brkhandler();
	m68k_reset();
	if (use_debugger && debuggable())
		activate_debugger();



	// Open window
	if (!graphics_init())
		return 1;

	// Run emulation
	printf("m68k_go(1)\n");
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
	int flags;
	int delay;
	int my_timer_id;
	void * my_callback_param;

	char *display_name = NULL;


	/* Set the video mode */
        if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO)<0)
		{printf("There was an issue with SDL trying to initalize video.\n");
                exit(0);}
        flags=SDL_SWSURFACE;//(SDL_SWSURFACE|SDL_HWPALETTE);
        if (!(screen = SDL_SetVideoMode(DISPLAY_X ,DISPLAY_Y , 8, flags)))
        printf("VID: Couldn't set video mode: %s\n", SDL_GetError());
        SDL_WM_SetCaption("Basilisk 0.6.8 (SDL build)","UAE 0.6.8");
	the_bits=screen->pixels;
	// Get address of Mac screen buffer

	//Macintosh Classic
	MacScreenBuf = get_real_address(0x3fa700);


#if 0
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
#else
#endif

	black_pixel = 0;
	white_pixel = 255;

	// Initialize ExpTabs
	for (i=0; i<0x100; i++) {
//big endian
#if 0
		ExpTabHi[i] = ((i & 0x80) ? black_pixel << 24 : white_pixel << 24)
			    | ((i & 0x40) ? black_pixel << 16 : white_pixel << 16)
			    | ((i & 0x20) ? black_pixel << 8 : white_pixel << 8)
			    | ((i & 0x10) ? black_pixel : white_pixel);

		ExpTabLo[i] = ((i & 0x08) ? black_pixel << 24 : white_pixel << 24)
			    | ((i & 0x04) ? black_pixel << 16 : white_pixel << 16)
			    | ((i & 0x02) ? black_pixel << 8 : white_pixel << 8)
			    | ((i & 0x01) ? black_pixel : white_pixel);
#else
//little endian
		ExpTabHi[i] = ((i & 0x10) ? black_pixel << 24 : white_pixel << 24)
			    | ((i & 0x20) ? black_pixel << 16 : white_pixel << 16)
			    | ((i & 0x40) ? black_pixel << 8 : white_pixel << 8)
			    | ((i & 0x80) ? black_pixel : white_pixel);

		ExpTabLo[i] = ((i & 0x01) ? black_pixel << 24 : white_pixel << 24)
			    | ((i & 0x02) ? black_pixel << 16 : white_pixel << 16)
			    | ((i & 0x04) ? black_pixel << 8 : white_pixel << 8)
			    | ((i & 0x08) ? black_pixel : white_pixel);
#endif
        }

	init_colors();

	return 1;
}


/*
 *  Exit graphics, close window
 */

void graphics_exit(void)
{
printf("graphics_exit\n");
	SDL_Quit();
	exit(0);
}


/*
 *  Refresh screen, handle events
 */

basilisk_tick_handler(void)
{
	int i;
	static int frame_counter = 1;
	static int tick_counter = 1;

SDL_PollEvent(&ev);
	switch(ev.type) {
	case SDL_MOUSEBUTTONDOWN:
		mousebutton = 1;
		break;
	case SDL_MOUSEBUTTONUP:
		mousebutton = 0;
		break;

	case SDL_MOUSEMOTION:
		mousex = ev.motion.x;
		mousey = ev.motion.y;
		break;

	default:
		break;
	}

	if (++frame_counter > framerate) {
		// Convert 1 bit -> 8 bit
		UBYTE *p = MacScreenBuf;
		ULONG *q = (ULONG *)the_bits;
		for (i=0; i<DISPLAY_X*DISPLAY_Y/8; i++) {
			*q++ = ExpTabHi[*p];
			*q++ = ExpTabLo[*p++];
		}


		// Has the Mac started?
		if (get_long(0xcfc) == 0x574c5343 && inwindow) {	// 'WLSC': Mac warm start flag
			// Set new cursor image if it was changed

		}


		frame_counter = 1;
	}

	// Trigger Mac VBL/Second interrupt
	TriggerVBL();
		SDL_LockSurface(screen);
        	SDL_UpdateRect(screen,0,0,0,0);
		SDL_UnlockSurface(screen);

	if (++tick_counter > 60){
		TriggerSec();
	}
}


/*
 *  Translate key event to (BeOS) keycode
 */



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
