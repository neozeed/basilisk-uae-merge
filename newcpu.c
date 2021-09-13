 /* 
  * UAE - The Un*x Amiga Emulator
  * 
  * MC68000 emulation
  *
  * (c) 1995 Bernd Schmidt
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"
#include "machdep/m68k.h"
#ifndef BASILISK
#include "events.h"
#endif
#include "gui.h"
#ifndef BASILISK
#include "memory.h"
#include "custom.h"
#else
#include "basilisk/memory.h"
#endif
#include "readcpu.h"
#include "newcpu.h"
#include "debug.h"
#include "compiler.h"
#ifndef BASILISK
#include "autoconf.h"
#include "ersatz.h"
#include "blitter.h"
#else
#include "patches.h"
#include "via.h"
#endif

int areg_byteinc[] = { 1,1,1,1,1,1,1,2 };
int imm8_table[] = { 8,1,2,3,4,5,6,7 };

int movem_index1[256];
int movem_index2[256];
int movem_next[256];

int fpp_movem_index1[256];
int fpp_movem_index2[256];
int fpp_movem_next[256];

cpuop_func *cpufunctbl[65536];
#if USER_PROGRAMS_BEHAVE > 0
cpuop_func *cpufunctbl_behaved[65536];
#endif

#define COUNT_INSTRS	0

#if COUNT_INSTRS
 UWORD opcode;
static unsigned long int instrcount[65536];
static UWORD opcodenums[65536];

static int compfn(const void *el1, const void *el2)
{
    return instrcount[*(const UWORD *)el1] < instrcount[*(const UWORD *)el2];
}

void dump_counts(void)
{
    FILE *f = fopen(getenv("INSNCOUNT") ? getenv("INSNCOUNT") : "insncount", "w");
    unsigned long int total = 0;
    int i;
    
    for(i=0; i < 65536; i++) {
	opcodenums[i] = i;
    	total += instrcount[i];
    }
    qsort(opcodenums, 65536, sizeof(UWORD), compfn);
    
    fprintf(f, "Total: %lu\n", total);
    for(i=0; i < 65536; i++) {
	unsigned long int cnt = instrcount[opcodenums[i]];
	struct instr *dp;
	struct mnemolookup *lookup;
	if (!cnt)
	    break;
	dp = table68k + opcodenums[i];
	for (lookup = lookuptab;lookup->mnemo != dp->mnemo; lookup++) ;
	fprintf(f, "%04x: %lu %s\n", opcodenums[i], cnt, lookup->name);
    }
    fclose(f);
}
#else
void dump_counts(void)
{
}
#endif

int broken_in;

void init_m68k (void)
{
    long int opcode;
    int i,j;
    
    for (i = 0 ; i < 256 ; i++) {
	for (j = 0 ; j < 8 ; j++) {
		if (i & (1 << j)) break;
	}
	movem_index1[i] = j;
	movem_index2[i] = 7-j;
	movem_next[i] = i & (~(1 << j));
    }
    for (i = 0 ; i < 256 ; i++) {
	for (j = 7 ; j >= 0 ; j--) {
		if (i & (1 << j)) break;
	}
	fpp_movem_index1[i] = j;
	fpp_movem_index2[i] = 7-j;
	fpp_movem_next[i] = i & (~(1 << j));
    }
#if COUNT_INSTRS
    {
        FILE *f = fopen("insncount", "r");
	memset(instrcount, 0, sizeof instrcount);
	if (f) {
	    ULONG opcode, count, total;
	    char name[20];
	    fscanf(f,"Total: %lu\n",&total);
	    while(fscanf(f,"%lx: %lu %s\n",&opcode,&count,name)==3) {
		instrcount[opcode] = count;
	    }
	    fclose(f);
	}
    }
#endif
    printf("Building CPU table...\n");
    read_table68k ();
    do_merges ();
    for (opcode = 0; opcode < 65536; opcode++)
	cpufunctbl[opcode] = op_illg;
    for (i = 0; op_smalltbl[i].handler != NULL; i++) {
	if (!op_smalltbl[i].specific)
	    cpufunctbl[op_smalltbl[i].opcode] = op_smalltbl[i].handler;
    }
    for (opcode = 0; opcode < 65536; opcode++) {
	cpuop_func *f;
	
	if (table68k[opcode].mnemo == i_ILLG)
	    continue;
	
	if (table68k[opcode].handler != -1) {
	    f = cpufunctbl[table68k[opcode].handler];
	    if (f == op_illg)
		abort();
	    cpufunctbl[opcode] = f;
	}
    }	
    for (i = 0; op_smalltbl[i].handler != NULL; i++) {
	if (op_smalltbl[i].specific)
	    cpufunctbl[op_smalltbl[i].opcode] = op_smalltbl[i].handler;
    }
#if USER_PROGRAMS_BEHAVE > 0
    for (opcode = 0; opcode < 65536; opcode++)
	cpufunctbl_behaved[opcode] = op_illg;

    for (i = 0; op_direct_smalltbl[i].handler != NULL; i++) {
	if (!op_direct_smalltbl[i].specific)
	    cpufunctbl_behaved[op_direct_smalltbl[i].opcode] = op_direct_smalltbl[i].handler;
    }
    for (opcode = 0; opcode < 65536; opcode++) {
	cpuop_func *f;
	
	if (table68k[opcode].mnemo == i_ILLG)
	    continue;
	
	if (table68k[opcode].handler != -1) {
	    f = cpufunctbl_behaved[table68k[opcode].handler];
	    if (f == op_illg)
		abort();
	    cpufunctbl_behaved[opcode] = f;
	}
    }	
    for (i = 0; op_direct_smalltbl[i].handler != NULL; i++) {
	if (op_direct_smalltbl[i].specific)
	    cpufunctbl_behaved[op_direct_smalltbl[i].opcode] = op_direct_smalltbl[i].handler;
    }
#endif
}

struct regstruct regs, lastint_regs;
static struct regstruct regs_backup[16];
static int backup_pointer = 0;
int lastint_no;

LONG ShowEA(int reg, amodes mode, wordsizes size, char *buf)
{
    UWORD dp;
    BYTE disp8;
    WORD disp16;
    int r;
    ULONG dispreg;
    CPTR addr;
    LONG offset = 0;
    char buffer[80];
    
    switch(mode){
     case Dreg:
	sprintf(buffer,"D%d", reg);
	break;
     case Areg:
	sprintf(buffer,"A%d", reg);
	break;
     case Aind:
	sprintf(buffer,"(A%d)", reg);
	break;
     case Aipi:
	sprintf(buffer,"(A%d)+", reg);
	break;
     case Apdi:
	sprintf(buffer,"-(A%d)", reg);
	break;
     case Ad16:
	disp16 = nextiword();
	addr = m68k_areg(regs,reg) + (WORD)disp16;
	sprintf(buffer,"(A%d,$%04x) == $%08lx", reg, disp16 & 0xffff,
					(long unsigned int)addr);
	break;
     case Ad8r:
	dp = nextiword();
	disp8 = dp & 0xFF;
	r = (dp & 0x7000) >> 12;
	dispreg = dp & 0x8000 ? m68k_areg(regs,r) : m68k_dreg(regs,r);	
	if (!(dp & 0x800)) dispreg = (LONG)(WORD)(dispreg);
	dispreg <<= (dp >> 9) & 3;
	
	if (dp & 0x100) {
		LONG outer = 0, disp = 0;
		LONG base = m68k_areg(regs,reg);
		char name[10];
		sprintf(name,"A%d, ",reg);
		if (dp & 0x80) { base = 0; name[0] = 0; }
		if (dp & 0x40) dispreg = 0;
		if ((dp & 0x30) == 0x20) disp = (LONG)(WORD)nextiword();
		if ((dp & 0x30) == 0x30) disp = nextilong();
		base += disp;
		
		if ((dp & 0x3) == 0x2) outer = (LONG)(WORD)nextiword();
		if ((dp & 0x3) == 0x3) outer = nextilong();
		
		if (!(dp & 4)) base += dispreg;
		if (dp & 3) base = get_long (base);
		if (dp & 4) base += dispreg;
		
		addr = base + outer;
		sprintf(buffer,"(%s%c%d.%c*%d+%ld)+%ld == $%08lx", name, 
		       dp & 0x8000 ? 'A' : 'D', (int)r, dp & 0x800 ? 'L' : 'W',
		       1 << ((dp >> 9) & 3),
		       disp,outer,
		       (long unsigned int)addr);
	}
	else {
	  addr = m68k_areg(regs,reg) + (LONG)((BYTE)disp8) + dispreg;
	  sprintf(buffer,"(A%d, %c%d.%c*%d, $%02x) == $%08lx", reg, 
	       dp & 0x8000 ? 'A' : 'D', (int)r, dp & 0x800 ? 'L' : 'W',
	       1 << ((dp >> 9) & 3), disp8,
	       (long unsigned int)addr);
	}
	break;
     case PC16:
	addr = m68k_getpc();
	disp16 = nextiword();
	addr += (WORD)disp16;
	sprintf(buffer,"(PC,$%04x) == $%08lx", disp16 & 0xffff,(long unsigned int)addr);
	break;
     case PC8r:
	addr = m68k_getpc();
	dp = nextiword();
	disp8 = dp & 0xFF;
	r = (dp & 0x7000) >> 12;
	dispreg = dp & 0x8000 ? m68k_areg(regs,r) : m68k_dreg(regs,r);
	if (!(dp & 0x800)) dispreg = (LONG)(WORD)(dispreg);
	dispreg <<= (dp >> 9) & 3;
	
	if (dp & 0x100) {
		LONG outer = 0,disp = 0;
		LONG base = addr;
		char name[10];
		sprintf(name,"PC, ");
		if (dp & 0x80) { base = 0; name[0] = 0; }
		if (dp & 0x80) base = 0;
		if (dp & 0x40) dispreg = 0;
		if ((dp & 0x30) == 0x20) disp = (LONG)(WORD)nextiword();
		if ((dp & 0x30) == 0x30) disp = nextilong();
		base += disp;
		
		if ((dp & 0x3) == 0x2) outer = (LONG)(WORD)nextiword();
		if ((dp & 0x3) == 0x3) outer = nextilong();
		
		if (!(dp & 4)) base += dispreg;
		if (dp & 3) base = get_long (base);
		if (dp & 4) base += dispreg;
		
		addr = base + outer;
		sprintf(buffer,"(%s%c%d.%c*%d+%ld)+%ld == $%08lx", name,
		       dp & 0x8000 ? 'A' : 'D', (int)r, dp & 0x800 ? 'L' : 'W',
		       1 << ((dp >> 9) & 3),
		       disp,outer,
		       (long unsigned int)addr);
	} else {
	  addr += (LONG)((BYTE)disp8) + dispreg;
	  sprintf(buffer,"(PC, %c%d.%c*%d, $%02x) == $%08lx", dp & 0x8000 ? 'A' : 'D', 
		(int)r, dp & 0x800 ? 'L' : 'W',  1 << ((dp >> 9) & 3),
		disp8, (long unsigned int)addr);
	}
	break;
     case absw:
	sprintf(buffer,"$%08lx", (long unsigned int)(LONG)(WORD)nextiword());
	break;
     case absl:
	sprintf(buffer,"$%08lx", (long unsigned int)nextilong());
	break;
     case imm:
	switch(size){
	 case sz_byte:
	    sprintf(buffer,"#$%02x", (unsigned int)(nextiword() & 0xff)); break;
	 case sz_word:
	    sprintf(buffer,"#$%04x", (unsigned int)(nextiword() & 0xffff)); break;
	 case sz_long:
	    sprintf(buffer,"#$%08lx", (long unsigned int)(nextilong())); break;
	 default:
	    break;
	}
	break;
     case imm0:
	offset = (LONG)(BYTE)nextiword();
	sprintf(buffer,"#$%02x", (unsigned int)(offset & 0xff));
	break;
     case imm1:
	offset = (LONG)(WORD)nextiword();
	sprintf(buffer,"#$%04x", (unsigned int)(offset & 0xffff));
	break;
     case imm2:
	offset = (LONG)nextilong();
	sprintf(buffer,"#$%08lx", (long unsigned int)offset);
	break;
     case immi:
	offset = (LONG)(BYTE)(reg & 0xff);
	sprintf(buffer,"#$%08lx", (long unsigned int)offset);
	break;
     default:
	break;
    }
    if (buf == 0)
	printf("%s", buffer);
    else
	strcat(buf, buffer);
    return offset;
}

void MakeSR(void)
{
#if 0
    assert((regs.t1 & 1) == regs.t1);
    assert((regs.t0 & 1) == regs.t0);
    assert((regs.s & 1) == regs.s);
    assert((regs.m & 1) == regs.m);
    assert((XFLG & 1) == XFLG);
    assert((NFLG & 1) == NFLG);
    assert((ZFLG & 1) == ZFLG);
    assert((VFLG & 1) == VFLG);
    assert((CFLG & 1) == CFLG);
#endif
    regs.sr = ((regs.t1 << 15) | (regs.t0 << 14)
	       | (regs.s << 13) | (regs.m << 12) | (regs.intmask << 8)
	       | (XFLG << 4) | (NFLG << 3) | (ZFLG << 2) | (VFLG << 1) 
	       |  CFLG);
}

void MakeFromSR(void)
{
    int oldm = regs.m;
    int olds = regs.s;

    regs.t1 = (regs.sr >> 15) & 1;
    regs.t0 = (regs.sr >> 14) & 1;
    regs.s = (regs.sr >> 13) & 1;
    regs.m = (regs.sr >> 12) & 1;
    regs.intmask = (regs.sr >> 8) & 7;
    XFLG = (regs.sr >> 4) & 1;
    NFLG = (regs.sr >> 3) & 1;
    ZFLG = (regs.sr >> 2) & 1;
    VFLG = (regs.sr >> 1) & 1;
    CFLG = regs.sr & 1;
    if (CPU_LEVEL >= 2) {
	if (olds != regs.s) {
 	    if (olds) {
 	        if (oldm)
 		    regs.msp = m68k_areg(regs, 7);
 	        else
 		    regs.isp = m68k_areg(regs, 7);
 	        m68k_areg(regs, 7) = regs.usp;
 	    } else {
 	        regs.usp = m68k_areg(regs, 7);
 	        m68k_areg(regs, 7) = regs.m ? regs.msp : regs.isp;
 	    }
	} else if (olds && oldm != regs.m) {
	    if (oldm) {
		regs.msp = m68k_areg(regs, 7);
		m68k_areg(regs, 7) = regs.isp;
	    } else {
		regs.isp = m68k_areg(regs, 7);
		m68k_areg(regs, 7) = regs.msp;
	    }
	}
    } else {
	if (olds != regs.s) {
 	    if (olds) {
 		regs.isp = m68k_areg(regs, 7);
 	        m68k_areg(regs, 7) = regs.usp;
 	    } else {
 	        regs.usp = m68k_areg(regs, 7);
 	        m68k_areg(regs, 7) = regs.isp;
 	    }
 	}
    }
    
    regs.spcflags |= SPCFLAG_INT;
    if (regs.t1 || regs.t0)
    	regs.spcflags |= SPCFLAG_TRACE;
    else
    	regs.spcflags &= ~(SPCFLAG_TRACE | SPCFLAG_DOTRACE);
}

void Exception(int nr, CPTR oldpc)
{
    compiler_flush_jsr_stack();
    MakeSR();
    if (!regs.s) {
	regs.usp = m68k_areg(regs, 7);
	if (CPU_LEVEL >= 2)
 	    m68k_areg(regs, 7) = regs.m ? regs.msp : regs.isp;
 	else
 	    m68k_areg(regs, 7) = regs.isp;
	regs.s = 1;
    }
    if (CPU_LEVEL > 0) {
	if (nr == 2 || nr == 3) {
	    int i;
	    for (i = 0 ; i < 12 ; i++) {
		m68k_areg(regs, 7) -= 2;
		put_word (m68k_areg(regs, 7), 0);
	    }
	    m68k_areg(regs, 7) -= 2;
	    put_word (m68k_areg(regs, 7), 0xa000 + nr * 4);
	} else if (nr ==5 || nr == 6 || nr == 7 || nr == 9) {
 	    m68k_areg(regs, 7) -= 4;
 	    put_long (m68k_areg(regs, 7), oldpc);
 	    m68k_areg(regs, 7) -= 2;
 	    put_word (m68k_areg(regs, 7), 0x2000 + nr * 4);
 	} else if (regs.m && nr >= 24 && nr < 32) {
 	    m68k_areg(regs, 7) -= 2;
 	    put_word (m68k_areg(regs, 7), nr * 4);
 	    m68k_areg(regs, 7) -= 4;
 	    put_long (m68k_areg(regs, 7), m68k_getpc ());
 	    m68k_areg(regs, 7) -= 2;
 	    put_word (m68k_areg(regs, 7), regs.sr);
 	    regs.sr |= (1 << 13);
 	    regs.msp = m68k_areg(regs, 7);
 	    m68k_areg(regs, 7) = regs.isp;
 	    m68k_areg(regs, 7) -= 2;
 	    put_word (m68k_areg(regs, 7), 0x1000 + nr * 4);
 	} else {
 	    m68k_areg(regs, 7) -= 2;
 	    put_word (m68k_areg(regs, 7), nr * 4);
 	}
    }
    m68k_areg(regs, 7) -= 4;
    put_long (m68k_areg(regs, 7), m68k_getpc ());
    m68k_areg(regs, 7) -= 2;
    put_word (m68k_areg(regs, 7), regs.sr);
    m68k_setpc(get_long(regs.vbr + 4*nr));
    regs.t1 = regs.t0 = regs.m = 0;
    regs.spcflags &= ~(SPCFLAG_TRACE | SPCFLAG_DOTRACE);
}

static void Interrupt(int nr)
{
    assert(nr < 8 && nr >= 0);
    lastint_regs = regs;
    lastint_no = nr;
    Exception(nr+24, 0);
    
    regs.intmask = nr;
    regs.spcflags |= SPCFLAG_INT;
}

static int caar, cacr;

void m68k_move2c (int regno, ULONG *regp)
{
    if (CPU_LEVEL == 1 && (regno & 0x7FF) > 1)
	op_illg (0x4E7B);
    else
	switch (regno) {
	 case 0: regs.sfc = *regp & 7; break;
	 case 1: regs.dfc = *regp & 7; break;
	 case 2: cacr = *regp & 0x3; break;	/* ignore C and CE */
	 case 0x800: regs.usp = *regp; break;
	 case 0x801: regs.vbr = *regp; break;
	 case 0x802: caar = *regp &0xfc; break;
	 case 0x803: regs.msp = *regp; if (regs.m == 1) m68k_areg(regs, 7) = regs.msp; break;
	 case 0x804: regs.isp = *regp; if (regs.m == 0) m68k_areg(regs, 7) = regs.isp; break;
	 default:
	    op_illg (0x4E7B);
	    break;
	}
}

void m68k_movec2 (int regno, ULONG *regp)
{
    if (CPU_LEVEL == 1 && (regno & 0x7FF) > 1)
	op_illg (0x4E7A);
    else
	switch (regno) {
	 case 0: *regp = regs.sfc; break;
	 case 1: *regp = regs.dfc; break;
	 case 2: *regp = cacr; break;
	 case 0x800: *regp = regs.usp; break;
	 case 0x801: *regp = regs.vbr; break;
	 case 0x802: *regp = caar; break;
	 case 0x803: *regp = regs.m == 1 ? m68k_areg(regs, 7) : regs.msp; break;
	 case 0x804: *regp = regs.m == 0 ? m68k_areg(regs, 7) : regs.isp; break;
	 default:
	    op_illg (0x4E7A);
	    break;
	}
}

static __inline__ int
div_unsigned(ULONG src_hi, ULONG src_lo, ULONG div, ULONG *quot, ULONG *rem)
{
	ULONG q = 0, cbit = 0;
	int i;

	if (div <= src_hi) {
	    return(1);
	}
	for (i = 0 ; i < 32 ; i++) {
		cbit = src_hi & 0x80000000ul;
		src_hi <<= 1;
		if (src_lo & 0x80000000ul) src_hi++;
		src_lo <<= 1;
		q = q << 1;
		if (cbit || div <= src_hi) {
			q |= 1;
			src_hi -= div;
		}
	}
	*quot = q;
	*rem = src_hi;
	return(0);
}

void m68k_divl (ULONG opcode, ULONG src, UWORD extra, CPTR oldpc)
{
#if defined(INT_64BIT)
    if (src == 0) {
	Exception(5,oldpc-2);
	return;
    }
    if (extra & 0x800) {
	/* signed variant */
	INT_64BIT a = (INT_64BIT)(LONG)m68k_dreg(regs, (extra >> 12) & 7);
	INT_64BIT quot, rem;
	
	if (extra & 0x400) {
	    a &= 0xffffffffLL;
	    a |= (INT_64BIT)m68k_dreg(regs, extra & 7) << 32;
	}
	rem = a % (INT_64BIT)(LONG)src;
	quot = a / (INT_64BIT)(LONG)src;
	if ((quot & 0xffffffff80000000uLL) != 0 &&
	    (quot & 0xffffffff80000000uLL) != 0xffffffff80000000uLL) {
	    VFLG = NFLG = 1;
	    CFLG = 0;
	}
	else {
	    if (((LONG)rem < 0) != ((INT_64BIT)a < 0)) rem = -rem;
	    VFLG = CFLG = 0;
	    ZFLG = ((LONG)quot) == 0;
	    NFLG = ((LONG)quot) < 0;
	    m68k_dreg(regs, extra & 7) = rem;
	    m68k_dreg(regs, (extra >> 12) & 7) = quot;
	}
    } else {
	/* unsigned */
	unsigned INT_64BIT a = (unsigned INT_64BIT)(ULONG)m68k_dreg(regs, (extra >> 12) & 7);
	unsigned INT_64BIT quot, rem;
	
	if (extra & 0x400) {
	    a &= 0xffffffffLL;
	    a |= (unsigned INT_64BIT)m68k_dreg(regs, extra & 7) << 32;
	}
	rem = a % (unsigned INT_64BIT)src;
	quot = a / (unsigned INT_64BIT)src;
	if (quot > 0xffffffffLL) {
	    VFLG = NFLG = 1;
	    CFLG = 0;
	}
	else {
	    VFLG = CFLG = 0;
	    ZFLG = ((LONG)quot) == 0;
	    NFLG = ((LONG)quot) < 0;
	    m68k_dreg(regs, extra & 7) = rem;
	    m68k_dreg(regs, (extra >> 12) & 7) = quot;
	}
    }
#else
    if (src == 0) {
	Exception(5,oldpc-2);
	return;
    }
    if (extra & 0x800) {
	/* signed variant */
	LONG lo = (LONG)m68k_dreg(regs, (extra >> 12) & 7);
	LONG hi = lo < 0 ? -1 : 0;
	LONG save_high;
	ULONG quot, rem;
	ULONG sign;
	
	if (extra & 0x400) {
	    hi = (LONG)m68k_dreg(regs, extra & 7);
	}
	save_high = hi;
	sign = (hi ^ src);
	if (hi < 0) {
		hi = ~hi;
		lo = -lo;
		if (lo == 0) hi++;
	}
	if ((LONG)src < 0) src = -src;
	if (div_unsigned(hi, lo, src, &quot, &rem) ||
	    (sign & 0x80000000) ? quot > 0x80000000 : quot > 0x7fffffff) {
	    VFLG = NFLG = 1;
	    CFLG = 0;
	}
	else {
	    if (sign & 0x80000000) quot = -quot;
	    if (((LONG)rem < 0) != (save_high < 0)) rem = -rem;
	    VFLG = CFLG = 0;
	    ZFLG = ((LONG)quot) == 0;
	    NFLG = ((LONG)quot) < 0;
	    m68k_dreg(regs, extra & 7) = rem;
	    m68k_dreg(regs, (extra >> 12) & 7) = quot;
	}
    } else {
	/* unsigned */
	ULONG lo = (ULONG)m68k_dreg(regs, (extra >> 12) & 7);
	ULONG hi = 0;
	ULONG quot, rem;
	
	if (extra & 0x400) {
	    hi = (ULONG)m68k_dreg(regs, extra & 7);
	}
	if (div_unsigned(hi, lo, src, &quot, &rem)) {
	    VFLG = NFLG = 1;
	    CFLG = 0;
	}
	else {
	    VFLG = CFLG = 0;
	    ZFLG = ((LONG)quot) == 0;
	    NFLG = ((LONG)quot) < 0;
	    m68k_dreg(regs, extra & 7) = rem;
	    m68k_dreg(regs, (extra >> 12) & 7) = quot;
	}
    }
#endif
}

static __inline__ void
mul_unsigned(ULONG src1, ULONG src2, ULONG *dst_hi, ULONG *dst_lo)
{
        ULONG r0 = (src1 & 0xffff) * (src2 & 0xffff);
        ULONG r1 = ((src1 >> 16) & 0xffff) * (src2 & 0xffff);
        ULONG r2 = (src1 & 0xffff) * ((src2 >> 16) & 0xffff);
        ULONG r3 = ((src1 >> 16) & 0xffff) * ((src2 >> 16) & 0xffff);
	ULONG lo;

        lo = r0 + ((r1 << 16) & 0xffff0000ul);
        if (lo < r0) r3++;
        r0 = lo;
        lo = r0 + ((r2 << 16) & 0xffff0000ul);
        if (lo < r0) r3++;
        r3 += ((r1 >> 16) & 0xffff) + ((r2 >> 16) & 0xffff);
	*dst_lo = lo;
	*dst_hi = r3;
}

void m68k_mull (ULONG opcode, ULONG src, UWORD extra)
{
#if defined(INT_64BIT)
    if (extra & 0x800) {
	/* signed variant */
	INT_64BIT a = (INT_64BIT)(LONG)m68k_dreg(regs, (extra >> 12) & 7);

	a *= (INT_64BIT)(LONG)src;
	VFLG = CFLG = 0;
	ZFLG = a == 0;
	NFLG = a < 0;
	if (extra & 0x400)
	    m68k_dreg(regs, extra & 7) = a >> 32;
	else if ((a & 0xffffffff80000000uLL) != 0 &&
	         (a & 0xffffffff80000000uLL) != 0xffffffff80000000uLL) {
	    VFLG = 1;
	}
	m68k_dreg(regs, (extra >> 12) & 7) = (ULONG)a;
    } else {
	/* unsigned */
	unsigned INT_64BIT a = (unsigned INT_64BIT)(ULONG)m68k_dreg(regs, (extra >> 12) & 7);
	
	a *= (unsigned INT_64BIT)src;
	VFLG = CFLG = 0;
	ZFLG = a == 0;
	NFLG = ((INT_64BIT)a) < 0;
	if (extra & 0x400)
	    m68k_dreg(regs, extra & 7) = a >> 32;
	else if ((a & 0xffffffff00000000uLL) != 0) {
	    VFLG = 1;
	}
	m68k_dreg(regs, (extra >> 12) & 7) = (ULONG)a;
    }
#else
    if (extra & 0x800) {
	/* signed variant */
	LONG src1,src2;
	ULONG dst_lo,dst_hi;
	ULONG sign;

	src1 = (LONG)src;
	src2 = (LONG)m68k_dreg(regs, (extra >> 12) & 7);
	sign = (src1 ^ src2);
	if (src1 < 0) src1 = -src1;
	if (src2 < 0) src2 = -src2;
	mul_unsigned((ULONG)src1,(ULONG)src2,&dst_hi,&dst_lo);
	if (sign & 0x80000000) {
		dst_hi = ~dst_hi;
		dst_lo = -dst_lo;
		if (dst_lo == 0) dst_hi++;
	}
	VFLG = CFLG = 0;
	ZFLG = dst_hi == 0 && dst_lo == 0;
	NFLG = ((LONG)dst_hi) < 0;
	if (extra & 0x400)
	    m68k_dreg(regs, extra & 7) = dst_hi;
	else if ((dst_hi != 0 || (dst_lo & 0x80000000) != 0) &&
	         ((dst_hi & 0xffffffff) != 0xffffffff ||
		  (dst_lo & 0x80000000) != 0x80000000)) {
	    VFLG = 1;
	}
	m68k_dreg(regs, (extra >> 12) & 7) = dst_lo;
    } else {
	/* unsigned */
	ULONG dst_lo,dst_hi;

	mul_unsigned(src,(ULONG)m68k_dreg(regs, (extra >> 12) & 7),&dst_hi,&dst_lo);
	
	VFLG = CFLG = 0;
	ZFLG = dst_hi == 0 && dst_lo == 0;
	NFLG = ((LONG)dst_hi) < 0;
	if (extra & 0x400)
	    m68k_dreg(regs, extra & 7) = dst_hi;
	else if (dst_hi != 0) {
	    VFLG = 1;
	}
	m68k_dreg(regs, (extra >> 12) & 7) = dst_lo;
    }
#endif
}
static char* ccnames[] =
{ "T ","F ","HI","LS","CC","CS","NE","EQ",
  "VC","VS","PL","MI","GE","LT","GT","LE" };

void m68k_reset(void)
{
#ifndef BASILISK
    m68k_areg(regs, 7) = get_long(0x00f80000);
    m68k_setpc(get_long(0x00f80004));
#else
    m68k_areg(regs, 7) = get_long(rommem_start);
    m68k_setpc(get_long(rommem_start+4));
#endif
    regs.kick_mask = 0xF80000;
    regs.s = 1;
    regs.m = 0;
    regs.stopped = 0;
    regs.t1 = 0;
    regs.t0 = 0;
    ZFLG = CFLG = NFLG = VFLG = 0;
    regs.spcflags = 0;
    regs.intmask = 7;
    regs.vbr = regs.sfc = regs.dfc = 0;
    regs.fpcr = regs.fpsr = regs.fpiar = 0;
    customreset();
}

void REGPARAM2 op_illg(ULONG opcode)
{
    compiler_flush_jsr_stack();
#ifndef BASILISK
    if (opcode == 0x4E7B && get_long(0x10) == 0 
	&& (m68k_getpc() & 0xF80000) == 0xF80000) 
    {
	fprintf(stderr, "Your Kickstart requires a 68020 CPU. Giving up.\n");
	broken_in = 1; 
	regs.spcflags |= SPCFLAG_BRK;
	quit_program = 1;
    }
    if (opcode == 0xFF0D) {
	if ((m68k_getpc() & 0xF80000) == 0xF80000) {
	    /* This is from the dummy Kickstart replacement */
	    ersatz_perform (nextiword ());
	    return;
	} else if ((m68k_getpc() & 0xF80000) == 0xF00000) {
	    /* User-mode STOP replacement */
	    m68k_setstopped(1);
	    return;
	}
    }
#else
    if (opcode == 0xFF0E) {
		regs.spcflags |= SPCFLAG_EMULTRAP;
		return;
	}
    if (opcode == 0xFF0D) {
	/* This is from the dummy Kickstart replacement */
	ersatz_perform (nextiword ());
	return;
    }
#endif
#ifdef USE_POINTER
    regs.pc_p -= 2;
#else
    regs.pc -= 2;
#endif
    if ((opcode & 0xF000) == 0xF000) {
	Exception(0xB,0);
	return;
    }
    if ((opcode & 0xF000) == 0xA000) {
    	Exception(0xA,0);
	return;
    }
    fprintf(stderr, "Illegal instruction: %04x at %08lx\n", opcode, m68k_getpc());
    Exception(4,0);
}

void mmu_op(ULONG opcode, UWORD extra)
{
    if ((extra & 0xB000) == 0) { /* PMOVE instruction */

    } else if ((extra & 0xF000) == 0x2000) { /* PLOAD instruction */
    } else if ((extra & 0xF000) == 0x8000) { /* PTEST instruction */
    } else
	op_illg(opcode);
}

static int n_insns=0, n_spcinsns=0;

static CPTR last_trace_ad = 0;

static __inline__ void do_trace(void)
{
    if (regs.spcflags & SPCFLAG_TRACE) {		/* 6 */
	if (regs.t0) {
	    UWORD opcode;
	    /* should also include TRAP, CHK, SR modification FPcc */
	    /* probably never used so why bother */
	    /* We can afford this to be inefficient... */
	    m68k_setpc(m68k_getpc());
	    opcode = get_word(regs.pc);
	    if (opcode == 0x4e72 		/* RTE */
		|| opcode == 0x4e74 		/* RTD */
		|| opcode == 0x4e75 		/* RTS */
		|| opcode == 0x4e77 		/* RTR */
		|| opcode == 0x4e76 		/* TRAPV */
		|| (opcode & 0xffc0) == 0x4e80 	/* JSR */
		|| (opcode & 0xffc0) == 0x4ec0 	/* JMP */
		|| (opcode & 0xff00) == 0x6100  /* BSR */
		|| ((opcode & 0xf000) == 0x6000	/* Bcc */
		    && cctrue((opcode >> 8) & 0xf)) 
		|| ((opcode & 0xf0f0) == 0x5050 /* DBcc */
		    && !cctrue((opcode >> 8) & 0xf) 
		    && (WORD)m68k_dreg(regs, opcode & 7) != 0)) 
	    {
		last_trace_ad = m68k_getpc();
		regs.spcflags &= ~SPCFLAG_TRACE;
		regs.spcflags |= SPCFLAG_DOTRACE;
	    }
	} else if (regs.t1) {
	    last_trace_ad = m68k_getpc();
	    regs.spcflags &= ~SPCFLAG_TRACE;
	    regs.spcflags |= SPCFLAG_DOTRACE;
	}
    }
}

#ifdef BASILISK
#define do_cycles() ;
#define do_disk() ;
//static void basilisk_tick_handler(void);
static int count=0;
#endif

static void m68k_run_1(void)
{
    regs.spcflags &= ~(SPCFLAG_EMULTRAP);
    for(;;) {

	/* assert (!regs.stopped && !(regs.spcflags & SPCFLAG_STOP)); */
/*	regs_backup[backup_pointer = (backup_pointer + 1) % 16] = regs;*/
#if COUNT_INSTRS
	instrcount[opcode]++;
//	count += opcode;
#endif
#if defined(X86_ASSEMBLY)
	{
	    ULONG opcode = nextiword();
	    __asm__ __volatile__("\tcall *%%ebx"
				 : : "b" (cpufunctbl[opcode]), "a" (opcode)
				 : "%eax", "%ebx", "%edx", "%ecx", 
				 "%esi", "%edi", "%ebp", "memory", "cc");
	}
#else
	{
	    ULONG opcode = nextiword();
	    (*cpufunctbl[opcode])(opcode);
	}
#endif
#ifndef NO_EXCEPTION_3
	if (buserr) {
	    Exception(3,0);
	    buserr = 0;
	}
#endif
	if (regs.spcflags & SPCFLAG_EMULTRAP)
	    return;
	/*n_insns++;*/


#if BASILISK
/*Im cheating to trigger some update
it should be timed and periodic to the VBL
60hz...*/
if(++count>500){
basilisk_tick_handler();
	count=0;
	}
#endif
	do_cycles();	
	if (regs.spcflags) {
	    /*n_spcinsns++;*/
	    while (regs.spcflags & SPCFLAG_BLTNASTY) {
		do_cycles();
		if (regs.spcflags & SPCFLAG_DISK)
		    do_disk();
	    }
	    run_compiled_code();
	    if (regs.spcflags & SPCFLAG_DOTRACE) {
		Exception(9,last_trace_ad);
	    }
	    while (regs.spcflags & SPCFLAG_STOP) {
		do_cycles();
		if (regs.spcflags & SPCFLAG_DISK)
		    do_disk();
		if (regs.spcflags & (SPCFLAG_INT | SPCFLAG_DOINT | SPCFLAG_TIMER)){
		    int intr = intlev();
		    regs.spcflags &= ~(SPCFLAG_INT | SPCFLAG_DOINT);
		    if (intr != -1 && intr > regs.intmask) {
			Interrupt(intr);
			regs.stopped = 0;
			regs.spcflags &= ~SPCFLAG_STOP;
		    }	    
		}		
	    }
	    do_trace();
#ifdef WANT_SLOW_MULTIPLY
	    /* Kludge for Hardwired demo. The guys who wrote it should be
	     * mutilated. */
	    if (regs.spcflags & SPCFLAG_EXTRA_CYCLES) {
		do_cycles (); do_cycles (); do_cycles (); do_cycles ();
		regs.spcflags &= ~SPCFLAG_EXTRA_CYCLES;
	    }
#endif
	    if (regs.spcflags & SPCFLAG_DISK)
		do_disk();
	    
	    if (regs.spcflags & (SPCFLAG_DOINT|SPCFLAG_TIMER)) {
		int intr = intlev();
		regs.spcflags &= ~SPCFLAG_DOINT;
		if (intr != -1 && intr > regs.intmask) {
		    Interrupt(intr);
		    regs.stopped = 0;
		}	    
	    }
	    if (regs.spcflags & SPCFLAG_INT) {
		regs.spcflags &= ~SPCFLAG_INT;
		regs.spcflags |= SPCFLAG_DOINT;
	    }
	    if (regs.spcflags & (SPCFLAG_BRK|SPCFLAG_MODE_CHANGE)) {
		regs.spcflags &= ~(SPCFLAG_BRK|SPCFLAG_MODE_CHANGE);
		return;
	    }
	}
    }
}

#if USER_PROGRAMS_BEHAVE > 0
static void m68k_run_2(void)
{
    regs.spcflags &= ~(SPCFLAG_EMULTRAP);
    for(;;) {

	/* assert (!regs.stopped && !(regs.spcflags & SPCFLAG_STOP)); */
/*	regs_backup[backup_pointer = (backup_pointer + 1) % 16] = regs;*/
#if COUNT_INSTRS
	instrcount[opcode]++;
#endif
#if defined(X86_ASSEMBLY)
	{
	    ULONG opcode = nextiword();
	    __asm__ __volatile__("\tcall *%%ebx"
				 : : "b" (cpufunctbl_behaved[opcode]), "a" (opcode)
				 : "%eax", "%ebx", "%edx", "%ecx", 
				 "%esi", "%edi", "%ebp", "memory", "cc");
	}
#else
	{
	    ULONG opcode = nextiword();
	    (*cpufunctbl_behaved[opcode])(opcode);
	}
#endif
#ifndef NO_EXCEPTION_3
	if (buserr) {
	    Exception(3,0);
	    buserr = 0;
	}
#endif
	if (regs.spcflags & SPCFLAG_EMULTRAP)
	    return;
	/*n_insns++;*/
	do_cycles();	
	if (regs.spcflags) {
	    /*n_spcinsns++;*/
	    while (regs.spcflags & SPCFLAG_BLTNASTY) {
		do_cycles();
		if (regs.spcflags & SPCFLAG_DISK)
		    do_disk();
	    }
	    run_compiled_code();
	    if (regs.spcflags & SPCFLAG_DOTRACE) {
		Exception(9,last_trace_ad);
	    }
	    while (regs.spcflags & SPCFLAG_STOP) {
		do_cycles();
		if (regs.spcflags & SPCFLAG_DISK)
		    do_disk();
		if (regs.spcflags & (SPCFLAG_INT | SPCFLAG_DOINT | SPCFLAG_TIMER)){
		    int intr = intlev();
		    regs.spcflags &= ~(SPCFLAG_INT | SPCFLAG_DOINT);
		    if (intr != -1 && intr > regs.intmask) {
			Interrupt(intr);
			regs.stopped = 0;
			regs.spcflags &= ~SPCFLAG_STOP;
		    }	    
		}		
	    }
	    do_trace();
#ifdef WANT_SLOW_MULTIPLY
	    /* Kludge for Hardwired demo. The guys who wrote it should be
	     * mutilated. */
	    if (regs.spcflags & SPCFLAG_EXTRA_CYCLES) {
		do_cycles (); do_cycles (); do_cycles (); do_cycles ();
		regs.spcflags &= ~SPCFLAG_EXTRA_CYCLES;
	    }
#endif
	    if (regs.spcflags & SPCFLAG_DISK)
		do_disk();
	    
	    if (regs.spcflags & (SPCFLAG_DOINT|SPCFLAG_TIMER)) {
		int intr = intlev();
		regs.spcflags &= ~SPCFLAG_DOINT;
		if (intr != -1 && intr > regs.intmask) {
		    Interrupt(intr);
		    regs.stopped = 0;
		}	    
	    }
	    if (regs.spcflags & SPCFLAG_INT) {
		regs.spcflags &= ~SPCFLAG_INT;
		regs.spcflags |= SPCFLAG_DOINT;
	    }
	    if (regs.spcflags & (SPCFLAG_BRK|SPCFLAG_MODE_CHANGE)) {
		regs.spcflags &= ~(SPCFLAG_BRK|SPCFLAG_MODE_CHANGE);
		return;
	    }
	}
    }
}
#endif

#ifdef X86_ASSEMBLY
static __inline__ void m68k_run1(void)
{
    /* Work around compiler bug: GCC doesn't push %ebp in m68k_run_1. */
    __asm__ __volatile__ ("pushl %%ebp\n\tcall *%%eax\n\tpopl %%ebp" : : "a" (m68k_run_1) : "%eax", "%edx", "%ecx", "memory", "cc");
}
#if USER_PROGRAMS_BEHAVE > 0
static __inline__ void m68k_run2(void)
{
    /* Work around compiler bug: GCC doesn't push %ebp in m68k_run_1. */
    __asm__ __volatile__ ("pushl %%ebp\n\tcall *%%eax\n\tpopl %%ebp" : : "a" (m68k_run_2) : "%eax", "%edx", "%ecx", "memory", "cc");
}
#endif
#else
#define m68k_run1 m68k_run_1
#define m68k_run2 m68k_run_2
#endif

int may_run_compiled;

void m68k_go(int may_quit)
{
    int old_mrc = may_run_compiled;
    
    may_run_compiled = may_quit;
    for(;;) {
	if (may_quit && quit_program)
	    return;
	if (debugging)
	    debug();
#if USER_PROGRAMS_BEHAVE > 0
	if ((regs.kick_mask & 0xF00000) != 0xF00000)
	    m68k_run2();
	else
#endif
	    m68k_run1();
	
	/* 
	 * We make sure in the above functions that this is never
	 * set together with SPCFLAG_INT and similar flags
	 */
	if (regs.spcflags & SPCFLAG_EMULTRAP) {
	    regs.spcflags &= ~SPCFLAG_EMULTRAP;
#ifndef BASILISK
	    if (lasttrap == 0)
		break;
	    do_emultrap(lasttrap);
#else
		break;
#endif
	}
    }
    may_run_compiled = old_mrc;
}

void m68k_disasm(CPTR addr, CPTR *nextpc, int cnt)
{
    CPTR pc = m68k_getpc();
    CPTR newpc = 0;
    m68k_setpc(addr);
    for (;cnt--;){
	char instrname[20],*ccpt;
	int opwords;
	ULONG opcode;
	struct mnemolookup *lookup;
	struct instr *dp;
	printf("%08lx: ", m68k_getpc());
	for(opwords = 0; opwords < 5; opwords++){
	    printf("%04x ", get_word(m68k_getpc() + opwords*2));
	}
	
	opcode = nextiword();
	if (cpufunctbl[opcode] == op_illg) {
	    opcode = 0x4AFC;
	}
	dp = table68k + opcode;
	for (lookup = lookuptab;lookup->mnemo != dp->mnemo; lookup++)
	    ;
	
	strcpy(instrname,lookup->name);
	ccpt = strstr(instrname,"cc");
	if (ccpt != 0) {
	    strncpy(ccpt,ccnames[dp->cc],2);
	}
	printf("%s", instrname);
	switch(dp->size){
	 case sz_byte: printf(".B "); break;
	 case sz_word: printf(".W "); break;
	 case sz_long: printf(".L "); break;
	 default: printf("   ");break;
	}

	if (dp->suse) {
	    newpc = m68k_getpc() + ShowEA(dp->sreg, dp->smode, dp->size, 0);
	}
	if (dp->suse && dp->duse)
	    printf(",");
	if (dp->duse) {
	    newpc = m68k_getpc() + ShowEA(dp->dreg, dp->dmode, dp->size, 0);
	}
	if (ccpt != 0) {
	    if (cctrue(dp->cc))
		printf(" == %08lx (TRUE)",newpc);
	    else 
		printf(" == %08lx (FALSE)",newpc);
	} else if ((opcode & 0xff00) == 0x6100) /* BSR */
	    printf(" == %08lx",newpc);
	printf("\n");
    }
    if (nextpc) *nextpc = m68k_getpc();
    m68k_setpc(pc);
}

void m68k_dumpstate(CPTR *nextpc)
{
    int i;
    for(i = 0; i < 8; i++){
	printf("D%d: %08lx ", i, m68k_dreg(regs, i));
	if ((i & 3) == 3) printf("\n");
    }
    for(i = 0; i < 8; i++){
	printf("A%d: %08lx ", i, m68k_areg(regs, i));
	if ((i & 3) == 3) printf("\n");
    }
    if (regs.s == 0) regs.usp = m68k_areg(regs, 7);
    if (regs.s && regs.m) regs.msp = m68k_areg(regs, 7);
    if (regs.s && regs.m == 0) regs.isp = m68k_areg(regs, 7);
    printf("USP=%08lx ISP=%08lx MSP=%08lx VBR=%08lx\n",
	    regs.usp,regs.isp,regs.msp,regs.vbr);
    printf ("T=%d%d S=%d M=%d X=%d N=%d Z=%d V=%d C=%d IMASK=%d\n",
	    regs.t1, regs.t0, regs.s, regs.m,
	    XFLG, NFLG, ZFLG, VFLG, CFLG, regs.intmask);
    for(i = 0; i < 8; i++){
	printf("FP%d: %g ", i, regs.fp[i]);
	if ((i & 3) == 3) printf("\n");
    }
    printf("N=%d Z=%d I=%d NAN=%d\n",
		(regs.fpsr & 0x8000000) != 0,
		(regs.fpsr & 0x4000000) != 0,
		(regs.fpsr & 0x2000000) != 0,
		(regs.fpsr & 0x1000000) != 0);
    m68k_disasm(m68k_getpc(), nextpc, 1);
    if (nextpc) printf("next PC: %08lx\n", *nextpc);
}
