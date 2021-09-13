 /*
  * UAE - The Un*x Amiga Emulator
  *
  * timer.device emulation
  *
  * Copyright 1996 Bernd Schmidt
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include <assert.h>
#include <signal.h>
#include <ctype.h>

#include "config.h"
#include "options.h"
#include "memory.h"
#include "custom.h"
#include "readcpu.h"
#include "newcpu.h"
#include "xwin.h"
#include "autoconf.h"
#include "osemu.h"
#include "execlib.h"

#define UNIT_MICROHZ 0
#define UNIT_VBLANK 1
/* V36 additions - untested/unimplemented */
#define UNIT_ECLOCK 2
#define UNIT_WAITUNTIL 3
#define UNIT_WAITECLOCK 4

#define TR_ADDREQUEST CMD_NONSTD
#define TR_GETSYSTIME (CMD_NONSTD+1)
#define TR_SETSYSTIME (CMD_NONSTD+2)

#if defined(USE_EXECLIB) && defined(HAVE_SIGACTION)

static struct timeval next_timer, next_schedule, base_time;
static struct itimerval itv;

static CPTR timerbase;

#ifdef __cplusplus
static RETSIGTYPE alarmhandler(...)
#else
static RETSIGTYPE alarmhandler(int foo)
#endif
{
    /* FIXME: This should be an atomic operation throughout... but
     * calling sigblock() each time we want to change the specialflags
     * might be expensive. */
    regs.spcflags |= SPCFLAG_TIMER;
}

static void TIMER_bumpsched(void)
{
    next_schedule.tv_usec += 20*1000;
    if (next_schedule.tv_usec >= 1000*1000) {
	next_schedule.tv_sec ++;
	next_schedule.tv_usec -= 1000*1000;
    }
}

static int timer_sigmask;

void TIMER_block(void) 
{
    sigset_t set;
    EXEC_Forbid(); 
    sigemptyset(&set); sigaddset(&set, SIGALRM);
    sigprocmask(SIG_BLOCK, &set, NULL);
}
void TIMER_unblock(void) 
{
    sigset_t set;
    sigpending(&set);
    if (sigismember(&set, SIGALRM))
	regs.spcflags |= SPCFLAG_TIMER;
    sigemptyset(&set); sigaddset(&set, SIGALRM);
    sigprocmask(SIG_UNBLOCK, &set, NULL);
    EXEC_Permit(); 
}

/*
 * This must be called with timer blocked
 * @@@ make this efficient some day
 */
static void TIMER_rethink(void)
{
    CPTR list = timerbase + 34, node, next;
    struct timeval tv1, tv2, tv_next;

    gettimeofday(&tv1, NULL);
    
    while (tv1.tv_sec > next_schedule.tv_sec
	     || (tv1.tv_sec == next_schedule.tv_sec 
		 && tv1.tv_usec >= next_schedule.tv_usec))
    {
	TIMER_bumpsched();
    }
    tv_next = next_schedule;
    for (node = get_long(list); (next = get_long(node)) != 0; node = next) {
	tv2.tv_sec = get_long(node + 32) + base_time.tv_sec;
	tv2.tv_usec = get_long(node + 36);
	
	if (tv1.tv_sec > tv2.tv_sec
	    || (tv1.tv_sec == tv2.tv_sec && tv1.tv_usec > tv2.tv_usec))
	{
	    /* Timer expired */
	    EXEC_Remove(node);
	    EXEC_ReplyMsg(node);
	    continue;
	}
	
	if (tv_next.tv_sec > tv2.tv_sec
	    || (tv_next.tv_sec == tv2.tv_sec && tv_next.tv_usec > tv2.tv_usec))
	{
	    tv_next = tv2;
	}
    }
    tv_next.tv_usec -= tv1.tv_usec;
    if (tv_next.tv_usec < 0)
	tv_next.tv_usec += 1000*1000, tv_next.tv_sec--;
    tv_next.tv_sec -= tv1.tv_sec;
    if (tv_next.tv_sec < 0)
	fprintf(stderr, "timer: Impossible\n");
    itv.it_value = tv_next;
    setitimer(ITIMER_REAL, &itv, NULL);
}

void TIMER_Interrupt(void)
{
    struct timeval tv;
    
    gettimeofday(&tv, NULL);
    
    if (tv.tv_sec > next_schedule.tv_sec
	|| (tv.tv_sec == next_schedule.tv_sec && tv.tv_usec >= next_schedule.tv_usec))
    {
	EXEC_QuantumElapsed();
	TIMER_bumpsched();
    }
    TIMER_block();
    TIMER_rethink();
    TIMER_unblock();
}

static ULONG tdev_BeginIO(void)
{
    CPTR ioRequest = m68k_areg(regs, 1); /* IOReq */
    int unit = get_long(ioRequest + 24);
    int command = get_word(ioRequest + 28);
    struct timeval tv;
    
    gettimeofday(&tv, NULL);
    tv.tv_sec -= base_time.tv_sec;
    if (unit == UNIT_WAITECLOCK || unit == UNIT_ECLOCK)
	fprintf(stderr, "warning: eclock unit used\n");

    TIMER_block();
    put_byte (ioRequest + 31, 0);
    put_byte (ioRequest + 8, NT_MESSAGE);
    switch (command) {
     case TR_ADDREQUEST:
	if (unit == UNIT_ECLOCK)
	    goto argh;
	
	if (unit == UNIT_VBLANK || unit == UNIT_MICROHZ) {
	    unsigned long int usec, sec;
	    sec = get_long(ioRequest + 32) + tv.tv_sec;
	    usec = get_long(ioRequest + 36) + tv.tv_usec;
	    if (usec > 1000*1000)
		usec -= 1000*1000, sec++;
	    put_long(ioRequest + 32, sec);
	    put_long(ioRequest + 36, usec);
	}
	
	/* clear Quick bit */
	put_byte (ioRequest + 30, 0);
	EXEC_Insert(timerbase + 34, ioRequest, 0);
	TIMER_rethink();
	break;

     case TR_GETSYSTIME:
	put_long(ioRequest + 32, tv.tv_sec);
	put_long(ioRequest + 36, tv.tv_usec);

	if ((get_byte(ioRequest + 30) & 1) == 0) {
	    EXEC_ReplyMsg(ioRequest);
	}
	break;
     case TR_SETSYSTIME:
	fprintf(stderr, "TR_SETSYSTIME\n");
	/* No way dude */
	if ((get_byte(ioRequest + 30) & 1) == 0) {
	    EXEC_ReplyMsg(ioRequest);
	}
	break;

     argh:
	fprintf(stderr, "timer: bogus parameters\n");
     default:
	put_byte (ioRequest+31, (UBYTE)-1);
	break;
    }
    TIMER_unblock();
    return (LONG)(BYTE)get_byte(ioRequest + 31);
}

static ULONG tdev_AbortIO(void)
{
    CPTR ioRequest = m68k_areg(regs, 1);
    
    EXEC_Forbid();
    TIMER_block();
    if ((get_byte(ioRequest + 30) & 1) == 0 
	&& get_byte(ioRequest + 8) != NT_REPLYMSG) 
    {
	EXEC_Remove(ioRequest);
	EXEC_ReplyMsg(ioRequest);
    }
	
    TIMER_unblock();
    EXEC_Permit();
    return 0; /* ?? */
}

static ULONG tdev_Open(void)
{
    CPTR ioRequest = m68k_areg(regs, 1); /* IOReq */
    int unit = m68k_dreg(regs, 0);

    /* Check unit number */
    switch (unit) {
     case UNIT_MICROHZ:
     case UNIT_VBLANK:
     case UNIT_ECLOCK:
     case UNIT_WAITUNTIL:
     case UNIT_WAITECLOCK:
	break;

     default:
	put_long (ioRequest+20, (ULONG)-1);
	put_byte (ioRequest+31, (UBYTE)-1);
	return (ULONG)-1;
    }
    put_word (timerbase + 32, get_word (timerbase + 32) + 1);
    put_long (ioRequest+24, unit); /* io_Unit */
    put_byte (ioRequest+31, 0); /* io_Error */
    put_byte (ioRequest+8, NT_REPLYMSG);
    return 0;
}

static ULONG tdev_Close(void)
{
    put_word (m68k_areg(regs, 6) + 32, get_word (m68k_areg(regs, 6) + 32) - 1);
    return 0;
}

static ULONG tdev_AddTime(void)
{
    CPTR dest = m68k_areg(regs, 0), src = m68k_areg(regs, 1);
    unsigned long int usec, sec;
    
    usec = get_long(dest + 4) + get_long(src + 4);
    sec = get_long(dest) + get_long(src);
    if (usec > 1000*1000)
	usec -= 1000*1000, sec++;
    put_long(dest, sec); put_long(dest + 4, usec);
    return 0;
}

static ULONG tdev_SubTime(void)
{
    CPTR dest = m68k_areg(regs, 0), src = m68k_areg(regs, 1);
    unsigned long int usec, sec;
    
    usec = get_long(dest + 4) - get_long(src + 4);
    sec = get_long(dest) - get_long(src);
    if (get_long(dest+4) < get_long(src+4))
	usec += 1000*1000, sec--;
    put_long(dest, sec); put_long(dest + 4, usec);
    return 0;
}

static ULONG tdev_CmpTime(void)
{
    CPTR dest = m68k_areg(regs, 0), src = m68k_areg(regs, 1);

    if (get_long(dest) > get_long(src))
	return -1;
    if (get_long(dest) < get_long(src))
	return 1;
    if (get_long(dest + 4) > get_long(src + 4))
	return -1;
    if (get_long(dest + 4) < get_long(src + 4))
	return 1;
    return 0;
}

static ULONG tdev_ReadEClock(void)
{
    fprintf(stderr, "ReadEClock called\n");
    return 0;
}

static ULONG tdev_GetSysTime(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    put_long(m68k_areg(regs, 0), tv.tv_sec - base_time.tv_sec);
    put_long(m68k_areg(regs, 0) + 4, tv.tv_usec);
}

#define NR_TIMER_FUNCTIONS 12

void timerdev_init(void)
{
    struct sigaction act;
    
    timerbase = EXEC_AllocMem(6*NR_TIMER_FUNCTIONS + 34 + /* whatever */ 42, 0);
    
    timerbase += 6*NR_TIMER_FUNCTIONS;
    put_long(timerbase + 10, ds("timer.device"));
    put_byte(timerbase + 8, NT_DEVICE);
    
    EXEC_NewList(timerbase + 34); /* We queue time requests here */
    
    EXEC_Enqueue(EXEC_sysbase + 350, timerbase);
    
    libemu_InstallFunctionFlags(tdev_Open, timerbase, -6, 0, "timer: Open");
    libemu_InstallFunctionFlags(tdev_Close, timerbase, -12, 0, "timer: Close");
    put_word(timerbase - 18, 0x4EF9); put_long(timerbase - 16, EXPANSION_nullfunc);
    put_word(timerbase - 24, 0x4EF9); put_long(timerbase - 22, EXPANSION_nullfunc);
    libemu_InstallFunctionFlags(tdev_BeginIO, timerbase, -30, 0, "timer: BeginIO");
    libemu_InstallFunctionFlags(tdev_AbortIO, timerbase, -36, 0, "timer: AbortIO");
    libemu_InstallFunctionFlags(tdev_AddTime, timerbase, -42, 0, "timer: AddTime");
    libemu_InstallFunctionFlags(tdev_SubTime, timerbase, -48, 0, "timer: SubTime");
    libemu_InstallFunctionFlags(tdev_CmpTime, timerbase, -54, 0, "timer: CmpTime");
    libemu_InstallFunctionFlags(tdev_ReadEClock, timerbase, -60, 0, "timer: ReadEClock");
    libemu_InstallFunctionFlags(tdev_GetSysTime, timerbase, -66, 0, "timer: GetSysTime");

    gettimeofday(&base_time, 0);
    /* Keep calculations simple */
    base_time.tv_sec++;
    base_time.tv_usec = 0;
    
    next_schedule = base_time;
    TIMER_bumpsched();

    itv.it_interval.tv_sec = itv.it_interval.tv_usec = 0;
    act.sa_handler = alarmhandler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
#ifdef SA_RESTART
    act.sa_flags = SA_RESTART;
#endif
    sigaction(SIGALRM, &act, NULL);
    TIMER_block();
    TIMER_rethink();
    TIMER_unblock();
}

#else

void timerdev_init(void)
{
}

void TIMER_block(void)
{
}

void TIMER_unblock(void)
{
}

void TIMER_Interrupt(void)
{
}
#endif
