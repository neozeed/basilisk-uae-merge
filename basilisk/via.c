/*
 *  via.c - Minimal VIA emulation (interrupts only)
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
#include "via.h"


// VIA registers
static UBYTE ifr = 0, ier = 0;


// Return CPU interrupt level
int intlev(void)
{
//printf("v");fflush(stdout);
	if (ifr & ier)
		return 1;	// VIA is level 1
	else
		return -1;
}


// Trigger VBL interrupt
void TriggerVBL(void)
{
	ifr |= 0x82;
	if (ier & 0x02)
		regs.spcflags |= SPCFLAG_INT;
//printf("v");fflush(stdout);
}


// Trigger Seconds interrupt
void TriggerSec(void)
{
	ifr |= 0x81;
	if (ier & 0x01)
		regs.spcflags |= SPCFLAG_INT;
//printf("s");fflush(stdout);
}


/* VIA memory access */

const CPTR via_base = 0xefe1fe;		// Mac Classic VIA base address

static ULONG via_lget(CPTR) REGPARAM;
static ULONG via_wget(CPTR) REGPARAM;
static ULONG via_bget(CPTR) REGPARAM;
static void  via_lput(CPTR, ULONG) REGPARAM;
static void  via_wput(CPTR, ULONG) REGPARAM;
static void  via_bput(CPTR, ULONG) REGPARAM;

addrbank via_bank = {
	default_alget, default_awget,
	via_lget, via_wget, via_bget,
	via_lput, via_wput, via_bput,
	default_xlate, default_check
};

ULONG via_lget(CPTR addr)
{
    return via_bget(addr+3);
}

ULONG via_wget(CPTR addr)
{
    return via_bget(addr+1);
}

ULONG via_bget(CPTR addr)
{
	if (addr - via_base == 0x1a00)
		return ifr;
	else if (addr - via_base == 0x1c00)
		return ier | 0x80;
    else
		return 0;
}

void via_lput(CPTR addr, ULONG value)
{
    via_bput(addr+3,value);
}

void via_wput(CPTR addr, ULONG value)
{
    via_bput(addr+1,value);
}

void via_bput(CPTR addr, ULONG value)
{
	if (addr - via_base == 0x1a00)
		ifr &= ~value;
	else if (addr - via_base == 0x1c00)
		if (value & 0x80)
			ier |= value & 0x7f;
		else
			ier &= ~value;
}
