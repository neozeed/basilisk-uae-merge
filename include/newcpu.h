 /* 
  * UAE - The Un*x Amiga Emulator
  * 
  * MC68000 emulation
  *
  * Copyright 1995 Bernd Schmidt
  */

#ifdef BASILISK
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
#endif

extern int areg_byteinc[];
extern int imm8_table[];

extern int movem_index1[256];
extern int movem_index2[256];
extern int movem_next[256];

extern int fpp_movem_index1[256];
extern int fpp_movem_index2[256];
extern int fpp_movem_next[256];

extern int broken_in;
extern int may_run_compiled;

typedef void cpuop_func(ULONG) REGPARAM;

struct cputbl {
    cpuop_func *handler;
    int specific;
    UWORD opcode;
};

extern void op_illg(ULONG) REGPARAM;

typedef char flagtype;

/* Arrrghh.. */

#if defined(USE_COMPILER) && !defined(USE_POINTER)
#define USE_POINTER
#undef NEED_TO_DEBUG_BADLY
#endif
#if defined(NEED_TO_DEBUG_BADLY) && !defined(USE_POINTER)
#define USE_POINTER
#endif

extern struct regstruct 
{
    ULONG regs[16];
    CPTR  usp,isp,msp;
    UWORD sr;
    flagtype t1;
    flagtype t0;
    flagtype s;
    flagtype m;
    flagtype x;
    flagtype stopped;
    int intmask;
    ULONG pc;
#ifdef USE_POINTER
    UBYTE *pc_p;
    UBYTE *pc_oldp;
#endif
    
    ULONG vbr,sfc,dfc;

    double fp[8];
    ULONG fpcr,fpsr,fpiar;
#ifdef USE_EXECLIB
    volatile
#endif
    ULONG spcflags;
    ULONG kick_mask;
} regs, lastint_regs;

#define m68k_dreg(r,num) ((r).regs[(num)])
#define m68k_areg(r,num) ((r).regs[(num)+8])

#ifdef USE_POINTER
static __inline__ ULONG nextibyte(void)
{
    ULONG r = do_get_mem_byte(regs.pc_p+1);
    regs.pc_p += 2;
    return r;
}

static __inline__ ULONG nextiword(void)
{
    ULONG r = do_get_mem_word((UWORD *)regs.pc_p);
    regs.pc_p += 2;
    return r;
}

static __inline__ ULONG nextilong(void)
{
    ULONG r = do_get_mem_long((ULONG *)regs.pc_p);
    regs.pc_p += 4;
    return r;
}
#else

static __inline__ ULONG nextibyte(void)
{
    ULONG r = get_byte(regs.pc+1);
    regs.pc += 2;
    return r;
}

static __inline__ ULONG nextiword(void)
{
    ULONG r = get_aword(regs.pc);
    regs.pc += 2;
    return r;
}

static __inline__ ULONG nextilong(void)
{
    ULONG r = get_along(regs.pc);
    regs.pc += 4;
    return r;
}

#endif

#ifdef USE_POINTER

#if !defined(NEED_TO_DEBUG_BADLY) && !defined(USE_COMPILER)
static __inline__ void m68k_setpc(CPTR newpc)
{
    regs.pc_p = regs.pc_oldp = get_real_address(newpc);
    regs.pc = newpc;
#if USER_PROGRAMS_BEHAVE > 0
    if ((newpc & 0xF80000) != regs.kick_mask)
	regs.spcflags |= SPCFLAG_MODE_CHANGE;
    regs.kick_mask = newpc & 0xF80000;
#endif
}
#else
extern void m68k_setpc(CPTR newpc);
#endif

static __inline__ CPTR m68k_getpc(void)
{
    return regs.pc + ((char *)regs.pc_p - (char *)regs.pc_oldp);
}

#else

static __inline__ void m68k_setpc(CPTR newpc)
{
    regs.pc = newpc;
}

static __inline__ CPTR m68k_getpc(void)
{
    return regs.pc;
}
#endif

#ifdef USE_COMPILER
extern void m68k_setpc_fast(CPTR newpc);
extern void m68k_setpc_bcc(CPTR newpc);
extern void m68k_setpc_rte(CPTR newpc);
#else
#define m68k_setpc_fast m68k_setpc
#define m68k_setpc_bcc  m68k_setpc
#define m68k_setpc_rte  m68k_setpc
#endif

static __inline__ void m68k_setstopped(int stop)
{
    regs.stopped = stop;
    if (stop)
	regs.spcflags |= SPCFLAG_STOP;
}

#if CPU_LEVEL > 1
static __inline__ ULONG get_disp_ea (ULONG base)
{
    UWORD dp = nextiword();
    int reg = (dp >> 12) & 15;
    LONG regd = regs.regs[reg];
    if ((dp & 0x800) == 0)
	regd = (LONG)(WORD)regd;
    regd <<= (dp >> 9) & 3;
    if (dp & 0x100) {
	LONG outer = 0;
	if (dp & 0x80) base = 0;
	if (dp & 0x40) regd = 0;

	if ((dp & 0x30) == 0x20) base += (LONG)(WORD)nextiword();
	if ((dp & 0x30) == 0x30) base += nextilong();
	
	if ((dp & 0x3) == 0x2) outer = (LONG)(WORD)nextiword();
	if ((dp & 0x3) == 0x3) outer = nextilong();
	
	if ((dp & 0x4) == 0) base += regd;
	if (dp & 0x3) base = get_long (base);
	if (dp & 0x4) base += regd;
	
	return base + outer;
    } else {
	return base + (LONG)((BYTE)dp) + regd;
    }
}
#else
static __inline__ ULONG get_disp_ea (ULONG base)
{
    UWORD dp = nextiword();
    int reg = (dp >> 12) & 15;
    LONG regd = regs.regs[reg];
    if ((dp & 0x800) == 0)
	regd = (LONG)(WORD)regd;
    return base + (BYTE)(dp) + regd;
}
#endif

extern LONG ShowEA(int reg, amodes mode, wordsizes size, char *buf);

extern void MakeSR(void);
extern void MakeFromSR(void);
extern void Exception(int, CPTR);
extern void dump_counts(void);
extern void m68k_move2c(int, ULONG *);
extern void m68k_movec2(int, ULONG *);
extern void m68k_divl (ULONG, ULONG, UWORD, CPTR);
extern void m68k_mull (ULONG, ULONG, UWORD);
extern void init_m68k (void);
extern void m68k_go(int);
extern void m68k_dumpstate(CPTR *);
extern void m68k_disasm(CPTR,CPTR *,int);
extern void m68k_reset(void);

extern void mmu_op (ULONG, UWORD);

extern void fpp_opp (ULONG, UWORD);
extern void fdbcc_opp (ULONG, UWORD);
extern void fscc_opp (ULONG, UWORD);
extern void ftrapcc_opp (ULONG,CPTR);
extern void fbcc_opp (ULONG, CPTR, ULONG);
extern void fsave_opp (ULONG);
extern void frestore_opp (ULONG);

#ifdef MEMFUNCS_DIRECT_REQUESTED
#define CPU_OP_NAME(a) op_direct ## a
#else
#define CPU_OP_NAME(a) op ## a
#endif

extern struct cputbl op_smalltbl[];
extern struct cputbl op_direct_smalltbl[];

#ifndef INTEL_FLAG_OPT
extern cpuop_func *cpufunctbl[65536];
#if USER_PROGRAMS_BEHAVE > 0
extern cpuop_func *cpufunctbl_behaved[65536];
#endif
#else
extern cpuop_func *cpufunctbl[65536] __asm__ ("cpufunctbl");
#if USER_PROGRAMS_BEHAVE > 0
extern cpuop_func *cpufunctbl_behaved[65536] __asm__ ("cpufunctbl_behaved");;
#endif
#endif
