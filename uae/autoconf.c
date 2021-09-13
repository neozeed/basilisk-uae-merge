 /* 
  * UAE - The Un*x Amiga Emulator
  * 
  * AutoConfig devices
  *
  * Copyright 1995, 1996 Bernd Schmidt
  * Copyright 1996 Ed Hanway
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"
#include "machdep/m68k.h"
#include "memory.h"
#include "custom.h"
#include "readcpu.h"
#include "newcpu.h"
#include "disk.h"
#include "xwin.h"
#include "autoconf.h"

/* We'll need a lot of these. */
#define MAX_TRAPS 4096
static TrapFunction traps[MAX_TRAPS];
static char trapmode[MAX_TRAPS];
static const char *trapstr[MAX_TRAPS];
static CPTR trapoldfunc[MAX_TRAPS];

static int max_trap = 0;
static int trace_traps = 0;
int lasttrap;

static struct {
    CPTR libbase;
    TrapFunction functions[300]; /* I don't think any library has more than 300 functions */
} libpatches[20];
static int n_libpatches = 0;

ULONG Call68k_retaddr(CPTR address, int saveregs, CPTR retaddr)
{
    ULONG retval;
    CPTR oldpc = m68k_getpc();

    struct regstruct backup_regs;
    struct flag_struct backup_flags;
    
    if (saveregs) {
	backup_regs = regs;
	backup_flags = regflags;
    }
    m68k_areg(regs, 7) -= 4;
    put_long (m68k_areg(regs, 7), retaddr);
    m68k_setpc(address);
    m68k_go(0);
    retval = m68k_dreg(regs, 0);
    if (saveregs) {
	regs = backup_regs;
	regflags = backup_flags;
    }
    m68k_setpc(oldpc);
    return retval;
}

ULONG Call68k(CPTR address, int saveregs)
{
    return Call68k_retaddr(address, saveregs, 0xF0FFF0);
}

ULONG CallLib(CPTR base, WORD offset)
{
    int i;    
    CPTR olda6 = m68k_areg(regs, 6);
    ULONG retval;

    for (i = 0; i < n_libpatches; i++) {
	if (libpatches[i].libbase == base && libpatches[i].functions[-offset/6] != NULL)
	    return (*libpatches[i].functions[-offset/6])();
    }
    
    m68k_areg(regs, 6) = base;
    retval = Call68k(base + offset, 1);
    m68k_areg(regs, 6) = olda6;
    return retval;
}

/* @$%&§ compiler bugs */
static volatile int four = 4;

CPTR libemu_InstallFunctionFlags(TrapFunction f, CPTR libbase, int offset,
				 int flags, const char *tracename)
{
    int i;
    CPTR retval;
    CPTR execbase = get_long(four);
    int trnum;
    ULONG addr = here();
    calltrap2(trnum = deftrap2(f, flags, tracename));
    dw(RTS);
    
    m68k_areg(regs, 1) = libbase;
    m68k_areg(regs, 0) = offset;
    m68k_dreg(regs, 0) = addr;
    retval = CallLib(execbase, -420);

    trapoldfunc[trnum] = retval;

    for (i = 0; i < n_libpatches; i++) {
	if (libpatches[i].libbase == libbase)
	    break;
    }
    if (i == n_libpatches) {
	int j;
	libpatches[i].libbase = libbase;
	for (j = 0; j < 300; j++)
	    libpatches[i].functions[j] = NULL;
	n_libpatches++;
    }
    libpatches[i].functions[-offset/6] = f;
    return retval;
}

CPTR libemu_InstallFunction(TrapFunction f, CPTR libbase, int offset,
			    const char *tracename)
{
    return libemu_InstallFunctionFlags(f, libbase, offset, 0, tracename);
}

/* Commonly used autoconfig strings */

CPTR EXPANSION_explibname, EXPANSION_doslibname, EXPANSION_uaeversion;
CPTR EXPANSION_uaedevname, EXPANSION_explibbase = 0, EXPANSION_haveV36;
CPTR EXPANSION_bootcode, EXPANSION_nullfunc;

static int current_deviceno = 0;

int get_new_device(char **devname, CPTR *devname_amiga)
{
    char buffer[80];
    
    sprintf(buffer,"UAE%d", current_deviceno);

    *devname_amiga = ds(*devname = my_strdup(buffer));
    return current_deviceno++;
}

/* ROM tag area memory access */

static UBYTE rtarea[65536];

static ULONG rtarea_lget(CPTR) REGPARAM;
static ULONG rtarea_wget(CPTR) REGPARAM;
static ULONG rtarea_bget(CPTR) REGPARAM;
static void  rtarea_lput(CPTR, ULONG) REGPARAM;
static void  rtarea_wput(CPTR, ULONG) REGPARAM;
static void  rtarea_bput(CPTR, ULONG) REGPARAM;
static UBYTE *rtarea_xlate(CPTR) REGPARAM;

addrbank rtarea_bank = {
    rtarea_lget, rtarea_wget,
    rtarea_lget, rtarea_wget, rtarea_bget,
    rtarea_lput, rtarea_wput, rtarea_bput,
    rtarea_xlate, default_check
};

UBYTE REGPARAM2 *rtarea_xlate(CPTR addr) 
{
    addr &= 0xFFFF;
    return rtarea + addr;
}

ULONG REGPARAM2 rtarea_lget(CPTR addr)
{
    addr &= 0xFFFF;
    return (ULONG)(rtarea_wget(addr) << 16) + rtarea_wget(addr+2);
}

ULONG REGPARAM2 rtarea_wget(CPTR addr)
{
    addr &= 0xFFFF;
    return (rtarea[addr]<<8) + rtarea[addr+1];
}

ULONG REGPARAM2 rtarea_bget(CPTR addr)
{
    UWORD data;
    addr &= 0xFFFF;
    return rtarea[addr];
}

void REGPARAM2 rtarea_lput(CPTR addr, ULONG value) { }
void REGPARAM2 rtarea_bput(CPTR addr, ULONG value) { }

/* Don't start at 0 -- can get bogus writes there. */
static ULONG trap_base_addr = 0x00F00180;

void do_emultrap(int tr)
{
    struct regstruct backup_regs;
    ULONG retval;

    if (*trapstr[tr] != 0 && trace_traps)
	fprintf(stderr, "TRAP: %s\n", trapstr[tr]);

    /* For monitoring only? */
    if (traps[tr] == NULL) {
	Call68k(trapoldfunc[tr], 0);
	if (*trapstr[tr] != 0 && trace_traps)
	    fprintf(stderr, "RET : %s\n", trapstr[tr]);
	return;
    }
    
    if ((trapmode[tr] & TRAPFLAG_NOREGSAVE) == 0)
	backup_regs = regs;
    
    retval = (*traps[tr])();
    
    if ((trapmode[tr] & TRAPFLAG_NOREGSAVE) == 0)
        regs = backup_regs;
    if ((trapmode[tr] & TRAPFLAG_NORETVAL) == 0) {
	m68k_dreg(regs, 0) = retval;
    }
}

void REGPARAM2 rtarea_wput(CPTR addr, ULONG value)
{
    /* Save all registers */
    struct regstruct backup_regs;
    ULONG retval = 0;
    ULONG func = ((addr  - trap_base_addr) & 0xFFFF) >> 1;

    if (func == 0) {
	lasttrap = get_long(m68k_areg(regs, 7));
	m68k_areg(regs, 7) += 4;
	regs.spcflags |= SPCFLAG_EMULTRAP;
	return;
    }
    
    backup_regs = regs;
    if(func < max_trap) {
	retval = (*traps[func])();
    } else {
	fprintf(stderr, "illegal emulator trap\n");
    }
    regs = backup_regs;
    m68k_dreg(regs, 0) = retval;
}

/* some quick & dirty code to fill in the rt area and save me a lot of
 * scratch paper
 */

static int rt_addr = 0;
static int rt_straddr = 0xFF00 - 2;

ULONG addr(int ptr)
{
    return (ULONG)ptr + 0x00F00000;
}

void dw(UWORD data)
{
    rtarea[rt_addr++] = data >> 8;
    rtarea[rt_addr++] = data;
}

void dl(ULONG data)
{
    rtarea[rt_addr++] = data >> 24;
    rtarea[rt_addr++] = data >> 16;
    rtarea[rt_addr++] = data >> 8;
    rtarea[rt_addr++] = data;
}

/* store strings starting at the end of the rt area and working
 * backward.  store pointer at current address
 */

ULONG ds(char *str)
{
    int len = strlen(str) + 1;

    rt_straddr -= len;
    strcpy((char *)rtarea + rt_straddr, str);
    
    return addr(rt_straddr);
}

void calltrap2(ULONG n)
{
    dw(0x4878); /* PEA.L n.w */
    dw(n);
    /* Call trap #0 is reserved for this */
    dw(0x33C0);	/* MOVE.W D0,abs32 */
    dl(trap_base_addr);
}

void calltrap(ULONG n)
{
#if 1
    dw(0x33C0);	/* MOVE.W D0,abs32 */
    dl(n*2 + trap_base_addr);
#else
    calltrap2(n);
#endif
}

void org(ULONG a)
{
    rt_addr = a - 0x00F00000;
}

ULONG here(void)
{
    return addr(rt_addr);
}

int deftrap(TrapFunction func)
{
    int num = max_trap++;
    traps[num] = func;
    trapmode[num] = 0;
    trapstr[num] = "";
    return num;
}

int deftrap2(TrapFunction func, int mode, const char *str)
{
    int num = max_trap++;
    traps[num] = func;
    trapmode[num] = mode;
    trapstr[num] = str;
    return num;
}

void align(int b)
{
    rt_addr = (rt_addr + (b-1)) & ~(b-1);
}

static ULONG bootcode(void)
{
    m68k_areg(regs, 1) = EXPANSION_doslibname;
    m68k_dreg(regs, 0) = CallLib (m68k_areg(regs, 6), -96); /* FindResident() */
    m68k_areg(regs, 0) = get_long(m68k_dreg(regs, 0) + 22); /* get dos.library rt_init */
    return m68k_areg(regs, 0);
}

static ULONG nullfunc(void)
{
    fprintf(stderr, "Null function called\n");
    return 0;
}

void rtarea_init()
{
    ULONG a;
    char uaever[100];
    sprintf(uaever, "uae-%d.%d.%d", (version / 100) % 10, (version / 10) % 10, (version / 1) % 10);

    EXPANSION_uaeversion = ds(uaever);
    EXPANSION_explibname = ds("expansion.library");
    EXPANSION_doslibname = ds("dos.library");
    EXPANSION_uaedevname = ds("uae.device");

    deftrap(NULL); /* Generic emulator trap */
    lasttrap = 0;

    align(4);
    EXPANSION_bootcode = here();
    calltrap2(deftrap2(bootcode, 1, ""));
    dw(0x2040);			/* move.l d0,a0 */
    dw(0x4e90);                 /* jsr (a0) */
    dw(RTS);

    EXPANSION_nullfunc = here();
    calltrap(deftrap(nullfunc));
    dw(RTS);
    
    a = here();
    /* Standard "return from 68k mode" trap */
    org(0xF0FFF0);
    calltrap2(0);
    dw(RTS);
    org(a);
}

static ULONG hyperchip_init(void)
{
#if 0 /* This doesn't work either. Arrrgh!! */
    CPTR execbase = get_long(4);
    CPTR memlist = execbase + 322;
    CPTR memh;
    
    for (;;) {
	memh = get_long(memlist);
	if (get_long(memh) == 0) {
	    fprintf(stderr, "No chip memory !?!??\n");
	    return 0;
	}

	if (get_word(memh + 14) == 3)
	    break;

	memlist = memh;
    }
    put_long(memh + 24, get_long(memh + 24) + chipmem_size - 0x200000);
    put_long(memh + 28, get_long(memh + 28) + chipmem_size - 0x200000);
    memlist = memh + 16;
    for (;;) {
	memh = get_long(memlist);
	if (memh == 0)
	    break;
	memlist = memh;
    }
    put_long(memlist, 0x200000);
    put_long(0x200000, 0);
    put_long(0x200004, chipmem_size - 0x200000);
#endif
    return 0;
}

void rtarea_setup(void)
{
#if 0
    if (chipmem_size > 0x200000) {
	CPTR a = here();
	/* Build a struct Resident for the 8MB chipmem support */
	dw(0x4AFC);
	dl(a);
	dl(a + 0x1A); /* Continue scan here */
	dw(0x0101); /* RTF_COLDSTART; Version 1 */
	dw(0x0305); /* NT_DEVICE; pri 05 */
	a = ds("UAE HyperChip (tm)");
	dl(a); dl(a); /* Identification */
	dl(here() + 4); /* Init code */
	
	calltrap(deftrap(hyperchip_init));
#if 0 /* This doesn't work for some reason */
	dw(0x2c78); dw(4); /* move.l $4,a6 */
	dw(0x203c); dl(chipmem_size - 0x200000);
	dw(0x7203); dw(0x74f6);
	dw(0x207c); dl(0x200000);
	dw(0x227c); dl(ds("Chip RAM"));
	dw(0x4eee); dw(0xfd96); /* jmp AddMemList(a6) */
#endif
    }
#endif
}
