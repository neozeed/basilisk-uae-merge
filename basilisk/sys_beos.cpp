/*
 *  sys_beos.cpp - BeOS specific stuff
 *
 *  Basilisk (C) 1996-1997 Christian Bauer
 */

#include <AppKit.h>
#include <StorageKit.h>
#include <Path.h>
#include <InterfaceKit.h>
#include <KernelKit.h>
#include <StopWatch.h>

extern "C" {
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
}


// Mac Classic screen dimensions
const int DISPLAY_X = 512;
const int DISPLAY_Y = 342;

// Chunky refresh constants
const int LINES_PER_CHUNK = 8;
const int NUM_CHUNKS = (DISPLAY_Y + LINES_PER_CHUNK - 1) / LINES_PER_CHUNK;
const int BYTES_PER_CHUNK = LINES_PER_CHUNK * DISPLAY_X / 8;
const int LONGS_PER_CHUNK = BYTES_PER_CHUNK / 4;
const int MAX_AGE = 20;

// Bitmap frame is a bit larger because last chunk may have fewer lines
const BRect DisplayFrame = BRect(0, 0, DISPLAY_X-1, DISPLAY_Y-1);
const BRect BitmapFrame = BRect(0, 0, DISPLAY_X-1, NUM_CHUNKS * LINES_PER_CHUNK - 1);

// Messages
const uint32 MSG_FLOPPY = 'flop';
const uint32 MSG_DEBUG = 'dbug';
const uint32 MSG_REDRAW = 'draw';

// Tables for converting 1 Bit -> 8 Bit bitmaps (doubles for 64 bit access)
double ExpTab[0x100];

// Be cursor image
UBYTE MacCursor[68] = {16, 1};

bool mouse_in_view = TRUE;	// Flag: Mouse pointer within bitmap view

int quit_program;


class MacWindow;
class BitmapView;


/*
 *  The BeOS application object
 */

class Basilisk : public BApplication {
public:
	Basilisk();
	virtual void ArgvReceived(int32 argc, char **argv);
	virtual void ReadyToRun(void);
	virtual void AboutRequested(void);
};


/*
 *  The window in which the Mac graphics are displayed, handles I/O
 */

class MacWindow : public BWindow {
public:
	MacWindow(BRect frame);
	virtual bool QuitRequested(void);
	virtual void MessageReceived(BMessage *msg);
	virtual	void DispatchMessage(BMessage *msg, BHandler *handler);

private:
	void redraw(void);
	static long tick_func(void *arg);
	static long emul_func(void *arg);

	BitmapView *main_view;
	BBitmap *the_bitmap;
	double *bits;

	thread_id emul_thread;
	thread_id tick_thread;

	UBYTE *mac_screen_buf;	// Pointer to Mac screen buffer
};


/*
 *  A simple view class for blitting a bitmap on the screen
 */

class BitmapView : public BView {
public:
	BitmapView(BRect frame, BBitmap *bitmap);
	virtual void Draw(BRect update);
	virtual void MouseDown(BPoint where);
	virtual void MouseUp(BPoint where);
	virtual void MouseMoved(BPoint point, uint32 transit, const BMessage *message);

private:
	BBitmap *the_bitmap;
};


/*
 *  Create application object and start it
 */

main()
{
	Basilisk *the_app;
	
	the_app = new Basilisk();
	if (the_app != NULL) {
		the_app->Run();
		delete the_app;
	}
	return 0;
}


/*
 *  Application constructor: Initialize member variables
 */

Basilisk::Basilisk() : BApplication("application/x-be-executable") {}


/*
 *  Parse command line options
 */

void Basilisk::ArgvReceived(int32 argc, char **argv)
{
	parse_cmdline(argc, argv);
}


/*
 *  Arguments processed, create and start emulation
 */

void Basilisk::ReadyToRun(void)
{
	// Find application directory and cwd to it
	app_info the_info;
	GetAppInfo(&the_info);
	BEntry the_file(&the_info.ref);
	BEntry the_dir;
	the_file.GetParent(&the_dir);
	BPath the_path;
	the_dir.GetPath(&the_path);
	chdir(the_path.Path());

	// Initialize ExpTab
	ULONG *p = (ULONG *)ExpTab;
	for (int i=0; i<0x100; i++) {
		*p++ = ((i & 0x80) ? 0 : 0xff000000)
				| ((i & 0x40) ? 0 : 0x00ff0000)
				| ((i & 0x20) ? 0 : 0x0000ff00)
				| ((i & 0x10) ? 0 : 0x000000ff);
		*p++ = ((i & 0x08) ? 0 : 0xff000000)
				| ((i & 0x04) ? 0 : 0x00ff0000)
				| ((i & 0x02) ? 0 : 0x0000ff00)
				| ((i & 0x01) ? 0 : 0x000000ff);
	}

	// Open window
	new MacWindow(DisplayFrame);
}


/*
 *  Display "about" window
 */

void Basilisk::AboutRequested(void)
{
	char str[256];
	sprintf(str, "Basilisk V%d.%d by Christian Bauer\n<cbauer@iphcip1.physik.uni-mainz.de>", BASILISK_VERSION, BASILISK_REVISION);
	BAlert *the_alert = new BAlert("", str, "OK");
	the_alert->Go();
}


/*
 *  MacWindow constructor
 */

MacWindow::MacWindow(BRect frame) : BWindow(frame, "Basilisk", B_TITLED_WINDOW, B_NOT_RESIZABLE)
{
	// Move window to right position
	Lock();
	MoveTo(80, 60);

	// Set up menus
	BMenuBar *bar = new BMenuBar(Bounds(), "menu_bar");
	BMenu *menu = new BMenu("File");
	BMenuItem *item = new BMenuItem("About Basilisk...", new BMessage(B_ABOUT_REQUESTED));
	item->SetTarget(be_app);
	menu->AddItem(item);
	menu->AddItem(new BMenuItem("Mount Floppy Disk", new BMessage(MSG_FLOPPY)));
	menu->AddItem(new BMenuItem("Debugger", new BMessage(MSG_DEBUG)));
	menu->AddItem(new BMenuItem("Quit Basilisk", new BMessage(B_QUIT_REQUESTED), 'Q'));
	bar->AddItem(menu);
	AddChild(bar);
	SetKeyMenuBar(bar);
	int mbar_height = bar->Frame().bottom + 1;

	// Resize window to fit menu bar
	ResizeBy(0, mbar_height);

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
	mac_screen_buf = get_real_address(0x3fa700);

	// Allocate bitmap
	the_bitmap = new BBitmap(BitmapFrame, B_COLOR_8_BIT);
	bits = (double *)the_bitmap->Bits();

	// Create bitmap view
	main_view = new BitmapView(DisplayFrame, the_bitmap);
	AddChild(main_view);
	main_view->MoveBy(0, mbar_height);
	main_view->MakeFocus();

	// Show window
	Unlock();
	Show();

	// Start 60Hz interrupt
	tick_thread = spawn_thread(tick_func, "Basilisk 60Hz Ticks", B_REAL_TIME_PRIORITY, this);
	resume_thread(tick_thread);

	// Start emulator thread
	emul_thread = spawn_thread(emul_func, "Basilisk 68000", B_NORMAL_PRIORITY, this);
	resume_thread(emul_thread);
}


/*
 *  Closing the window quits the program
 */

bool MacWindow::QuitRequested(void)
{
	// Stop emulator
	kill_thread(emul_thread);

	// Stop 60Hz interrupt
	kill_thread(tick_thread);

	// Deinitialize everything
	dump_counts();
	filedisk_exit();
	sony_exit();
	save_pram();
	zfile_exit();

	be_app->PostMessage(B_QUIT_REQUESTED);
	return TRUE;
}


/*
 *  Handles redraw messages, polls keyboard and mouse
 */

void MacWindow::MessageReceived(BMessage *msg)
{
	BMessage *msg2;
	static int frame_counter = 1;
	static int tick_counter = 1;

	switch (msg->what) {
		case MSG_FLOPPY:
			mount_floppy = 1;
			break;

		case MSG_DEBUG:
			activate_debugger();
			break;

		case MSG_REDRAW:

			// Prevent backlog of messages
			MessageQueue()->Lock();
			while ((msg2 = MessageQueue()->FindMessage(MSG_REDRAW, 0)) != NULL)
				MessageQueue()->RemoveMessage(msg2);
			MessageQueue()->Unlock();

			if (++frame_counter > framerate) {

				// Refresh screen unless ScrollLock is down
				if (!(modifiers() & B_SCROLL_LOCK))
					redraw();

				// Has the Mac started?
				if (get_long(0xcfc) == 0x574c5343 && mouse_in_view) {	// 'WLSC': Mac warm start flag

					// Poll keyboard
					key_info keys;
					get_key_info(&keys);
					memcpy(key_states, keys.key_states, sizeof(key_states));

					// Set new cursor image if it was changed
					if (memcmp(MacCursor+4, get_real_address(0x844), 64)) {
						memcpy(MacCursor+4, get_real_address(0x844), 64);	// Cursor image
						MacCursor[2] = get_byte(0x885);	// Hotspot
						MacCursor[3] = get_byte(0x887);
						be_app->SetCursor(MacCursor);
					}
				}

				frame_counter = 1;
			}

			// Trigger Mac VBL/Second interrupt
			TriggerVBL();
			if (++tick_counter > 60)
				TriggerSec();
			break;

		default:
			inherited::MessageReceived(msg);
	}
}


/*
 *  Intercept B_MOUSE_UP
 */

void MacWindow::DispatchMessage(BMessage *msg, BHandler *handler)
{
	switch (msg->what) {
		case B_MOUSE_UP:
			main_view->MouseUp(BPoint(0, 0));
			inherited::DispatchMessage(msg, handler);
			break;

		default:
			inherited::DispatchMessage(msg, handler);
	}
}


/*
 *  Refresh and redraw screen
 */

void MacWindow::redraw(void)
{
	static ULONG chunk_sum[NUM_CHUNKS];
	static ULONG chunk_age[NUM_CHUNKS];
	UBYTE *src = mac_screen_buf;
	double *dest = bits;
	BRect update_rect;

	Lock();
	update_rect.left = 0;
	update_rect.right = DISPLAY_X-1;
	update_rect.top = -LINES_PER_CHUNK;
	for (int ch=0; ch<NUM_CHUNKS; ch++) {
		update_rect.top += LINES_PER_CHUNK;
		if (++chunk_age[ch] < MAX_AGE) {
			ULONG *p = (ULONG *)src;
			ULONG sum = 0;
			for (int i=0; i<LONGS_PER_CHUNK; i++)
				sum += *p++;

			if (sum == chunk_sum[ch]) {
				src += BYTES_PER_CHUNK;
				dest += BYTES_PER_CHUNK;
				continue;
			}
			chunk_sum[ch] = sum;
		}
		chunk_age[ch] = 1;
		for (int i=0; i<BYTES_PER_CHUNK; i++)
			*dest++ = ExpTab[*src++];
		update_rect.bottom = update_rect.top + LINES_PER_CHUNK - 1;
		main_view->DrawBitmapAsync(the_bitmap, update_rect, update_rect);
	}
	Unlock();
}


/*
 *  Main emulation thread
 */

long MacWindow::emul_func(void *arg)
{
	m68k_go(1);
	be_app->PostMessage(B_QUIT_REQUESTED);
	return 0;
}


/*
 *  60Hz interrupt routine
 */

long MacWindow::tick_func(void *arg)
{
	for (;;) {
		((MacWindow *)arg)->PostMessage(MSG_REDRAW);
		snooze(16666);
	}
	return 0;
}


/*
 *  Bitmap view constructor
 */

BitmapView::BitmapView(BRect frame, BBitmap *bitmap) : BView(frame, "bitmap", B_FOLLOW_NONE, B_WILL_DRAW)
{
	the_bitmap = bitmap;
}


/*
 *  Blit the bitmap
 */

void BitmapView::Draw(BRect update)
{
	DrawBitmap(the_bitmap, update, update);
}


/*
 *  Mouse moved
 */

void BitmapView::MouseMoved(BPoint point, uint32 transit, const BMessage *message)
{
	switch (transit) {
		case B_ENTERED_VIEW:
			mouse_in_view = TRUE;
			be_app->SetCursor(MacCursor);
			mousex = point.x;
			mousey = point.y;
			break;
		case B_EXITED_VIEW:
			mouse_in_view = FALSE;
			be_app->SetCursor(B_HAND_CURSOR);
			break;
		case B_INSIDE_VIEW:
			mousex = point.x;
			mousey = point.y;
			break;
	}
}


/*
 *  Mouse button pressed
 */

void BitmapView::MouseDown(BPoint where)
{
	mousebutton = true;
}


/*
 *  Mouse button released
 */

void BitmapView::MouseUp(BPoint where)
{
	mousebutton = false;
}


/*
 *  Mac floppy ejected, inform user
 */

void gui_disk_unmounted(int num)
{
	BAlert *the_alert = new BAlert("", "The floppy disk was 'ejected'.", "OK");
	the_alert->Go();
}


int debuggable(void)
{
    return 1;
}
