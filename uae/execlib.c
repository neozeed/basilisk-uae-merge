 /*
  * UAE - The Un*x Amiga Emulator
  *
  * exec.library emulation
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
#include "machdep/exectasks.h"
#include "machdep/m68k.h"
#include "memory.h"
#include "custom.h"
#include "readcpu.h"
#include "newcpu.h"
#include "xwin.h"
#include "autoconf.h"
#include "osemu.h"
#include "execlib.h"

#ifdef USE_EXECLIB

CPTR EXEC_sysbase;

/*
 * Exec list management
 */

void EXEC_NewList(CPTR list)
{
    put_long(list, list + 4);
    put_long(list + 4, 0);
    put_long(list + 8, list);
}

void EXEC_Insert(CPTR list, CPTR node, CPTR pred)
{
    CPTR succ;
    if (pred == 0)
	pred = list;
    put_long(node, succ = get_long(pred));
    put_long(pred, node);
    put_long(succ + 4, node);
    put_long(node + 4, pred);
}

void EXEC_Enqueue(CPTR list, CPTR node)
{
    int pri = (BYTE)get_byte (node + 9);
    CPTR lnode = get_long(list); /* lh_Head */
    CPTR prev = 0, next;
    
    for (;(next = get_long(lnode)) != 0 && (BYTE)get_byte(lnode + 9) >= pri;) {
	prev = lnode;
	lnode = next;
    }
    EXEC_Insert(list, node, prev);
}

void EXEC_Remove(CPTR node)
{
    CPTR pred = get_long(node + 4), succ = get_long(node);
    put_long(pred, succ);
    put_long(succ + 4, pred);
}

ULONG EXEC_RemHead(CPTR list)
{
    CPTR head = get_long(list);
    if (get_long(head) == 0)
	return 0;
    EXEC_Remove(head);
    return head;
}

ULONG EXEC_RemTail(CPTR list)
{
    CPTR tail = get_long(list + 8);
    if (get_long(tail + 4) == 0)
	return 0;
    
    EXEC_Remove(tail);
    return tail;
}

void EXEC_AddTail(CPTR list, CPTR node) 
{
    EXEC_Insert(list, node, get_long(list + 8)); 
}

CPTR EXEC_FindName(CPTR start, char *name)
{
    CPTR node, next;

    if (name == NULL)
	return 0;
    
    for (node = get_long(start);(next = get_long(node)) != 0; node = next) {
	CPTR nnp;
	char *nn;

	nnp = get_long(node + 10);
	if (nnp != 0 && (nn = raddr(nnp)) != NULL)
	    if (strcmp(nn, name) == 0)
		return node;
	start = node;
    }
    return 0;
}

static ULONG execl_Enqueue(void) { EXEC_Enqueue(m68k_areg(regs, 0), m68k_areg(regs, 1)); return 0; }
static ULONG execl_Insert(void) { EXEC_Insert(m68k_areg(regs, 0), m68k_areg(regs, 1), m68k_areg(regs, 2)); return 0; }
static ULONG execl_AddHead(void) { EXEC_Insert(m68k_areg(regs, 0), m68k_areg(regs, 1), 0); return 0; }
static ULONG execl_AddTail(void) { EXEC_Insert(m68k_areg(regs, 0), m68k_areg(regs, 1), get_long(m68k_areg(regs, 0) + 8)); return 0; }
static ULONG execl_Remove(void) { EXEC_Remove(m68k_areg(regs, 1)); return 0; }
static ULONG execl_RemHead(void) { return EXEC_RemHead(m68k_areg(regs, 0)); }
static ULONG execl_RemTail(void) { return EXEC_RemTail(m68k_areg(regs, 0)); }
static ULONG execl_FindName(void) { CPTR n = EXEC_FindName(m68k_areg(regs, 0), raddr(m68k_areg(regs, 1))); ZFLG = n == 0; return n; }

/*
 * Miscellaneous
 */

void EXEC_InitStruct(CPTR inittable, CPTR memory, unsigned long size)
{
    CPTR dptr = memory;
    UBYTE *mem = (UBYTE *)raddr(memory);
    if (memory == 0 || mem == NULL || !valid_address(memory, size)) {
	fprintf(stderr, "Foo\n");
	return;
    }
    memset(mem, 0, size);
    fprintf(stderr, "InitStruct\n");
    for (;;) {
	UBYTE command;
	int count, desttype, srcloc;
	ULONG offset;

	/* Read commands from even bytes */
	inittable = (inittable + 1) & ~1;

	command = get_byte(inittable);
	if (command == 0)
	    break;
	desttype = (command >> 6) & 3;
	srcloc = (command >> 4) & 3;
	count = (command & 15) + 1;

	if (desttype == 3) {
	    dptr = memory + (get_long(inittable) & 0xFFFFFF);
	    inittable += 4;
	} else if (desttype == 2) {
	    dptr = memory + get_byte(inittable+1), inittable += 2;
	} else 
	    inittable++;

	if (srcloc != 0)
	    inittable = (inittable + 1) & ~1;

	if (desttype != 1) {
	    for(; count > 0; count--) {
		switch (srcloc) {
		 case 0: put_long(dptr, get_long(inittable)); dptr += 4; inittable += 4; break;
		 case 1: put_word(dptr, get_word(inittable)); dptr += 2; inittable += 2; break;
		 case 2: put_byte(dptr, get_byte(inittable)); dptr += 1; inittable += 1; break;
		}
	    }
	} else {
	    ULONG data;
	    switch (srcloc) {
	     case 0: data = get_byte(inittable); inittable += 1; break;
	     case 1: data = get_word(inittable); inittable += 2; break;
	     case 2: data = get_long(inittable); inittable += 4; break;
	     default: data = 0; /* Alert */ break;
	    }
	    for(; count > 0; count--) {
		switch (srcloc) {
		 case 0: put_long(dptr, data); dptr += 4; break;
		 case 1: put_word(dptr, data); dptr += 2; break;
		 case 2: put_byte(dptr, data); dptr += 1; break;
		}
	    }
	}
    }
}
/* The size parameter sometimes seems to contain garbage in the top 16 bit */
static ULONG execl_InitStruct(void) { EXEC_InitStruct(m68k_areg(regs, 1), m68k_areg(regs, 2), m68k_dreg(regs, 0) & 0xFFFF); return 0; }

static ULONG execl_GetCC(void)
{
    MakeSR();
    m68k_dreg(regs, 0) = regs.sr & 0xFF;
    return 0; /* ignored */
}

/*
 * Interrupts
 * This is all very confusing. As I see things, the IntVects field of the
 * ExecBase is set up as follows:
 *   One interrupt may either be assigned an IntServer, or an IntHandler.
 *   PORTS, COPER, VERTB EXTER and NMI are set up as Servers (at least that's
 *   what the AutoDocs say), the others as IntHandlers.
 *   An IntServer field has a pointer to an Exec list in the iv_Data field,
 *   the list contains all the server Interrupt nodes in a nice chain. The
 *   iv_Node field is zero. For an IntHandler, the iv_Node field contains a
 *   pointer to the node that was passed in SetIntVector.
 */

CPTR EXEC_SetIntVector(int number, CPTR interrupt)
{
    CPTR oldvec = get_long(EXEC_sysbase + 84 + 12*number + 8);
    if (interrupt == 0) {
	put_long(EXEC_sysbase + 84 + 12*number, 0);
	put_long(EXEC_sysbase + 84 + 12*number + 4, 0);
	put_long(EXEC_sysbase + 84 + 12*number + 8, 0);
    } else {
	put_long(EXEC_sysbase + 84 + 12*number, get_long(interrupt + 14));
	put_long(EXEC_sysbase + 84 + 12*number + 4, get_long(interrupt + 18));
	put_long(EXEC_sysbase + 84 + 12*number + 8, interrupt);
    }
    return oldvec;
}

void EXEC_RemIntServer(ULONG nr, CPTR interrupt)
{
    CPTR list = get_long(EXEC_sysbase + 84 + nr*12);
    
    EXEC_Remove(interrupt);
    /* Disable interrupt if necessary */
    if (get_long(get_long(list)) == 0)
	put_word(0xDFF09A, 1 << nr);
}

void EXEC_AddIntServer(ULONG nr, CPTR interrupt)
{
    CPTR list = get_long(EXEC_sysbase + 84 + nr*12);
    int was_empty;

    /* Disable interrupt if necessary */
    was_empty = get_long(get_long(list)) == 0;
    
    EXEC_Enqueue(list, interrupt);
    if (was_empty)
	put_word(0xDFF09A, 0x8000 | (1 << nr));
    
}

static ULONG execl_SetIntVector(void) { return EXEC_SetIntVector(m68k_dreg(regs, 0), m68k_areg(regs, 1)); }
static ULONG execl_AddIntServer(void) { EXEC_AddIntServer(m68k_dreg(regs, 0) & 15, m68k_areg(regs, 1)); return 0; }
static ULONG execl_RemIntServer(void) { EXEC_RemIntServer(m68k_dreg(regs, 0) & 15, m68k_areg(regs, 1)); return 0; }

/*
 * Memory functions
 */

#define MEMF_PUBLIC 1
#define MEMF_CHIP 2
#define MEMF_FAST 4
#define MEMF_LOCAL 256
#define MEMF_24BITDMA 512
#define MEMF_CLEAR (1<<16)
#define MEMF_LARGEST (1<<17)
#define MEMF_REVERSE (1<<18)
#define MEMF_TOTAL (1<<19)

CPTR EXEC_Allocate(CPTR memheader, unsigned long size)
{
    CPTR chunk, chunkpp;
    unsigned long avail;
    
    size = (size + 7) & ~7;
    
    if (size == 0 || size > get_long(memheader + 28))
	return 0;
	
    for (chunk = get_long(chunkpp = memheader + 16); chunk != 0; chunkpp = chunk, chunk = get_long(chunk))
	if ((avail = get_long(chunk + 4)) >= size) {
	    CPTR nextchunk = get_long(chunk);
	    if (avail-size == 0)
		put_long(chunkpp, nextchunk);
	    else {
		put_long(chunkpp, chunk + size);
		put_long(chunk + size, nextchunk);
		put_long(chunk + size + 4, avail - size);
	    }
	    put_long(memheader + 28, get_long(memheader + 28) - size);
	    return chunk;
	}
    return 0;
}

void EXEC_Deallocate(CPTR memheader, CPTR addr, unsigned long size)
{
    CPTR chunk, chunkpp;
    CPTR areaend;

    if (addr == 0)
	return;

    size = (size + 7) & ~7;
    areaend = addr + size;
    
    put_long(memheader + 28, get_long(memheader + 28) + size);
    
    for (chunk = get_long(chunkpp = memheader + 16); chunk != 0; chunkpp = chunk, chunk = get_long(chunk)) {
	if (chunk + get_long(chunk + 4) == addr) {
	    /* Merge with this and go ahead so we can also merge with the next */
	    put_long(chunkpp, get_long(chunk));
	    addr = chunk; size += get_long(chunk + 4);
	    chunk = chunkpp;
	    continue;
	}
	if (areaend == chunk) {
	    /* Merge with previous */
	    put_long(chunkpp, addr);
	    put_long(addr, get_long(chunk));
	    put_long(addr + 4, get_long(chunk + 4) + size);
	    return;
	}
	if (chunk > addr)
	    break;
    }
    put_long(chunkpp, addr);
    put_long(addr, chunk);
    put_long(addr + 4, size);
}

CPTR EXEC_AllocMem(unsigned long size, ULONG requirements)
{
    CPTR nextmh,  memheader = get_long(EXEC_sysbase + 322);
    ULONG attrs = requirements & (MEMF_PUBLIC | MEMF_CHIP | MEMF_FAST);
    CPTR result = 0;

    size = (size + 7) & ~7;

    for (;(nextmh = get_long(memheader)) != 0; memheader = nextmh) {
	if ((get_word(memheader + 14) & attrs) != attrs)
	    continue;
	result = EXEC_Allocate(memheader, size);
	if (result != 0)
	    break;
    }
    if (result) {
	if (requirements & MEMF_CLEAR)
	    memset(raddr(result), 0, size);
    }
    return result;
}

CPTR EXEC_AllocEntry(CPTR ml)
{
    CPTR newml = EXEC_AllocMem(16 + 8*get_word(ml + 14), MEMF_PUBLIC|MEMF_CLEAR);
    int i;

    if (newml == 0)
	return 0x80000000|MEMF_PUBLIC;
    for (i = 0; i < get_word(ml + 14); i++) {
	ULONG reqs = get_long(ml + 16 + 8*i);
	ULONG addr = EXEC_AllocMem(get_long(ml + 16 + 8*i + 4), reqs);
	if (addr == 0)
	    /* Thank god for SetPatch */
	    return 0x80000000 | reqs;
	put_long(newml + 16 + 8*i, addr);
	put_long(newml + 16 + 8*i + 4, reqs);
    }
    return newml;
}

void EXEC_FreeMem(CPTR addr, unsigned long size)
{
    CPTR nextmh,  memheader = get_long(EXEC_sysbase + 322);

    size = (size + 7) & ~7;
    if (addr == 0)
	return;
    if (size == 0) {
	fprintf(stderr, "warning: Freemem(..., 0)\n");
	return;
    }

    for (;(nextmh = get_long(memheader)) != 0; memheader = nextmh) {
	if (get_long(memheader + 20) <= addr
	    && get_long(memheader + 24) > addr) {
	    EXEC_Deallocate(memheader, addr, size);
	    return;
	}
    }
}

unsigned long EXEC_AvailMem(ULONG requirements)
{
    ULONG attrs = requirements & (MEMF_PUBLIC | MEMF_CHIP | MEMF_FAST);
    CPTR nextmh, memheader = get_long(EXEC_sysbase + 322);
    CPTR result = 0;

    for (;(nextmh = get_long(memheader)) != 0; memheader = nextmh) {
	if ((get_word(memheader + 14) & attrs) != attrs)
	    continue;
	if (attrs & MEMF_LARGEST) {
	    unsigned long maxsize = 0;
	    CPTR chunk;
	    for (chunk = get_long(memheader + 16); chunk != 0; chunk = get_long(chunk))
		if (get_long(chunk+4) > maxsize)
		    maxsize = get_long(chunk + 4);
	    result += maxsize;
	} else if (attrs & MEMF_TOTAL) {
	    result += get_long(memheader + 24) - get_long(memheader + 20);
	} else result += get_long(memheader + 28);
    }
    return result;
}

void EXEC_AddMemList(unsigned long size, ULONG attrs, int pri, CPTR base,
		     CPTR name)
{
    if (size == 0xFFFFFFFF) {
	fprintf(stderr, "Weird AddMemList call\n");
	return;
    }
	
    put_byte(base + 8, 0);
    put_byte(base + 9, pri);
    put_long(base + 10, name);
    put_word(base + 14, attrs);
    put_long(base + 16, base + 32);
    put_long(base + 20, base + 32);
    put_long(base + 36, size - 32);
    put_long(base + 28, size - 32);
    put_long(base + 24, base + size);
    EXEC_Enqueue(EXEC_sysbase + 322, base);
}

static ULONG execl_AddMemList(void)
{
    EXEC_AddMemList(m68k_dreg(regs, 0), m68k_dreg(regs, 1), (LONG)m68k_dreg(regs, 2), m68k_areg(regs, 0), m68k_areg(regs, 1));
    return 0;
}

static ULONG execl_CopyMem(void)
{
    UBYTE *src = (UBYTE *)raddr(m68k_areg(regs, 0));
    UBYTE *dst = (UBYTE *)raddr(m68k_areg(regs, 1));

    if (src != NULL && dst != NULL
	&& valid_address(m68k_areg(regs, 0), m68k_dreg(regs, 0))
	&& valid_address(m68k_areg(regs, 1), m68k_dreg(regs, 0)))
	memcpy(dst, src, m68k_dreg(regs, 0));
    else {
	/* Ho hum. This happens when copying expansion card data. We
	 * better do something sensible in this case */
	unsigned long int i;
	for(i = 0; i < m68k_dreg(regs, 0); i++)
	    put_byte(m68k_areg(regs, 1) + i, get_byte(m68k_areg(regs, 0) + i));
    }
    return 0;
}

static ULONG execl_AllocMem(void) { return EXEC_AllocMem(m68k_dreg(regs, 0), m68k_dreg(regs, 1)); }
static ULONG execl_AllocEntry(void) { return EXEC_AllocEntry(m68k_areg(regs, 0)); }
static ULONG execl_Allocate(void) { return EXEC_Allocate(m68k_areg(regs, 0), m68k_dreg(regs, 0)); }
static ULONG execl_FreeMem(void) { EXEC_FreeMem(m68k_areg(regs, 1), m68k_dreg(regs, 0)); return 0; }
static ULONG execl_AvailMem(void) { return EXEC_AvailMem(m68k_dreg(regs, 1)); }
static ULONG execl_Deallocate(void) { EXEC_Deallocate(m68k_areg(regs, 0), m68k_areg(regs, 1), m68k_dreg(regs, 0)); return 0; }

/*
 * Scheduling
 */

#define TS_RUN 2
#define TS_READY 3
#define TS_WAIT 4
#define TS_EXCEPT 5
#define TS_REMOVED 6

struct NewTask {
    struct NewTask *next, *prev;
    CPTR exectask;
    struct switch_struct sws;
    void *stack;
    struct regstruct regs;
    struct flag_struct flags;
};

static struct NewTask idle_task;
static int intr_count, need_resched, quantum_elapsed;
static struct NewTask *task_to_kill;

static struct NewTask *find_newtask(CPTR task)
{
    /* Ugh, this is ugly */
    struct NewTask *t = &idle_task;
    do {
	if (t->exectask == task)
	    return t;
	t = t->next;
    } while (t != &idle_task);
    fprintf(stderr, "Uh oh. Task list corrupt\n");
    return 0;
}

void EXEC_QuantumElapsed(void)
{
    need_resched = 1;
    quantum_elapsed = 1;
}

static int trace_schedules = 0;

static void schedule(void)
{
    CPTR readylist = EXEC_sysbase + 406;
    CPTR readytask = get_long(readylist);
    CPTR runtask = get_long(EXEC_sysbase + 276);
    struct NewTask *runtask_nt, *readytask_nt;
    unsigned long oldflags;
    UBYTE oldstate;
    int idis;
    
    TIMER_block();
    
    need_resched = 0;
    /* Any ready tasks? */
    if (get_long(readytask) == 0)
	goto dont_schedule;
    
    /* If the running task is going to sleep, don't check priorities */
    oldstate = get_byte(runtask + 15);
    if (oldstate != TS_WAIT && oldstate != TS_REMOVED) {
	/* Has the running task a higher priority than any ready tasks? */
	if ((BYTE)get_byte(runtask + 9) > (BYTE)get_byte(readytask + 9))
	    goto dont_schedule;
	if ((BYTE)get_byte(runtask + 9) == (BYTE)get_byte(readytask + 9)
	    && !quantum_elapsed)
	    goto dont_schedule;
	quantum_elapsed = 0;
    }
    if (oldstate == TS_RUN)
	put_byte(runtask + 15, oldstate = TS_READY);
    if (oldstate == TS_WAIT)
	EXEC_Enqueue(EXEC_sysbase + 420, runtask);
    else 
	/* Removed tasks end up here also, so we can safely Remove() them
	 * always. */
	EXEC_Enqueue(readylist, runtask);
    put_byte(readytask + 15, TS_RUN);
    EXEC_Remove(readytask);
    
    readytask_nt = find_newtask(readytask);
    runtask_nt = find_newtask(runtask);

    oldflags = regs.spcflags;
    runtask_nt->regs = regs;
    runtask_nt->flags = regflags;
    regs = readytask_nt->regs;
    regflags = readytask_nt->flags;
    regs.spcflags = (regs.spcflags & ~PRESERVED_FLAGS) | (oldflags & PRESERVED_FLAGS);
    regs.isp = runtask_nt->regs.isp;
    regs.msp = runtask_nt->regs.msp;
    put_byte(runtask + 16, get_byte(EXEC_sysbase + 294));
    put_byte(runtask + 17, get_byte(EXEC_sysbase + 295));
    put_byte(EXEC_sysbase + 294, idis = get_byte(readytask + 16));
    put_byte(EXEC_sysbase + 295, get_byte(readytask + 17));
    put_long(EXEC_sysbase + 276, readytask);
    /* Set INTENA according to new interrupt enable status */
    if (idis == 0xFF)
	put_word(0xDFF09A, 0xC000);
    else
	put_word(0xDFF09A, 0x4000);

    if (trace_schedules)
	fprintf(stderr, "Task switch: new task %s\n", raddr(get_long(readytask + 10)));

    EXEC_SWITCH_TASKS(runtask_nt, readytask_nt);

    regs.spcflags &= ~SPCFLAG_BRK;
    
    dont_schedule:
    
    TIMER_unblock();

    /* Now we know we are running in a live task, so we can safely remove
     * a corpse if necessary */
    if (task_to_kill != NULL) {
	struct NewTask *nt = task_to_kill;
	task_to_kill = NULL;
	EXEC_Remove(nt->exectask);
	EXEC_FreeMem(nt->exectask, 92);
	free(nt->stack);
	nt->prev->next = nt->next;
	nt->next->prev = nt->prev;
	free(nt);
    }
}

static __inline__ void maybe_schedule(void)
{
    /* @@@ Should we avoid a schedule when supervisor bit is set?
     * Definitely not if the running task is the idle task */
    if (intr_count == 0 && get_byte(EXEC_sysbase + 294) == 0xFF && get_byte(EXEC_sysbase + 295) == 0xFF && need_resched)
	schedule();
}

/*
 * Signals
 */

int EXEC_AllocSignal(int signum)
{
    CPTR thistask = get_long(EXEC_sysbase + 276);
    ULONG sigalloc = get_long(thistask + 18);
    if (signum == -1)
	for (signum = 0; signum < 32; signum++)
	    if ((sigalloc & (1 << signum)) == 0)
		break;
    
    if ((sigalloc & (1 << signum)) == 0) {
	put_long(thistask + 18, sigalloc | (1 << signum));
    } else
	signum = -1;

    return signum;
}

void EXEC_FreeSignal(int signum)
{
    CPTR thistask = get_long(EXEC_sysbase + 276);
    if (signum != -1)
	return;
    put_long(thistask + 18, get_long(thistask + 18) & ~(1 << signum));
}

ULONG EXEC_SetSignal(ULONG newsig, ULONG exec_sigmask)
{
    CPTR task = get_long(EXEC_sysbase + 276);
    ULONG signals = get_long(task + 26);
    /* @@@ check SigExcept here */
    put_long(task + 26, (signals & ~exec_sigmask) | (newsig & exec_sigmask));
    return signals;
}

void EXEC_Signal(CPTR task, ULONG exec_sigmask)
{
    ULONG exec_sigs = get_long(task + 26);
    ULONG sigwait = get_long(task + 22);
    
    if (task == 0) {
/*	fprintf(stderr, "Signalling NULL task. Bad\n");*/
	return;
    }
    put_long(task + 26, exec_sigs | exec_sigmask);
    if (get_byte(task + 15) == TS_WAIT && (sigwait & exec_sigmask) != 0) {
	EXEC_Remove(task);
	put_byte(task + 15, TS_READY);
	EXEC_Enqueue(EXEC_sysbase + 406, task);
	need_resched = 1;
	maybe_schedule();
    }
}

ULONG EXEC_Wait(ULONG exec_sigmask)
{
    CPTR task = get_long(EXEC_sysbase + 276);
    ULONG exec_sigs = get_long(task + 26);
    if ((exec_sigs & exec_sigmask) != 0) {
	put_long(task + 26, exec_sigs & ~exec_sigmask);
	return exec_sigs & exec_sigmask;
    }
    if (get_byte(EXEC_sysbase + 294) != 0xFF || get_byte(EXEC_sysbase + 295) != 0xFF)
    {
	fprintf(stderr, "Arrgh, task Wait()ing when it shouldn't\n");
    }
    if (intr_count || regs.s) {
	fprintf(stderr, "Arrgh, task Wait()ing when it _really_ shouldn't\n");
    }

    put_byte(task + 15, TS_WAIT);
    put_long(task + 22, exec_sigmask);
    schedule();
    exec_sigs = get_long(task + 26);
    if ((exec_sigs & exec_sigmask) != 0) {
	put_long(task + 26, exec_sigs & ~exec_sigmask);
	return exec_sigs & exec_sigmask;
    } 
    fprintf(stderr, "Bug: task woke up without signals\n");
    return 0;
}

static ULONG execl_SetSignal(void) { return EXEC_SetSignal(m68k_dreg(regs, 0), m68k_dreg(regs, 1)); }
static ULONG execl_Signal(void) { EXEC_Signal(m68k_areg(regs, 1), m68k_dreg(regs, 0)); return 0; }
static ULONG execl_Wait(void) { return EXEC_Wait(m68k_dreg(regs, 0)); }
static ULONG execl_AllocSignal(void) { return EXEC_AllocSignal((BYTE)m68k_dreg(regs, 0)); }
static ULONG execl_FreeSignal(void) { EXEC_FreeSignal((BYTE)m68k_dreg(regs, 0)); return 0; }

/*
 * Semaphores
 */

void EXEC_InitSemaphore(CPTR exec_sigsem)
{
    EXEC_NewList(exec_sigsem + 16);
    put_word(exec_sigsem + 14, 0);
    put_word(exec_sigsem + 44, 0xFFFF);
}

ULONG EXEC_AttemptSemaphore(CPTR exec_sigsem)
{
    CPTR task = get_long(EXEC_sysbase + 276);
    UWORD count = get_word(exec_sigsem + 44);
    if (count == 0xFFFF) {
	/* Semaphore free -> lock it */
	put_word(exec_sigsem + 44, 0);
	put_long(exec_sigsem + 40, task);
	put_word(exec_sigsem + 14, 1);
	return 1;
    }
    if (get_long(exec_sigsem + 40) == task) {
	/* Locked by same task -> Increment NestCount */
	put_word(exec_sigsem + 14, get_word(exec_sigsem + 14) + 1);
	return 1;
    }
    return 0;
}

void EXEC_ObtainSemaphore(CPTR exec_sigsem)
{
    CPTR task = get_long(EXEC_sysbase + 276);
    UWORD count = get_word(exec_sigsem + 44);
    CPTR n;

    if (count == 0xFFFF) {
	/* Semaphore free -> lock it */
	put_word(exec_sigsem + 44, 0);
	put_long(exec_sigsem + 40, task);
	put_word(exec_sigsem + 14, 1);
	return;
    }
    if (get_long(exec_sigsem + 40) == task) {
	/* Locked by same task -> Increment NestCount */
	put_word(exec_sigsem + 14, get_word(exec_sigsem + 14) + 1);
	return;
    }
    /* Need to wait. */
    /* @@@ None of the predefined signals in exec/tasks.h seem to be used
     * for this. We use 0x8000, which appears not to be used otherwise */
    put_word(exec_sigsem + 44, count + 1);
    n = EXEC_AllocMem(12, 0); /* Ugh... is there a cleaner way? */
    put_long(n + 8, task);
    EXEC_AddTail(exec_sigsem + 16, n);
    if (EXEC_Wait(0x8000) != 0x8000)
	fprintf(stderr, "BUG\n");
    EXEC_FreeMem(n, 12);
    /* Now it's mine, all mine! */
    put_long(exec_sigsem + 40, task);
    put_word(exec_sigsem + 14, 1);
}

void EXEC_ReleaseSemaphore(CPTR exec_sigsem)
{
    UWORD nest = get_word(exec_sigsem + 14);
    put_word(exec_sigsem + 14, nest - 1);
    if (nest == 1) {
	/* We're losing it. Signal the first task on the wait queue
	 * (that one will in turn wake the next, etc.) */
	CPTR task = get_long(EXEC_RemHead(exec_sigsem + 16) + 8);
	UWORD count = get_word(exec_sigsem + 44);
	put_word(exec_sigsem + 44, count - 1);
	if (task) {
	    EXEC_Signal(task, 0x8000);
	}
    }
}

/* Assumption: Semaphores on the list are only handled through these two
 * functions. I don't see how it could work reliably otherwise. But I don't
 * see either why these functions ought to be used: You might as well have
 * one semaphore instead of a list of them.
 * For safety, these functions try hard to function even in cases where they
 * might otherwise deadlock or break.
 * @@@ what should happen if the task already owns semaphores on the list?
 */

void EXEC_ObtainSemaphoreList(CPTR l)
{
    CPTR task = get_long(EXEC_sysbase + 276);
    CPTR n;

    int retry;
    EXEC_Forbid();
    do {
	retry = 0;
	/* See if any semaphores on the list are locked. If so, go to
	 * sleep */
	n = get_long(l);
	while(get_long(n) != 0) {
	    if (get_word(n + 44) != 0xFFFF && get_long(n + 40) != task) {
		/* Semaphore is locked. Obtain it the normal way */
		EXEC_Permit();
		EXEC_ObtainSemaphore(n);
		EXEC_ReleaseSemaphore(n);
		EXEC_Forbid();
		retry = 1;
		break;
	    }
	    n = get_long(n);
	}
    } while (retry);
    /* All semaphores on the list are unlocked. */
    for (n = get_long(l); get_long(n) != 0; n = get_long(n)) {
	put_word(n + 44, get_word(n + 44) + 1);
	put_long(n + 40, task);
	put_word(n + 14, get_word(n + 14) + 1);
    }
    EXEC_Permit();
}

void EXEC_ReleaseSemaphoreList(CPTR l)
{
    CPTR n;

    EXEC_Forbid();
    for (n = get_long(l); get_long(n) != 0; n = get_long(n)) {
	EXEC_ReleaseSemaphore(n);
    }
    EXEC_Permit();
}

static ULONG execl_ObtainSemaphore(void) { EXEC_ObtainSemaphore(m68k_areg(regs, 0)); return 0; }
static ULONG execl_ReleaseSemaphore(void) { EXEC_ReleaseSemaphore(m68k_areg(regs, 0)); return 0; }
static ULONG execl_ObtainSemaphoreList(void) { EXEC_ObtainSemaphoreList(m68k_areg(regs, 0)); return 0; }
static ULONG execl_ReleaseSemaphoreList(void) { EXEC_ReleaseSemaphoreList(m68k_areg(regs, 0)); return 0; }
static ULONG execl_FindSemaphore(void) { return EXEC_FindName(EXEC_sysbase + 532, raddr(m68k_areg(regs, 1))); }
static ULONG execl_InitSemaphore(void) { EXEC_InitSemaphore(m68k_areg(regs, 0)); return 0; }
static ULONG execl_AttemptSemaphore(void) { return EXEC_AttemptSemaphore(m68k_areg(regs, 0)); }
static ULONG execl_AddSemaphore(void) { put_byte(m68k_areg(regs, 1) + 8, NT_SIGNALSEM); EXEC_Enqueue(EXEC_sysbase + 532, m68k_areg(regs, 1)); return 0; }

/*
 * Messages and ports
 */

CPTR EXEC_GetMsg(CPTR port)
{
    return EXEC_RemHead(port + 20);
}

/* This is shared between PutMsg and ReplyMsg */
static void EXEC_doputmsg(CPTR port, CPTR msg)
{
    UBYTE flags = get_byte (port + 14);
    EXEC_AddTail(port + 20, msg);
    if (flags == 0)
	EXEC_Signal(get_long(port + 16), 1 << get_byte(port + 15));
    else if (flags == 1)
	fprintf(stderr, "PA_SOFTINT unsupported\n");
    
}

void EXEC_PutMsg(CPTR port, CPTR msg)
{
    put_byte(msg + 8, NT_MESSAGE);
    EXEC_doputmsg(port, msg);
}

void EXEC_ReplyMsg(CPTR msg)
{
    CPTR p = get_long(msg + 14);
    if (p == 0) {
	put_byte(msg + 8, NT_FREEMSG);
    } else {
	put_byte(msg + 8, NT_REPLYMSG);
	EXEC_doputmsg(p, msg);
    }
}

CPTR EXEC_WaitPort(CPTR port)
{
    for (;;) {
	CPTR msg = get_long(port + 20);
	if (get_long(msg) != 0)
	    return msg;
	if (get_byte(port + 14) != 0)
	    fprintf(stderr, "Don't know how to WaitPort()\n");
	EXEC_Wait(1 << get_byte(port + 15));
    }
}

void EXEC_AddPort(CPTR port)
{
    put_byte(port + 8, NT_MSGPORT);
    EXEC_Enqueue(EXEC_sysbase + 392, port);
    EXEC_NewList(port + 20);
    /* should we put the task into the appropriate place? */
}

void EXEC_RemPort(CPTR port)
{
    EXEC_Remove(port);
}

static ULONG execl_GetMsg(void) { return EXEC_GetMsg(m68k_areg(regs, 0)); }
static ULONG execl_PutMsg(void) { EXEC_PutMsg(m68k_areg(regs, 0), m68k_areg(regs, 1)); return 0; }
static ULONG execl_ReplyMsg(void) { EXEC_ReplyMsg(m68k_areg(regs, 1)); return 0; }
static ULONG execl_WaitPort(void) { return EXEC_WaitPort(m68k_areg(regs, 0)); }
static ULONG execl_FindPort(void) { return EXEC_FindName(EXEC_sysbase + 392, raddr(m68k_areg(regs, 1))); }
static ULONG execl_AddPort(void) { EXEC_AddPort(m68k_areg(regs, 1)); return 0; }
static ULONG execl_RemPort(void) { EXEC_RemPort(m68k_areg(regs, 1)); return 0; }

/*
 * I/O
 */

LONG EXEC_WaitIO(CPTR ioreq)
{
    /* The device is supposed to clear the IO_QUICK bit if it's going to
     * reply to the command */
    if ((get_byte(ioreq + 30) & 1) == 1)
	return (LONG)(BYTE)get_byte(ioreq + 31);

    for (;;) {
	if (get_byte(ioreq + 8) == NT_REPLYMSG)
	    break;
	/* We'll just hope that this a) has a ReplyPort and b) that port
	 * is of type PA_SIGNAL 
	 * Don't WaitPort() here: WaitPort() returns a message, and we
	 * aren't sure it's for us. */
	
	EXEC_Wait(1 << get_byte(get_long(ioreq + 14) + 15));
    }
    EXEC_Remove(ioreq);
    return (LONG)(BYTE)get_byte(ioreq + 31);
}

static void EXEC_BeginIO(CPTR ioreq)
{
    CPTR dev = get_long(ioreq + 20);
    m68k_areg(regs, 6) = dev;
    m68k_areg(regs, 1) = ioreq;
    Call68k(dev - 30, 1);
    m68k_areg(regs, 6) = EXEC_sysbase;
}

LONG EXEC_DoIO(CPTR ioreq)
{
    put_byte(ioreq + 30, 1);
    EXEC_BeginIO(ioreq);
    return EXEC_WaitIO(ioreq);
}

void EXEC_SendIO(CPTR ioreq)
{
    put_byte(ioreq + 30, 0);
    EXEC_BeginIO(ioreq);
}

CPTR EXEC_CheckIO(CPTR ioreq)
{
    if ((get_byte(ioreq + 30) & 1) == 1)
	return ioreq;
    if (get_byte(ioreq + 8) == NT_REPLYMSG)
	return ioreq;
    return 0;
}

void EXEC_AbortIO(CPTR ioRequest)
{
    CPTR dev = get_long(ioRequest + 20);
    m68k_areg(regs, 6) = dev;
    m68k_areg(regs, 1) = ioRequest;
    /* The example code returns a value here. What should we do with it? */
    Call68k(dev - 36, 1);
    m68k_areg(regs, 6) = EXEC_sysbase;
}

static ULONG execl_WaitIO(void) { return EXEC_WaitIO(m68k_areg(regs, 1)); }
static ULONG execl_DoIO(void) { return EXEC_DoIO(m68k_areg(regs, 1)); }
static ULONG execl_SendIO(void) { EXEC_SendIO(m68k_areg(regs, 1)); return 0; }
static ULONG execl_CheckIO(void) { return EXEC_CheckIO(m68k_areg(regs, 1)); }
static ULONG execl_AbortIO(void) { EXEC_AbortIO(m68k_areg(regs, 1)); return 0; }

/*
 * Libraries
 */

void EXEC_SumLibrary(CPTR lib)
{
    CPTR start = lib - get_word(lib + 16);
    int len = get_word(lib + 16) + get_word(lib + 18);
    ULONG sum = 0;
    UWORD flags = get_word(lib + 14);
    
    if (flags & 1) /* SUMMING? */
	return;
    if (!(flags & 4)) /* SUMUSED? */
	return;
    put_word(lib + 14, flags & ~2); /* mark as not changed */
    /* Pretend the checksum is OK, I don't care enough */
}

CPTR EXEC_SetFunction(CPTR lib, int funcOffset, CPTR function)
{
    CPTR oldfunc = get_long(lib + funcOffset + 2);
    put_word(lib + funcOffset, 0x4EF9);
    put_long(lib + funcOffset + 2, function);
    put_word(lib + 14, get_word(lib + 14) | 2);
    EXEC_SumLibrary(lib);
    return oldfunc;
}

void EXEC_AddLibrary(CPTR lib)
{
    EXEC_Enqueue(EXEC_sysbase + 378, lib);
    put_word(lib + 14, 2); /* mark as changed */
    EXEC_SumLibrary(lib);
}

CPTR EXEC_OpenLibrary(char *name, int version)
{
    CPTR lib = EXEC_FindName(EXEC_sysbase + 378, name);
    
    if (lib != 0 && get_word(lib + 20) < version)
	return 0;
    
    if (lib != 0) {
	CPTR lib2;
	m68k_areg(regs, 6) = lib;
	m68k_dreg(regs, 0) = version;
	lib2 = Call68k(lib - 6, 1);
	if (lib2 == 0)
	    lib = 0;
	else if (lib2 != lib)
	    fprintf(stderr, "OpenLibrary: weirdness\n");
	m68k_areg(regs, 6) = EXEC_sysbase;
    }
    return lib;
}

CPTR EXEC_OpenDevice(char *name, ULONG unit, CPTR ioRequest, ULONG flags)
{
    CPTR dev = EXEC_FindName(EXEC_sysbase + 350, name);
    CPTR replyport = get_long(ioRequest + 14);
    put_long(ioRequest + 20, dev);
    put_byte(ioRequest + 31, 0);
    put_byte(ioRequest + 30, flags); /* is this right? */
    if (replyport == 0)
	fprintf(stderr, "ioRequest has no ReplyPort\n");
    else {
	CPTR rtask = get_long(replyport + 16);
	if (rtask == 0) {
	    fprintf(stderr, "replyTask == 0\n");
	    put_long(replyport + 16, get_long(EXEC_sysbase + 276));
	}
	else if (rtask != get_long(EXEC_sysbase + 276))
	    fprintf(stderr, "replyTask != current\n");
/*	put_long(replyport + 16, get_long(EXEC_sysbase + 276));*/
    }
    if (dev != 0) {
	m68k_areg(regs, 6) = dev;
	m68k_areg(regs, 1) = ioRequest;
	m68k_dreg(regs, 0) = unit;
	m68k_dreg(regs, 1) = flags;
	EXEC_Forbid();
	dev = Call68k(dev - 6, 1);
	EXEC_Permit();
	m68k_areg(regs, 6) = EXEC_sysbase;
    }
    return (LONG)(BYTE)get_byte(ioRequest + 31);
}

/*
 * There seems to be some magic involved here. These functions are documented
 * as returning void. However, if we leave D0 unmodified by giving
 * TRAPFLAG_NORETVAL, there are bogus FreeMem calls; these go away if we
 * return 0 in D0 as the old code did. I guess these are really supposed to
 * return the value that was given to them by the library's Close routine
 * (for the DOS CloseDevice()/CloseLibrary() calls, probably)
 */

ULONG EXEC_CloseDevice(CPTR ioRequest)
{
    CPTR dev = get_long(ioRequest + 20);
    ULONG retval;
    
    m68k_areg(regs, 6) = dev;
    m68k_areg(regs, 1) = ioRequest;
    EXEC_Forbid();
    retval = Call68k(dev - 12, 1);
    EXEC_Permit();
    return retval;
}

ULONG EXEC_CloseLibrary(CPTR lib)
{
    ULONG retval;

    m68k_areg(regs, 6) = lib;
    EXEC_Forbid();
    retval = Call68k(lib - 12, 1);
    EXEC_Permit();
    return retval;
}

void EXEC_AddDevice(CPTR lib)
{
    EXEC_Enqueue(EXEC_sysbase + 350, lib);
}

void EXEC_AddResource(CPTR lib)
{
    EXEC_Enqueue(EXEC_sysbase + 336, lib);
}

CPTR EXEC_OpenResource(char *name)
{
    CPTR lib = EXEC_FindName(EXEC_sysbase + 336, name);
    return lib;
}

void EXEC_MakeFunctions(CPTR target, CPTR funcarray, CPTR funcdispb)
{
    if (funcdispb != 0) {
	WORD tmp;
	for (;;) {
	    tmp = get_word(funcarray); funcarray += 2;
	    if (tmp == -1)
		break;
	    target -= 6;
	    put_word(target, 0x4EF9); /* JMP.L */
	    put_long(target + 2, funcdispb + tmp);
	}
    } else {
	CPTR tmp;
	for (;;) {
	    tmp = get_long(funcarray); funcarray += 4;
	    if (tmp == (CPTR)-1)
		break;
	    target -= 6;
	    put_word(target, 0x4EF9); /* JMP.L */
	    put_long(target + 2, tmp);
	}
    }
}

CPTR EXEC_MakeLibrary(CPTR vectors, CPTR structure, CPTR init,
		      unsigned long dsize, ULONG seglist_b)
{
    CPTR seglist = seglist_b << 2;
    int vec_cnt = 0;
    unsigned long negsize;
    CPTR base, fdispb = 0, funcarray = vectors;
    ULONG retval = 0;

    if (get_word (vectors) == (UWORD)-1) {
	int offs = 2;
	fdispb = vectors;
	funcarray += 2;
	while (get_word (vectors+offs) != (UWORD)-1) {
	    offs += 2;
	    vec_cnt++;
	}
    } else {
	int offs = 0;
	while (get_long (vectors+offs) != (ULONG)-1) {
	    offs += 4; 
	    vec_cnt++;
	}
    }
    negsize = (vec_cnt * 6 + 3) & ~3;
    dsize = (dsize + 3) & ~3;

    base = EXEC_AllocMem(negsize + dsize, MEMF_PUBLIC|MEMF_CLEAR);
    if (base == 0) /* Uh oh */
	return 0;
    EXEC_MakeFunctions(base + negsize, funcarray, fdispb);
    base += negsize;
    put_word(base + 16, negsize);
    put_word(base + 18, dsize);
    if (structure != 0) {
	/* Size is set to 0 so we won't clear it again */
	EXEC_InitStruct(structure, base, 0);
    }
    if (init != 0) {
	m68k_areg(regs, 6) = EXEC_sysbase;
	m68k_areg(regs, 0) = seglist; /* BCPL seglist here? */
	m68k_dreg(regs, 0) = base;
	/* call it */
	retval = Call68k(init, 0);
    } else
	retval = base;
    
    fprintf(stderr, "MakeLibrary: %s\n", raddr(get_long(base+10)));
    return retval;
}

CPTR EXEC_InitResident(CPTR resident, ULONG segList)
{
    UBYTE flags = get_byte(resident + 10);

    if (flags & 0x80) {
	CPTR autoinit = get_long(resident + 22);
	CPTR lib;
	lib = EXEC_MakeLibrary(get_long(autoinit + 4), get_long(autoinit + 8),
			       get_long(autoinit + 12), get_long(autoinit), 
			       segList >> 2);

	/* Is this right? Some libraries never get added if we don't do
	 * this. */
	if (get_byte(resident + 12) == NT_LIBRARY)
	    EXEC_AddLibrary(lib);
	else if (get_byte(resident + 12) == NT_DEVICE)
	    EXEC_AddDevice(lib);
	else if (get_byte(resident + 12) == NT_RESOURCE)
	    EXEC_AddResource(lib);
	return lib;

    }

    m68k_dreg(regs, 0) = 0;
    m68k_areg(regs, 0) = segList;
    m68k_areg(regs, 6) = EXEC_sysbase;
    return Call68k(get_long(resident + 22), 0);
}

CPTR EXEC_FindResident(char *name)
{
    CPTR rmods = get_long(EXEC_sysbase + 300);
    CPTR tmp2;

    if (name == NULL)
	return 0;

    for (; (tmp2 = get_long(rmods)) != 0; rmods += 4) {
	char *name2 = raddr(get_long(tmp2 + 14));
	if (strcmp(name, name2) == 0)
	    return tmp2;
    }
    return 0;
}

static ULONG execl_MakeFunctions(void) { EXEC_MakeFunctions(m68k_areg(regs, 0), m68k_areg(regs, 1), m68k_areg(regs, 2)); return 0; }
static ULONG execl_SumLibrary(void) { EXEC_SumLibrary(m68k_areg(regs, 1)); return 0; }
static ULONG execl_SetFunction(void) { return EXEC_SetFunction(m68k_areg(regs, 1), (WORD)m68k_areg(regs, 0), m68k_dreg(regs, 0)); }
static ULONG execl_AddLibrary(void) { EXEC_AddLibrary(m68k_areg(regs, 1)); return 0; }
static ULONG execl_AddDevice(void) { EXEC_AddDevice(m68k_areg(regs, 1)); return 0; }
static ULONG execl_AddResource(void) { EXEC_AddResource(m68k_areg(regs, 1)); return 0; }
static ULONG execl_OpenLibrary(void) { return EXEC_OpenLibrary(raddr(m68k_areg(regs, 1)), m68k_dreg(regs, 0)); }
static ULONG execl_OldOpenLibrary(void) { return EXEC_OpenLibrary(raddr(m68k_areg(regs, 1)), 0); }
static ULONG execl_CloseLibrary(void) { return EXEC_CloseLibrary(m68k_areg(regs, 1)); }
static ULONG execl_OpenDevice(void) { return EXEC_OpenDevice(raddr(m68k_areg(regs, 0)), m68k_dreg(regs, 0), m68k_areg(regs, 1), m68k_dreg(regs, 1)); }
static ULONG execl_CloseDevice(void) { return EXEC_CloseDevice(m68k_areg(regs, 1)); }
static ULONG execl_OpenResource(void) { return EXEC_OpenResource(raddr(m68k_areg(regs, 1))); }
static ULONG execl_MakeLibrary(void) { return EXEC_MakeLibrary(m68k_areg(regs, 0), m68k_areg(regs, 1), m68k_areg(regs, 2), m68k_dreg(regs, 0), m68k_dreg(regs, 1)); }
static ULONG execl_FindResident(void) { return EXEC_FindResident(raddr(m68k_areg(regs, 1))); }
static ULONG execl_InitResident(void) { return EXEC_InitResident(m68k_areg(regs, 1), m68k_dreg(regs, 1)); }

/*
 * Tasks
 */

static void task_startup(void)
{
    m68k_setpc(regs.pc);
    Call68k_retaddr(regs.pc, 0, get_long(m68k_areg(regs, 7) - 4));
    fprintf(stderr, "Task fell through at the end\n");
}

void EXEC_RemTask(CPTR task)
{
    if (task == 0)
	task = get_long(EXEC_sysbase + 276);
    /* We can't easily delete it here while we use it's stack. Let schedule()
      * do it. */
    put_byte(task + 15, TS_REMOVED);
    task_to_kill = find_newtask(task);
    schedule();
}

CPTR EXEC_AddTask(CPTR task, CPTR initPC, CPTR finalPC)
{
    struct NewTask *nt = (struct NewTask *)malloc(sizeof (struct NewTask));

    nt->exectask = task;
    nt->prev = &idle_task;
    nt->next = idle_task.next;
    idle_task.next->prev = nt;
    idle_task.next = nt;
    memset(&nt->regs, 0, sizeof (nt->regs));
    nt->regs.pc = initPC;
    m68k_areg(nt->regs, 7) = get_long(task + 54);

    if (finalPC == 0)
	finalPC = 0xF0FFF0; /* standard "return from 68k mode" calltrap */
    put_byte(task + 16, 0xFF);
    put_byte(task + 17, 0xFF);
    put_long(m68k_areg(nt->regs, 7) - 4, finalPC);
    nt->stack = malloc(65536); /* @@@ make this smaller after checking that
				* no parts of the emulator need much stack
				* space */    
    EXEC_SETUP_SWS(nt);
    
    put_byte(task + 15, TS_READY);
    put_long(task + 18,0x0000FFFF); /* all tasks have the lower 16 signals allocated */
    EXEC_Enqueue(EXEC_sysbase + 406, task);
    need_resched = 1;
    fprintf(stderr, "AddTask: %s\n", raddr(get_long(task + 10)));
    maybe_schedule();
    return task;
}

int EXEC_SetTaskPri(CPTR task, int pri)
{
    int oldpri = get_byte(task + 9);

    if (task == get_long(EXEC_sysbase + 276)) {
	put_byte(task + 9, pri);
	return oldpri;
    }
    if (get_byte(task + 15) == TS_WAIT) {
	EXEC_Remove(task);
	put_byte(task + 9, pri);
	EXEC_Enqueue(EXEC_sysbase + 420, task);
	return oldpri;
    }
    EXEC_Remove(task);
    put_byte(task + 9, pri);
    EXEC_Enqueue(EXEC_sysbase + 406, task);
    schedule();
    return oldpri;
}

CPTR EXEC_FindTask(char *name)
{
    char *n;
    CPTR v;

    if (name == NULL || ((n = raddr(get_long(get_long(EXEC_sysbase + 276) + 10))) != NULL
			 && strcmp(n, name) == 0))
	return get_long(EXEC_sysbase + 276);
    
    v = EXEC_FindName(EXEC_sysbase + 406, name); /* ready tasks */
    if (v != 0)
	return v;
    return EXEC_FindName(EXEC_sysbase + 420, name); /* waiting tasks */
}

static ULONG execl_FindTask(void) { return EXEC_FindTask(raddr(m68k_areg(regs, 1))); }
static ULONG execl_AddTask(void) { return EXEC_AddTask(m68k_areg(regs, 1), m68k_areg(regs, 2), m68k_areg(regs, 3)); }
static ULONG execl_RemTask(void) { EXEC_RemTask(m68k_areg(regs, 1)); return 0; }
static ULONG execl_SetTaskPri(void) { return EXEC_SetTaskPri(m68k_areg(regs, 1), (BYTE)m68k_dreg(regs, 0)); }

ULONG EXEC_Forbid(void)
{
    int count = (BYTE)get_byte(EXEC_sysbase + 295);
    count++;
    put_byte(EXEC_sysbase + 295, count);
    return 0; 
}
ULONG EXEC_Permit(void) 
{
    int count = (BYTE)get_byte(EXEC_sysbase + 295);
    if (count == -1)
	fprintf(stderr, "Too many Permit() calls\n");
    else
	count--;
    put_byte(EXEC_sysbase + 295, count);    
    maybe_schedule();
    return 0;
}

ULONG EXEC_Disable(void) 
{
    int count = (BYTE)get_byte(EXEC_sysbase + 294);
    count++;
    put_byte(EXEC_sysbase + 294, count);
    put_word(0xDFF09A, 0x4000);
    return 0;
}

ULONG EXEC_Enable(void)
{
    int count = (BYTE)get_byte(EXEC_sysbase + 294);
    if (count == -1)
	fprintf(stderr, "Too many Enable() calls\n");
    else
	count--;
    if (count == -1)
	put_word(0xDFF09A, 0xC000);
    put_byte(EXEC_sysbase + 294, count);
    maybe_schedule();
    return 0;
}

/*
 * Annoyances...
 */

enum rdf_state { rdf_flags, rdf_width, rdf_limit, rdf_type };

CPTR EXEC_RawDoFormat(UBYTE *fstr, CPTR data, CPTR pcp, ULONG pcd)
{
    if (fstr == NULL)
	return data;
    for (;;) {
	UBYTE c = *fstr++;

	if (c == 0)
	    break;

	if (c == '%') {
	    enum rdf_state rdfs = rdf_flags;
	    int ljust = 0, fill0 = 0, islong = 0;
	    int w = 0, lim = 0, havelim = 0;

	    for (;;) {
		c = *fstr++;
		
		if (c == 0)
		    break;
		
		if (rdfs == rdf_flags) {
		    rdfs = rdf_width;
		    if (c == '-') {
			ljust = 1;
			continue;
		    }
		}
		if (rdfs == rdf_width && c == '.') {
		    rdfs = rdf_limit;
		    continue;
		} else if (rdfs == rdf_width && isdigit(c)) {
		    w = w*10 + c - '0';
		    if (w == 0)
			fill0 = 1;
		    continue;
		} else if (rdfs == rdf_limit && isdigit(c)) {
		    lim = lim*10 + c - '0';
		    havelim = 1;
		    continue;
		} else if ((rdfs == rdf_limit || rdfs == rdf_width) && c == 'l') {
		    islong = 1;
		    rdfs = rdf_type;
		    continue;
		}
		switch (c) {
		    CPTR tmp;
		    ULONG tmp1;
		    unsigned int tmpl;
		    char tmps1[20], tmps2[20], *tmpsp;

		 case 'b':
		    tmp = get_long(data); 
		    data += 4;
		    tmpl = get_byte(tmp++);
		    for(; tmpl > 0; tmpl--) {
			m68k_dreg(regs, 0) = get_byte(tmp++);
			m68k_areg(regs, 3) = pcd;
			Call68k(pcp, 0);
		    }
		    break;

		 case 'd':
		 case 'u':
		 case 'x':
		    if (islong) {
			tmp1 = get_long(data); data += 4;
		    } else {
			tmp1 = get_word(data); data += 2;
			if (c == 'd')
			    tmp1 = (LONG)(WORD)tmp1;
		    }
		    tmpsp = tmps1;
		    *tmpsp++ = '%';
		    if (fill0)
			*tmpsp++ = '0';
		    if (w != 0)
			sprintf(tmpsp, "%d", w);
		    tmpsp += strlen(tmpsp);
		    *tmpsp++ = c;
		    *tmpsp++ = 0;
		    sprintf(tmps2, tmps1, tmp1);
		    for (tmpsp = tmps2; ;) {
			m68k_dreg(regs, 0) = *tmpsp++;
			if (m68k_dreg(regs, 0) == 0)
			    break;
			m68k_areg(regs, 3) = pcd;
			Call68k(pcp, 0);			
		    }
		    break;

		 case 's':
		    tmp = get_long(data); 
		    data += 4;
		    for(;!havelim || lim-- > 0;) {
			m68k_dreg(regs, 0) = get_byte(tmp++);
			if (m68k_dreg(regs, 0) == 0)
			    break;
			m68k_areg(regs, 3) = pcd;
			Call68k(pcp, 0);
		    }
		    break;

		 case 'c':
		    if (islong) {
			m68k_dreg(regs, 0) = get_long(data); data += 4;
		    } else {
			m68k_dreg(regs, 0) = get_word(data); data += 2;
		    }
		    m68k_areg(regs, 3) = pcd;
		    Call68k(pcp, 0);
		}
		break;
	    }

	} else {
	    m68k_dreg(regs, 0) = c;
	    m68k_areg(regs, 3) = pcd;
	    Call68k(pcp, 0);
	}
    }
    return data;
}

static ULONG execl_RawDoFormat(void) 
{
    return EXEC_RawDoFormat((UBYTE *)raddr(m68k_areg(regs, 0)), m68k_areg(regs, 1), m68k_areg(regs, 2), m68k_areg(regs, 3)); 
}

/*
 *  Initialization
 */
static ULONG execlib_init(void)
{
    EXEC_sysbase = get_long(4);
#if 1
    /* CopyMem and CopyMemQuick */
    libemu_InstallFunctionFlags(execl_CopyMem, EXEC_sysbase, -630, 0, "CopyMem");
    libemu_InstallFunctionFlags(execl_CopyMem, EXEC_sysbase, -624, 0, "CopyMem");
    libemu_InstallFunctionFlags(execl_Allocate, EXEC_sysbase, -186, 0, "Allocate");
    libemu_InstallFunctionFlags(execl_AllocMem, EXEC_sysbase, -198, 0, "AllocMem");
    libemu_InstallFunctionFlags(execl_Deallocate, EXEC_sysbase, -192, 0, "Deallocate");
    libemu_InstallFunctionFlags(execl_FreeMem, EXEC_sysbase, -210, 0, "FreeMem");
    libemu_InstallFunctionFlags(execl_AvailMem, EXEC_sysbase, -216, 0, "AvailMem");
    
    /* List management */
    libemu_InstallFunctionFlags(execl_Insert, EXEC_sysbase, -234, TRAPFLAG_NORETVAL, "Insert");
    libemu_InstallFunctionFlags(execl_AddHead, EXEC_sysbase, -240, TRAPFLAG_NORETVAL, "AddHead");
    libemu_InstallFunctionFlags(execl_AddTail, EXEC_sysbase, -246, TRAPFLAG_NORETVAL, "AddTail");
    libemu_InstallFunctionFlags(execl_Enqueue, EXEC_sysbase, -270, TRAPFLAG_NORETVAL, "Enqueue");
    libemu_InstallFunctionFlags(execl_RemHead, EXEC_sysbase, -258, 0, "RemHead");
    libemu_InstallFunctionFlags(execl_RemTail, EXEC_sysbase, -264, 0, "RemTail");
    libemu_InstallFunctionFlags(execl_Remove, EXEC_sysbase, -252, TRAPFLAG_NORETVAL, "Remove");
    libemu_InstallFunctionFlags(execl_FindName, EXEC_sysbase, -276, 0, "FindName");
    
    /* Signals */
    libemu_InstallFunctionFlags(execl_AllocSignal, EXEC_sysbase, -330, 0, "AllocSignal");
    libemu_InstallFunctionFlags(execl_FreeSignal, EXEC_sysbase, -336, 0, "FreeSignal");

    /* Semaphores */
    libemu_InstallFunctionFlags(execl_FindSemaphore, EXEC_sysbase, -594, 0, "FindSemaphore");
    libemu_InstallFunctionFlags(execl_InitSemaphore, EXEC_sysbase, -558, 0, "InitSemaphore");
    libemu_InstallFunctionFlags(execl_AddSemaphore, EXEC_sysbase, -600, TRAPFLAG_NORETVAL, "AddSemaphore");

    /* Messages/Ports */
    libemu_InstallFunctionFlags(execl_FindPort, EXEC_sysbase, -390, 0, "FindPort");
    libemu_InstallFunctionFlags(execl_GetMsg, EXEC_sysbase, -372, 0, "GetMsg");

    /* Libraries */
    libemu_InstallFunctionFlags(execl_MakeFunctions, EXEC_sysbase, -90, 0, "MakeFunctions");
    libemu_InstallFunctionFlags(execl_SumLibrary, EXEC_sysbase, -426, TRAPFLAG_NORETVAL, "SumLibrary");
    libemu_InstallFunctionFlags(execl_SetFunction, EXEC_sysbase, -420, 0, "SetFunction");
    
    /* Tasks */
    libemu_InstallFunctionFlags(execl_FindTask, EXEC_sysbase, -294, 0, "FindTask");

    /* Interrupts */
    libemu_InstallFunctionFlags(execl_SetIntVector, EXEC_sysbase, -162, 0, "SetIntVector");
    libemu_InstallFunctionFlags(execl_AddIntServer, EXEC_sysbase, -168, TRAPFLAG_NORETVAL, "AddIntServer");
    libemu_InstallFunctionFlags(execl_RemIntServer, EXEC_sysbase, -174, 0, "RemIntServer");
    
    /* Miscellaneous */
    libemu_InstallFunctionFlags(execl_InitStruct, EXEC_sysbase, -78, 0, "InitStruct");
    libemu_InstallFunctionFlags(execl_GetCC, EXEC_sysbase, -528, TRAPFLAG_NORETVAL|TRAPFLAG_NOREGSAVE, "InitStruct");
#elif 0
    libemu_InstallFunctionFlags(NULL, EXEC_sysbase, -564, TRAPFLAG_NORETVAL, "ObtainSemaphore");
    libemu_InstallFunctionFlags(NULL, EXEC_sysbase, -570, TRAPFLAG_NORETVAL, "ReleaseSemaphore");
    libemu_InstallFunctionFlags(NULL, EXEC_sysbase, -576, 0, "AttemptSemaphore");
    libemu_InstallFunctionFlags(NULL, EXEC_sysbase, -582, TRAPFLAG_NORETVAL, "ObtainSemaphoreList");
    libemu_InstallFunctionFlags(NULL, EXEC_sysbase, -588, TRAPFLAG_NORETVAL, "ReleaseSemaphoreList");
#endif
    return 0;
}

static ULONG execl_Open(void) { return EXEC_sysbase; }

static ULONG execlib_init2(void)
{
    EXEC_sysbase = get_long(4);
    
    libemu_InstallFunctionFlags(execl_Open, EXEC_sysbase, -6, 0, "Open");
    libemu_InstallFunctionFlags(execl_AllocEntry, EXEC_sysbase, -222, 0, "AllocEntry");
    libemu_InstallFunctionFlags(execl_AddMemList, EXEC_sysbase, -618, 0, "AddMemList");

    libemu_InstallFunctionFlags(execl_AddLibrary, EXEC_sysbase, -396, TRAPFLAG_NORETVAL, "AddLibrary");
    libemu_InstallFunctionFlags(execl_AddDevice, EXEC_sysbase, -432, TRAPFLAG_NORETVAL, "AddDevice");
    libemu_InstallFunctionFlags(execl_AddResource, EXEC_sysbase, -486, TRAPFLAG_NORETVAL, "AddResource");
    libemu_InstallFunctionFlags(execl_OpenLibrary, EXEC_sysbase, -552, 0, "OpenLibrary");
    libemu_InstallFunctionFlags(execl_OldOpenLibrary, EXEC_sysbase, -408, 0, "OldOpenLibrary");
    libemu_InstallFunctionFlags(execl_OpenDevice, EXEC_sysbase, -444, 0, "OpenDevice");
    libemu_InstallFunctionFlags(execl_CloseDevice, EXEC_sysbase, -450, 0, "CloseDevice");
    libemu_InstallFunctionFlags(execl_OpenResource, EXEC_sysbase, -498, 0, "OpenResource");
    libemu_InstallFunctionFlags(execl_MakeLibrary, EXEC_sysbase, -84, 0, "MakeLibrary");
    libemu_InstallFunctionFlags(execl_CloseLibrary, EXEC_sysbase, -414, 0, "CloseLibrary");

    libemu_InstallFunctionFlags(execl_RawDoFormat, EXEC_sysbase, -522, 0, "RawDoFmt");
    libemu_InstallFunctionFlags(execl_InitResident, EXEC_sysbase, -102, 0, "InitResident");

    libemu_InstallFunctionFlags(EXEC_Disable, EXEC_sysbase, -120, TRAPFLAG_NORETVAL, "");
    libemu_InstallFunctionFlags(EXEC_Enable, EXEC_sysbase, -126, TRAPFLAG_NORETVAL, "");
    libemu_InstallFunctionFlags(EXEC_Permit, EXEC_sysbase, -138, TRAPFLAG_NORETVAL, "Permit");
    libemu_InstallFunctionFlags(EXEC_Forbid, EXEC_sysbase, -132, TRAPFLAG_NORETVAL, "Forbid");

    libemu_InstallFunctionFlags(execl_AddTask, EXEC_sysbase, -282, 0, "AddTask");
    libemu_InstallFunctionFlags(execl_RemTask, EXEC_sysbase, -288, TRAPFLAG_NORETVAL, "RemTask");
    libemu_InstallFunctionFlags(execl_SetTaskPri, EXEC_sysbase, -300, 0, "SetTaskPri");

    libemu_InstallFunctionFlags(execl_SetSignal, EXEC_sysbase, -306, 0, "SetSignal");
    libemu_InstallFunctionFlags(execl_Signal, EXEC_sysbase, -324, TRAPFLAG_NORETVAL, "Signal");
    libemu_InstallFunctionFlags(execl_Wait, EXEC_sysbase, -318, 0, "Wait");

    libemu_InstallFunctionFlags(execl_PutMsg, EXEC_sysbase, -366, 0, "PutMsg");
    libemu_InstallFunctionFlags(execl_ReplyMsg, EXEC_sysbase, -378, 0, "ReplyMsg");
    libemu_InstallFunctionFlags(execl_WaitPort, EXEC_sysbase, -384, 0, "WaitPort");
    libemu_InstallFunctionFlags(execl_AddPort, EXEC_sysbase, -354, TRAPFLAG_NORETVAL, "AddPort");
    libemu_InstallFunctionFlags(execl_RemPort, EXEC_sysbase, -360, TRAPFLAG_NORETVAL, "RemPort");

    libemu_InstallFunctionFlags(execl_WaitIO, EXEC_sysbase, -474, 0, "WaitIO");
    libemu_InstallFunctionFlags(execl_DoIO, EXEC_sysbase, -456, 0, "DoIO");
    libemu_InstallFunctionFlags(execl_SendIO, EXEC_sysbase, -462, TRAPFLAG_NORETVAL, "SendIO");
    libemu_InstallFunctionFlags(execl_CheckIO, EXEC_sysbase, -468, 0, "CheckIO");
    libemu_InstallFunctionFlags(execl_AbortIO, EXEC_sysbase, -468, 0, "AbortIO");

    libemu_InstallFunctionFlags(execl_ObtainSemaphore, EXEC_sysbase, -564, TRAPFLAG_NORETVAL, "ObtainSemaphore");
    libemu_InstallFunctionFlags(execl_ReleaseSemaphore, EXEC_sysbase, -570, TRAPFLAG_NORETVAL, "ReleaseSemaphore");
    libemu_InstallFunctionFlags(execl_AttemptSemaphore, EXEC_sysbase, -576, 0, "AttemptSemaphore");
    libemu_InstallFunctionFlags(execl_ObtainSemaphoreList, EXEC_sysbase, -582, TRAPFLAG_NORETVAL, "ObtainSemaphoreList");
    libemu_InstallFunctionFlags(execl_ReleaseSemaphoreList, EXEC_sysbase, -588, TRAPFLAG_NORETVAL, "ReleaseSemaphoreList");

    libemu_InstallFunctionFlags(execl_FindResident, EXEC_sysbase, -96, 0, "FindResident");
    return 0;
}

/*
 * These functions are one day going to bootstrap the emulated Exec
 */

#define NR_EXEC_FUNCTIONS 105 /* V34 (1.3) maximum */

static ULONG EXEC_ENOSYS(void)
{
    fprintf(stderr, "Not a system call\n");
    return 0;
}

static ULONG EXEC_StandardIntVec(void)
{
    int num = m68k_areg(regs, 1);
    /* Call the IntHandlers */
    return 0;
}

static int trace_ints = 0;

static ULONG EXEC_IntTrap(void)
{
    struct regstruct rtmp = regs;
    struct flag_struct ftmp = regflags;
    
    UWORD intsreq = get_word(0xDFF01E) & get_word(0xDFF01C);
    int i, do_timer = 0;
    intr_count++;

    if ((intena & 0x4000) == 0)
	fprintf(stderr, "Interrupt came in with interrupts off (%x, %x)!\n", get_byte(EXEC_sysbase + 294), get_byte(EXEC_sysbase + 295));
    
    TIMER_block();
    
    if (regs.spcflags & SPCFLAG_TIMER) {
	regs.spcflags &= ~SPCFLAG_TIMER;
	do_timer = 1;
    }
    
    TIMER_unblock();

    if (do_timer)
	TIMER_Interrupt();
    
    for (i = 13; i >= 0; i--) {
	if ((intsreq & (1 << i)) != 0) {
	    if (trace_ints)
		fprintf(stderr, "INT: %d\n", i);
	    /* Is this a server chain? */
	    if (i == 3 || i == 4 || i == 5 || i == 13 || i == 15) {
		CPTR slist = get_long(EXEC_sysbase + 84 + 12*i);
		CPTR snode = get_long(slist);
		for (;;) {
		    if (get_long(snode) == 0)
			break;
		    m68k_areg(regs, 0) = 0xDFF000;
		    m68k_areg(regs, 5) = get_long(snode + 18);
		    m68k_areg(regs, 1) = get_long(snode + 14);
		    Call68k(m68k_areg(regs, 5), 0);
		    if (ZFLG == 0)
			break;
		    snode = get_long(snode);
		}
		put_word(0xDFF09C, 1 << i);
	    } else {
		CPTR intnode = get_long(EXEC_sysbase + 84 + 12*i + 8);
		/* Blitter interrupts happen before vector is set up :-/ */
		if (intnode != 0) {
		    m68k_dreg(regs, 1) = intsreq;
		    m68k_areg(regs, 1) = get_long(intnode + 14);
		    m68k_areg(regs, 0) = 0xDFF000;
		    m68k_areg(regs, 5) = get_long(intnode + 18);
		    m68k_areg(regs, 6) = EXEC_sysbase;
		    Call68k(m68k_areg(regs, 5), 0);
		}
	    }
	    break;
	}
    }
    if (i < 0)
	fprintf(stderr, "So what interrupt am I supposed to serve?\n");
    intr_count--;
    
    /* This usually is a great indicator that the ExecBase pointer at
     * address 4 has been messed with... */
    if ((intena & 0x4000) == 0)
	fprintf(stderr, "Interrupt left interrupts off (%x, %x)!\n", get_byte(EXEC_sysbase + 294), get_byte(EXEC_sysbase + 295));
    regflags = ftmp;
    regs = rtmp;
    /* Perform a RTE */
    {
	CPTR sra = m68k_areg(regs, 7);
	regs.sr = get_word(sra);
	m68k_setpc_rte(get_long(sra+2));
	m68k_areg(regs, 7) = sra + 6;
	MakeFromSR();
    }

    maybe_schedule();
    return 0; /* ignored */
}

void execlib_sysinit(void)
{
    CPTR tmp, tmp2;
    CPTR enosys = deftrap(EXEC_ENOSYS);
    CPTR intvec = deftrap(EXEC_StandardIntVec);
    CPTR inttrap = deftrap2(EXEC_IntTrap, TRAPFLAG_NORETVAL|TRAPFLAG_NOREGSAVE, "");
    CPTR sysstack;
    int i;

    tmp = here();
    calltrap2(enosys); dw(RTS);
    
    /* why 0x400? */
    EXEC_sysbase = 0x400 + NR_EXEC_FUNCTIONS*6;
    memset(raddr(EXEC_sysbase), 0, 558);
    put_long(4, EXEC_sysbase);

    /* Build vectors. We initialize SetFunction so we can call execlib_Init() */
    tmp2 = here();
    calltrap(deftrap(execl_SetFunction));
    dw(RTS);
    for (i = 0; i < NR_EXEC_FUNCTIONS; i++) {
	put_word(EXEC_sysbase - 6*(i+1), 0x4EF9);
	put_long(EXEC_sysbase - 6*(i+1) + 2, i == 69 ? tmp2 : tmp);
    }

    /* Build syslists */
    EXEC_NewList(EXEC_sysbase + 322); /* MemList */
    EXEC_NewList(EXEC_sysbase + 336); /* ResourceList */
    EXEC_NewList(EXEC_sysbase + 350); /* ResourceList */
    EXEC_NewList(EXEC_sysbase + 364); /* List */
    EXEC_NewList(EXEC_sysbase + 378); /* LibList */
    EXEC_NewList(EXEC_sysbase + 392); /* PortList */
    EXEC_NewList(EXEC_sysbase + 406); /* TaskReady */
    EXEC_NewList(EXEC_sysbase + 420); /* TaskWait */
    EXEC_NewList(EXEC_sysbase + 434); /* SoftInts[5] */
    EXEC_NewList(EXEC_sysbase + 450); 
    EXEC_NewList(EXEC_sysbase + 466); 
    EXEC_NewList(EXEC_sysbase + 482); 
    EXEC_NewList(EXEC_sysbase + 498); 
    EXEC_NewList(EXEC_sysbase + 532); /* SemaphoreList */

    put_long(EXEC_sysbase + 10, ds("exec.library"));
    put_word(EXEC_sysbase + 20, 34); put_word(EXEC_sysbase + 22, 0);
    put_byte(EXEC_sysbase + 8, NT_LIBRARY);
    put_byte(EXEC_sysbase + 9, 0);
    
    put_long(EXEC_sysbase + 42, 0);
    put_long(EXEC_sysbase + 46, 0);
    put_long(EXEC_sysbase + 50, 0);
    put_long(EXEC_sysbase + 62, chipmem_size);
    put_long(EXEC_sysbase + 514, 0xFFFFFFFF);
    put_long(EXEC_sysbase + 518, 0); /* seems to contain random values */
    put_long(EXEC_sysbase + 522, 0);
    put_long(EXEC_sysbase + 526, 0);
    put_word(EXEC_sysbase + 530, 0x3232);
    put_long(EXEC_sysbase + 546, 0);
    put_long(EXEC_sysbase + 550, 0);
    put_long(EXEC_sysbase + 554, 0);
    EXEC_Enqueue(EXEC_sysbase + 378, EXEC_sysbase);
    
    tmp = EXEC_sysbase + 558; /* sizeof struct V34 execbase */
    /* We put the supervisor stack at the end, the user stack for our new
     * task at the end - 0x800, and build the ResModules array at the bottom
     * of the stack */
    sysstack = chipmem_size - 0x2000;
    
    /* Don't mess with Fast or Bogo for now, just set up Chip */
    EXEC_AddMemList(sysstack - tmp, MEMF_CHIP|MEMF_PUBLIC, -10, tmp, 
		    ds("Chip Memory"));

    /* Set up the CPU */
    regs.usp = chipmem_size - 0x800;
    regs.isp = chipmem_size; /* What about MSP? */
    m68k_areg(regs, 7) = regs.usp;
    regs.s = 0; regs.m = 0;
    regs.vbr = 0;
    regs.t0 = regs.t1 = 0;
    regs.intmask = 0;
    put_word(0xDFF09A, 0x7FFF);
    put_word(0xDFF096, 0x7FFF);
    put_word(0xDFF09C, 0x7FFF);
    put_word(0xDFF09A, 0xC000);
    put_word(0xDFF096, 0x8200);
    /* Initialize our supported functions */
    execlib_init();
    execlib_init2();
    
    /* Initialize interrupts */
    intr_count = 0; 
    put_byte(EXEC_sysbase + 294, 0xFF);
    put_byte(EXEC_sysbase + 295, 0xFF);

    tmp = here();
    calltrap2(inttrap);
    dw(RTE);
    for (i = 0; i < 8; i++)
	put_long((24+i)*4, tmp);
    
    tmp = here();
    calltrap2(intvec); dw(RTS);
    for (i = 0; i < 16; i++) {
	tmp2 = 0;
	/* Is this a server chain? */
	if (i == 3 || i == 4 || i == 5 || i == 13 || i == 15)
	    EXEC_NewList(tmp2 = EXEC_AllocMem(14, MEMF_PUBLIC));
	put_long(EXEC_sysbase + 84 + i*12, tmp2);
	put_long(EXEC_sysbase + 84 + i*12 + 4, tmp);
	put_long(EXEC_sysbase + 84 + i*12 + 8, 0);
    }
    put_word(0xDFF09A, 0xAFC3); /* Enable everything but the server chains */
    
    task_to_kill = NULL;
    /* Set up the init task.  We don't really set it up properly. */
    tmp = EXEC_AllocMem(92, MEMF_CLEAR);
    put_long(EXEC_sysbase + 276, tmp);
    put_byte(tmp + 8, NT_TASK);
    put_byte(tmp + 9, -127);
    put_long(tmp + 10, ds("init"));
    put_byte(tmp + 16, 0xFF);
    put_byte(tmp + 17, 0xFF);
    put_long(tmp + 54, chipmem_size - 0x804);
    put_long(tmp + 58, chipmem_size - 0x1800);
    put_long(tmp + 62, chipmem_size - 0x800);
    put_byte(tmp + 15, TS_RUN);
    put_long(tmp + 18,0x0000FFFF);
    EXEC_NewList(tmp + 74);
    idle_task.next = idle_task.prev = &idle_task;
    idle_task.exectask = tmp;
    idle_task.stack = NULL; /* we have a stack for this one already */

    /* Set up an idle task */
    tmp2 = here();
    dw (0xFF0D); dw (0x4eF9); dl (tmp2);

    tmp = EXEC_AllocMem(92, MEMF_CLEAR);
    put_byte(tmp + 8, NT_TASK);
    put_byte(tmp + 9, -128);
    put_long(tmp + 10, ds("idle"));
    put_byte(tmp + 16, 0xFF);
    put_byte(tmp + 17, 0xFF);
    put_long(tmp + 58, chipmem_size - 0x2000);
    put_long(tmp + 62, chipmem_size - 0x1800);
    put_long(tmp + 54, chipmem_size - 0x1804);
    EXEC_NewList(tmp + 74);
    EXEC_AddTask(tmp,  tmp2, 0); /* This won't start executing yet */
/*    find_newtask(tmp)->regs.s = 1;*/


    /* Grep Kickstart for resident modules */
    m68k_areg(regs, 7) -= 4;
    put_long(m68k_areg(regs, 7), 0);
    tmp2 = m68k_areg(regs, 7);
    for (tmp = 0xF80000; tmp < 0xFFFFFF; tmp += 2) {
	if (get_word(tmp) == 0x4AFC && get_long(tmp + 2) == tmp) {
	    m68k_areg(regs, 7) -= 4; put_long(m68k_areg(regs, 7), tmp);
	}
    }
    put_long(EXEC_sysbase + 300, m68k_areg(regs, 7));
    /* Bubble me do */
    for (tmp = m68k_areg(regs, 7); tmp < tmp2; tmp += 4) {
	CPTR tmp3;
	for (tmp3 = tmp2 - 4; tmp3 > tmp; tmp3 -= 4) {
	    if ((BYTE)get_byte(get_long(tmp3) + 13) > (BYTE)get_byte(get_long(tmp3 - 4) + 13)) {
		CPTR tmp4 = get_long(tmp3);
		put_long(tmp3, get_long(tmp3 - 4));
		put_long(tmp3 - 4, tmp4);
	    }
	}
    }
    
    need_resched = quantum_elapsed = 0;

    /* Initialize them and watch everything blow up. */
    for (tmp = m68k_areg(regs, 7); (tmp2 = get_long(tmp)) != 0; tmp += 4) {
	UBYTE flags = get_byte(tmp2 + 10);
	if (!(flags & 1)) /* RTF_COLDSTART? */
	    continue;
	fprintf(stderr, "Initializing %s\n", raddr(get_long(tmp2 + 18)));
/*	if (strcmp("timer.device", raddr(get_long(tmp2 + 14))) == 0) {
	    timerdev_init();
	    continue;
	}*/
	m68k_areg(regs, 6) = EXEC_sysbase;
	EXEC_InitResident(tmp2, /* ? */ 0);

    }
    /* Idle task */
    for(;;) {
	Call68k(0xF0FFF0 - 4, 0);
	schedule();
    }
}

/* 
 *  Install the gfx-library-replacement 
 */
void execlib_install(void)
{
    ULONG begin, end, resname, resid;
    int i;
    
    if (!use_gfxlib)
	return;
    
    resname = ds("UAEexeclib.resource");
    resid = ds("UAE execlib 0.1");

    begin = here();
    dw(0x4AFC);             /* RTC_MATCHWORD */
    dl(begin);              /* our start address */
    dl(0);                  /* Continue scan here */
    dw(0x0101);             /* RTF_COLDSTART; Version 1 */
    dw(0x0805);             /* NT_RESOURCE; pri 5 */
    dl(resname);            /* name */
    dl(resid);              /* ID */
    dl(here() + 4);         /* Init area: directly after this */

    calltrap(deftrap(execlib_init)); dw(RTS);

    end = here();
    org(begin + 6);
    dl(end);

    org(end);

    intr_count = 1; /* So we don't try to schedule if we don't emulate
		     * all of Exec */
}
#else
ULONG EXEC_Forbid(void) { return 0; }
ULONG EXEC_Permit(void) { return 0; }
ULONG EXEC_Enable(void) { return 0; }
ULONG EXEC_Disable(void) { return 0; }
void execlib_install (void) { abort (); }
#endif
