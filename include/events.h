 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Events
  * 
  * (c) 1995 Bernd Schmidt
  */

extern unsigned long int cycles, nextevent;
typedef void (*evfunc)(void);

struct ev
{
    int active;
    unsigned long int evtime, oldcycles;
    evfunc handler;
};

enum { 
    ev_hsync, ev_copper, ev_cia,
    ev_blitter, ev_diskblk, ev_diskindex,
#ifndef DONT_WANT_SOUND
    ev_aud0, ev_aud1, ev_aud2, ev_aud3,
    ev_sample,
#endif
    ev_max
};

extern struct ev eventtab[ev_max];

static __inline__ void events_schedule(void)
{
    int i;
    
    unsigned long int mintime = ~0L;
    for(i = 0; i < ev_max; i++) {
	if (eventtab[i].active) {	    
	    unsigned long int eventtime = eventtab[i].evtime - cycles;
	    if (eventtime < mintime)
	        mintime = eventtime;
	}
    }
    nextevent = cycles + mintime;
}

static __inline__ void do_cycles_slow(void)
{
    if ((nextevent - cycles) <= M68K_SPEED) {
	int j;
	for(j = 0; j < M68K_SPEED; j++) {
	    if (++cycles == nextevent) {
		int i;
		
		for(i = 0; i < ev_max; i++) {
		    if (eventtab[i].active && eventtab[i].evtime == cycles) {
			(*eventtab[i].handler)();
		    }
		}
		events_schedule();
	    }
	}
    } else {
	cycles += M68K_SPEED;
    }
}

static __inline__ void do_cycles_fast(void)
{
    cycles++;
    if (nextevent == cycles) {
	int i;
		
	for(i = 0; i < ev_max; i++) {
	    if (eventtab[i].active && eventtab[i].evtime == cycles) {
		(*eventtab[i].handler)();
	    }
	}
	events_schedule();
    }

}

#if M68K_SPEED == 1
#define do_cycles do_cycles_fast
#else
#define do_cycles do_cycles_slow
#endif
