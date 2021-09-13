/* 
 * UAE - The Un*x Amiga Emulator
 *
 * BeBox port specific stuff
 * 
 * (c) 1996-1997 Christian Bauer
 * (c) 1996 Patrick Hanevold
 */

extern "C" {
#include "sysconfig.h"
#include "sysdeps.h"
#include "config.h"
#include "options.h"
#include "uae.h"
#include "memory.h"
#include "custom.h"
#include "serial.h"
#include "readcpu.h"
#include "newcpu.h"
#include "disk.h"
#include "debug.h"
#include "xwin.h"
#include "os.h"
#include "events.h"
#include "keyboard.h"
#include "keybuf.h"
#include "gui.h"
#include "zfile.h"
#include "autoconf.h"
#include "osemu.h"
#include "compiler.h"
}

class UAEWindow;
class BitmapView;
class Emulator;


/*
 *  The BeOS application object
 */

class UAE : public BApplication {
public:
	UAE();
	virtual void ArgvReceived(int argc, char **argv);
	virtual void ReadyToRun(void);
	virtual bool QuitRequested(void);
	virtual void AboutRequested(void);
	virtual void RefsReceived(BMessage *msg);

	int GraphicsInit(void);
	void GraphicsLeave(void);

private:
	static long thread_func(void *obj);

	BBitmap *the_bitmap;
	UAEWindow *main_window;
	thread_id the_thread;
};


/*
 *  The window in which the Amiga graphics are displayed, handles I/O
 */

class UAEWindow : public BWindow {
public:
	UAEWindow(BRect frame, BBitmap *bitmap);
	virtual bool QuitRequested(void);
	virtual void MessageReceived(BMessage *msg);

private:
	void request_floppy(char *title, int drive_num);

	BitmapView *main_view;
};


/*
 *  A simple view class for blitting a bitmap on the screen
 */

class BitmapView : public BView {
public:
	BitmapView(BRect frame, BBitmap *bitmap);
	virtual void Draw(BRect update);
	virtual void KeyDown(ulong);
	virtual void MouseMoved(BPoint point, ulong transit, BMessage *message);
	void Draw(BRect from, BRect to);

private:
	BBitmap *the_bitmap;
};


/*
 *	LED
 */

class LEDView : public BView {
public:
	LEDView(BRect frame, rgb_color active, rgb_color idle);
	virtual void Draw(BRect update);
	void SetState(bool new_state);

private:
	BRect bounds;
	rgb_color active_color;
	rgb_color idle_color;
	bool state;
};


/*
 *  Global variables
 */

// Keyboard and mouse
int buttonstate[3];
int newmousecounters;
int lastmx, lastmy;
int quit_program;

// Color map and bitmap
xcolnr xcolors[4096];
struct vidbuf_description gfxvidinfo;
