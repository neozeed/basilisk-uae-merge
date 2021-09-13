 /* 
  * UAE - The Un*x Amiga Emulator
  * 
  * Blitter emulation
  *
  * (c) 1995 Bernd Schmidt
  */

#if 0
struct bltinfo {
    int blitzero;
    int blitashift,blitbshift,blitdownashift,blitdownbshift;
    ULONG bltadat, bltbdat, bltcdat,bltddat,bltahold,bltbhold,bltafwm,bltalwm;
    int vblitsize,hblitsize;
    int bltamod,bltbmod,bltcmod,bltdmod;
};
#else
struct bltinfo {
    int blitzero;
    int blitashift,blitbshift,blitdownashift,blitdownbshift;
    UWORD bltadat, bltbdat, bltcdat,bltddat,bltahold,bltbhold,bltafwm,bltalwm;
    int vblitsize,hblitsize;
    int bltamod,bltbmod,bltcmod,bltdmod;
};
#endif
extern enum blitter_states {
    BLT_done, BLT_init, BLT_read, BLT_work, BLT_write, BLT_next
} bltstate;

extern struct bltinfo blt_info;

extern UWORD bltsize, oldvblts;
extern UWORD bltcon0,bltcon1;
extern int blinea_shift;
extern ULONG bltapt,bltbpt,bltcpt,bltdpt;

extern void maybe_blit (void);
extern void blitter_handler (void);
extern void build_blitfilltable (void);
extern void do_blitter (void);
extern void blitter_done_notify (void);
typedef void blitter_func(UBYTE *, UBYTE *, UBYTE *, UBYTE *, struct bltinfo *);

#define BLITTER_MAX_WORDS 2048

extern blitter_func *blitfunc_dofast[256];
extern blitter_func *blitfunc_dofast_desc[256];
extern ULONG blit_masktable[BLITTER_MAX_WORDS];
