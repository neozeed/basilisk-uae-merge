 /* 
  * UAE - The Un*x Amiga Emulator
  * 
  * custom chip support
  *
  * (c) 1995 Bernd Schmidt
  */

extern void custom_init(void);
extern void customreset(void);
extern int intlev(void);
extern void dumpcustom(void);

extern void do_disk(void);

extern int inhibit_frame;
extern int bogusframe;

extern UWORD dmacon;
extern UWORD intena,intreq;

extern int current_hpos (void);

static __inline__ int dmaen(UWORD dmamask)
{
    return (dmamask & dmacon) && (dmacon & 0x200);
}

#define SPCFLAG_STOP 2
#define SPCFLAG_DISK 4
#define SPCFLAG_INT  8
#define SPCFLAG_BRK  16
#define SPCFLAG_EXTRA_CYCLES 32
#define SPCFLAG_TRACE 64
#define SPCFLAG_DOTRACE 128
#define SPCFLAG_DOINT 256
#define SPCFLAG_BLTNASTY 512
#define SPCFLAG_EXEC 1024
#define SPCFLAG_EMULTRAP 2048
#define SPCFLAG_TIMER 4096
#define SPCFLAG_MODE_CHANGE 8192

#define PRESERVED_FLAGS (SPCFLAG_DISK|SPCFLAG_BLTNASTY|SPCFLAG_TIMER)

extern int dskdmaen;
extern UWORD adkcon;

extern UWORD joy0dir;
extern int joy0button;

extern void INTREQ(UWORD);
extern UWORD INTREQR(void);
extern UWORD DMACONR(void);

#define maxhpos 227
#define maxvpos 312
/* The HRM says: The vertical blanking area (PAL) ranges from line 0 to line 29,
 * and no data can be displayed there. Nevertheless, we lose some overscan data
 * if this is set to 29. */
#define minfirstline 21
#define vblank_endline 29
#define numscrlines (maxvpos+1-minfirstline+1)

#define DMA_AUD0      0x0001
#define DMA_AUD1      0x0002
#define DMA_AUD2      0x0004
#define DMA_AUD3      0x0008
#define DMA_DISK      0x0010
#define DMA_SPRITE    0x0020
#define DMA_BLITTER   0x0040
#define DMA_COPPER    0x0080
#define DMA_BITPLANE  0x0100
#define DMA_BLITPRI   0x0400

extern unsigned long frametime, timeframes;

