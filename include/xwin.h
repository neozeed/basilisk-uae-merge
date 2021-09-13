 /* 
  * UAE - The Un*x Amiga Emulator
  * 
  * Interface to the graphics system (X, SVGAlib)
  * 
  * (c) 1995 Bernd Schmidt
  */

typedef long int xcolnr;

typedef int (*allocfunc_type)(int, int, int, xcolnr *);

extern xcolnr xcolors[4096];

extern int buttonstate[3];
extern int newmousecounters;
extern int lastmx, lastmy;

extern int graphics_init(void);
extern void graphics_leave(void);
extern void handle_events(void);
extern void setup_brkhandler(void);

extern void flush_line(int);
extern void flush_block(int, int);
extern void flush_screen(int, int);

extern int debuggable(void);
extern int needmousehack(void);
extern void togglemouse(void);
extern void LED(int);

extern unsigned long doMask(int p, int bits, int shift);
extern void setup_maxcol(int);
extern void alloc_colors256(int (*)(int, int, int, xcolnr *));
extern void alloc_colors64k(int, int, int, int, int, int);
extern void setup_greydither(int bits, allocfunc_type allocfunc);
extern void setup_greydither_maxcol(int maxcol, allocfunc_type allocfunc);
extern void setup_dither(int bits, allocfunc_type allocfunc);
#ifndef X86_ASSEMBLY
extern void DitherLine(UBYTE *l, UWORD *r4g4b4, int x, int y, WORD len, int bits);
#else
extern void DitherLine(UBYTE *l, UWORD *r4g4b4, int x, int y, UWORD len, int bits) __asm__("DitherLine");
#endif

struct vidbuf_description
{
    char *bufmem; /* Pointer to either the video memory or an area as large which is used as a buffer. */
    int rowbytes; /* Bytes per row in the memory pointed at by bufmem. */
    int pixbytes; /* Bytes per pixel. */
    int maxlinetoscr; /* Number of pixels to draw per line. */
    int maxline; /* Number of lines in the area pointed to by bufmem. */
    int maxblocklines; /* Set to 0 if you want calls to flush_line after each drawn line, or the number of
			* lines that flush_block wants to/can handle (it isn't really useful to use another
			* value than maxline here). */
};

extern struct vidbuf_description gfxvidinfo;
