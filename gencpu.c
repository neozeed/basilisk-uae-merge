/* 
 * UAE - The Un*x Amiga Emulator
 *
 * MC68000 emulation generator
 *
 * This is a fairly stupid program that generates a lot of case labels that 
 * can be #included in a switch statement.
 * As an alternative, it can generate functions that handle specific
 * MC68000 instructions, plus a prototype header file and a function pointer
 * array to look up the function for an opcode.
 * Error checking is bad, an illegal table68k file will cause the program to
 * call abort().
 * The generated code is sometimes sub-optimal, an optimizing compiler should 
 * take care of this.
 * 
 * Copyright 1995, 1996 Bernd Schmidt
 */

#include "sysconfig.h"
#include "sysdeps.h"
#include <ctype.h>

#include "config.h"
#include "options.h"
#include "readcpu.h"

#define BOOL_TYPE "int"

static long int counts[65536];

static int isspecific(int opcode)
{
    return counts[opcode]>5;
}

static void read_counts(void)
{
    FILE *file;
    unsigned long opcode,count, total;
    int trapcount=0;
    int trap=0;
    char name[20];
    memset(counts, 0, sizeof counts);

    file=fopen("insncount","r");
    if(file)
    {
	fscanf(file,"Total: %lu\n",&total);
	while(fscanf(file,"%lx: %lu %s\n",&opcode,&count,name)==3)
	{
	    counts[opcode]=10000.0*count/total;
	    if(isspecific(opcode))
	    {
		trapcount+=count;
		trap++;
	    }
	}
	fclose(file);
#if 0
	fprintf(stderr,"trap %d function: %f%\n",trap,100.0*trapcount/total);
#endif
    }
}


static int n_braces = 0;

static void start_brace(void)
{
    n_braces++;
    printf("{");
}

static void close_brace(void)
{
    assert (n_braces > 0);
    n_braces--;
    printf("}");
}

static void finish_braces(void)
{
    while (n_braces > 0)
	close_brace();
}

static void pop_braces(int to)
{
    while (n_braces > to)
	close_brace();
}

static int bit_size(int size)
{
    switch(size) {
     case sz_byte:	return 8;
     case sz_word:	return 16;
     case sz_long:	return 32;
     default:
	abort();
    }
    return 0;
}

static const char *bit_mask(int size)
{
    switch(size) {
     case sz_byte:	return "0xff";
     case sz_word:	return "0xffff";
     case sz_long:	return "0xffffffff";
     default:
	abort();
    }
    return 0;
}

static void genamode(amodes mode, char *reg, wordsizes size, char *name, int getv, int movem)
{
    start_brace ();
    switch(mode) {
     case Dreg:
	if (movem)
	    abort();
	if (getv)
	    switch(size) {	  
	     case sz_byte:
#ifdef AMIGA
		/* sam: I don't know why gcc.2.7.2.1 produces a code worse */
		/* if it is not done like that: */
		printf("\tBYTE %s = ((UBYTE*)&m68k_dreg(regs, %s))[3];\n", name, reg);
#else
		printf("\tBYTE %s = m68k_dreg(regs, %s);\n", name, reg);
#endif
		break;
	     case sz_word:
#ifdef AMIGA
		printf("\tWORD %s = ((WORD*)&m68k_dreg(regs, %s))[1];\n", name, reg);
#else
		printf("\tWORD %s = m68k_dreg(regs, %s);\n", name, reg);
#endif
		break;
	     case sz_long:
		printf("\tLONG %s = m68k_dreg(regs, %s);\n", name, reg);
		break;
	     default: abort();
	    }
	break;
     case Areg:
	if (movem)
	    abort();
	if (getv)
	    switch(size) {	  
	     case sz_word:
		printf("\tWORD %s = m68k_areg(regs, %s);\n", name, reg);
		break;
	     case sz_long:
		printf("\tLONG %s = m68k_areg(regs, %s);\n", name, reg);
		break;
	     default: abort();
	    }
	break;
     case Aind:
	printf("\tCPTR %sa = m68k_areg(regs, %s);\n", name, reg);
	if (getv)
	    switch(size) {	  
	     case sz_byte:
		printf("\tBYTE %s = get_byte(%sa);\n", name, name);
		break;
	     case sz_word:
		printf("\tWORD %s = get_word(%sa);\n", name, name);
		break;
	     case sz_long:
		printf("\tLONG %s = get_long(%sa);\n", name, name);
		break;
	     default: abort();
	    }
	break;
     case Aipi:
	printf("\tCPTR %sa = m68k_areg(regs, %s);\n", name, reg);
	switch(size) {
	 case sz_byte:	    
	    if (getv) printf("\tBYTE %s = get_byte(%sa);\n", name, name);
	    if (!movem) {
		start_brace();
		printf("\tm68k_areg(regs, %s) += areg_byteinc[%s];\n", reg, reg);
	    }
	    break;
	 case sz_word:
	    if (getv) printf("\tWORD %s = get_word(%sa);\n", name, name);
	    if (!movem) {
		start_brace();
		printf("\tm68k_areg(regs, %s) += 2;\n", reg);
	    }
	    break;
	 case sz_long:
	    if (getv) printf("\tLONG %s = get_long(%sa);\n", name, name);
	    if (!movem) {
		start_brace();
		printf("\tm68k_areg(regs, %s) += 4;\n", reg);
	    }
	    break;
	 default: abort();
	}
	break;
     case Apdi:
	switch(size) {	  
	 case sz_byte:
	    if (!movem) printf("\tm68k_areg(regs, %s) -= areg_byteinc[%s];\n", reg, reg);
	    start_brace();
	    printf("\tCPTR %sa = m68k_areg(regs, %s);\n", name, reg);
	    if (getv) printf("\tBYTE %s = get_byte(%sa);\n", name, name);
	    break;
	 case sz_word:
	    if (!movem) printf("\tm68k_areg(regs, %s) -= 2;\n", reg);
	    start_brace();
	    printf("\tCPTR %sa = m68k_areg(regs, %s);\n", name, reg);
	    if (getv) printf("\tWORD %s = get_word(%sa);\n", name, name);
	    break;
	 case sz_long:
	    if (!movem) printf("\tm68k_areg(regs, %s) -= 4;\n", reg);
	    start_brace();
	    printf("\tCPTR %sa = m68k_areg(regs, %s);\n", name, reg);
	    if (getv) printf("\tLONG %s = get_long(%sa);\n", name, name);
	    break;
	 default: abort();
	}
	break;
     case Ad16:
	printf("\tCPTR %sa = m68k_areg(regs, %s) + (LONG)(WORD)nextiword();\n", name, reg);
	if (getv) 
	    switch(size) {	  
	     case sz_byte:
		printf("\tBYTE %s = get_byte(%sa);\n", name, name);
		break;
	     case sz_word:
		printf("\tWORD %s = get_word(%sa);\n", name, name);
		break;
	     case sz_long:
		printf("\tLONG %s = get_long(%sa);\n", name, name);
		break;
	     default: abort();
	    }
	break;
     case Ad8r:
	printf("\tCPTR %sa = get_disp_ea(m68k_areg(regs, %s));\n", name, reg);
	if (getv) {
	    start_brace();
	    switch(size) {	  
	     case sz_byte:
		printf("\tBYTE %s = get_byte(%sa);\n", name, name);
		break;
	     case sz_word:
		printf("\tWORD %s = get_word(%sa);\n", name, name);
		break;
	     case sz_long:
		printf("\tLONG %s = get_long(%sa);\n", name, name);
		break;
	     default: abort();
	    }
	}
	break;
     case PC16:
	printf("\tCPTR %sa = m68k_getpc();\n", name);
	printf("\t%sa += (LONG)(WORD)nextiword();\n", name);
	if (getv) {
	    start_brace();
	    switch(size) {	  
	     case sz_byte:
		printf("\tBYTE %s = get_byte(%sa);\n", name, name);
		break;
	     case sz_word:
		printf("\tWORD %s = get_word(%sa);\n", name, name);
		break;
	     case sz_long:
		printf("\tLONG %s = get_long(%sa);\n", name, name);
		break;
	     default: abort();
	    }
	}
	break;
     case PC8r:
	printf("\tCPTR %sa = get_disp_ea(m68k_getpc());\n",name);
	if (getv) {
	    start_brace();
	    switch(size) {	  
	     case sz_byte:
		printf("\tBYTE %s = get_byte(%sa);\n", name, name);
		break;
	     case sz_word:
		printf("\tWORD %s = get_word(%sa);\n", name, name);
		break;
	     case sz_long:
		printf("\tLONG %s = get_long(%sa);\n", name, name);
		break;
	     default: abort();
	    }
	}
	break;
     case absw:
	printf("\tCPTR %sa = (LONG)(WORD)nextiword();\n", name);
	if (getv) 
	    switch(size) {	  
	     case sz_byte:
		printf("\tBYTE %s = get_byte(%sa);\n", name, name);
		break;
	     case sz_word:
		printf("\tWORD %s = get_word(%sa);\n", name, name);
		break;
	     case sz_long:
		printf("\tLONG %s = get_long(%sa);\n", name, name);
		break;
	     default: abort();
	    }
	break;
     case absl:
	printf("\tCPTR %sa = nextilong();\n", name);
	if (getv) 
	    switch(size) {	  
	     case sz_byte:
		printf("\tBYTE %s = get_byte(%sa);\n", name, name);
		break;
	     case sz_word:
		printf("\tWORD %s = get_word(%sa);\n", name, name);
		break;
	     case sz_long:
		printf("\tLONG %s = get_long(%sa);\n", name, name);
		break;
	     default: abort();
	    }
	break;
     case imm:
	if (getv) 
	    switch(size) {
	     case sz_byte:
		printf("\tBYTE %s = nextibyte();\n", name);
		break;
	     case sz_word:
		printf("\tWORD %s = nextiword();\n", name);
		break;
	     case sz_long:
		printf("\tLONG %s = nextilong();\n", name);
		break;
	     default: abort();
	    }
	break;
     case imm0:
	if (!getv) abort();
	printf("\tBYTE %s = nextibyte();\n", name);
	break;
     case imm1:
	if (!getv) abort();
	printf("\tWORD %s = nextiword();\n", name);
	break;
     case imm2:
	if (!getv) abort();
        printf("\tLONG %s = nextilong();\n", name);
	break;
     case immi:
	if (!getv) abort();
	printf("\tULONG %s = %s;\n", name, reg);
	break;
     default: 
	abort();
    }
}

static void genastore(char *from, amodes mode, char *reg, wordsizes size, char *to)
{
    switch(mode) {
     case Dreg:
	switch(size) {	  
	 case sz_byte:
	    printf("\tm68k_dreg(regs, %s) = (m68k_dreg(regs, %s) & ~0xff) | ((%s) & 0xff);\n", reg, reg, from);
	    break;
	 case sz_word:
	    printf("\tm68k_dreg(regs, %s) = (m68k_dreg(regs, %s) & ~0xffff) | ((%s) & 0xffff);\n", reg, reg, from);
	    break;
	 case sz_long:
	    printf("\tm68k_dreg(regs, %s) = (%s);\n", reg, from);
	    break;
	 default: abort();
	}
	break;
     case Areg:
	switch(size) {	  
	 case sz_word:
	    fprintf(stderr, "Foo\n");
	    printf("\tm68k_areg(regs, %s) = (LONG)(WORD)(%s);\n", reg, from);
	    break;
	 case sz_long:
	    printf("\tm68k_areg(regs, %s) = (%s);\n", reg, from);
	    break;
	 default: abort();
	}
	break;
     case Aind:
     case Aipi:
     case Apdi:
     case Ad16:
     case Ad8r:
     case absw:
     case absl:
	switch(size) {
	 case sz_byte:
	    printf("\tput_byte(%sa,%s);\n", to, from);
	    break;
	 case sz_word:
	    printf("\tput_word(%sa,%s);\n", to, from);
	    break;
	 case sz_long:
	    printf("\tput_long(%sa,%s);\n", to, from);
	    break;
	 default: abort();
	}
	break;
     case PC16:
     case PC8r:
	switch(size) {
	 case sz_byte:
	    printf("\tput_byte(%sa,%s);\n", to, from);
	    break;
	 case sz_word:
	    if (CPU_LEVEL < 2)
		abort();
	    printf("\tput_word(%sa,%s);\n", to, from);
	    break;
	 case sz_long:
	    if (CPU_LEVEL < 2)
		abort();
	    printf("\tput_long(%sa,%s);\n", to, from);
	    break;
	 default: abort();
	}
	break;
     case imm:
     case imm0:
     case imm1:
     case imm2:
     case immi:
	abort();
	break;
     default: 
	abort();
    }
}

static void genmovemel(UWORD opcode)
{
    char getcode[100];
    int size = table68k[opcode].size == sz_long ? 4 : 2;
    
    if (table68k[opcode].size == sz_long) {	
    	strcpy(getcode, "get_long(srca)");
    } else {	    
    	strcpy(getcode, "(LONG)(WORD)get_word(srca)");
    }
    
    printf("\tUWORD mask = nextiword(), dmask = mask & 0xff, amask = (mask >> 8) & 0xff;\n");
    genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "src", 0, 1);
    start_brace();
    printf("\twhile (dmask) { m68k_dreg(regs, movem_index1[dmask]) = %s; srca += %d; dmask = movem_next[dmask]; }\n",
		getcode, size);
    printf("\twhile (amask) { m68k_areg(regs, movem_index1[amask]) = %s; srca += %d; amask = movem_next[amask]; }\n",
		getcode, size);

    if (table68k[opcode].dmode == Aipi)
    	printf("\tm68k_areg(regs, dstreg) = srca;\n");
}

static void genmovemle(UWORD opcode)
{
    char putcode[100];
    int size = table68k[opcode].size == sz_long ? 4 : 2;
    if (table68k[opcode].size == sz_long) {
    	strcpy(putcode, "put_long(srca,");
    } else {	    
    	strcpy(putcode, "put_word(srca,");
    }
    
    printf("\tUWORD mask = nextiword();\n");
    genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "src", 0, 1);
    start_brace();
    if (table68k[opcode].dmode == Apdi) {
	printf("\tUWORD amask = mask & 0xff, dmask = (mask >> 8) & 0xff;\n");
        printf("\twhile (amask) { srca -= %d; %s m68k_areg(regs, movem_index2[amask])); amask = movem_next[amask]; }\n",
	   size, putcode);    
        printf("\twhile (dmask) { srca -= %d; %s m68k_dreg(regs, movem_index2[dmask])); dmask = movem_next[dmask]; }\n",
	   size, putcode);    
	printf("\tm68k_areg(regs, dstreg) = srca;\n");
    } else {
	printf("\tUWORD dmask = mask & 0xff, amask = (mask >> 8) & 0xff;\n");
        printf("\twhile (dmask) { %s m68k_dreg(regs, movem_index1[dmask])); srca += %d; dmask = movem_next[dmask]; }\n",
	   putcode, size);    
        printf("\twhile (amask) { %s m68k_areg(regs, movem_index1[amask])); srca += %d; amask = movem_next[amask]; }\n",
	   putcode, size);    
    }
}

typedef enum {
    flag_logical, flag_add, flag_sub, flag_cmp, flag_addx, flag_subx, flag_zn,
    flag_av, flag_sv
} flagtypes;

static void genflags_normal(flagtypes type, wordsizes size, char *value, char *src, char *dst)
{
    char vstr[100],sstr[100],dstr[100];
    char usstr[100],udstr[100];
    char unsstr[100],undstr[100];

    switch(size) {	    
     case sz_byte:
	strcpy(vstr, "((BYTE)(");
	strcpy(usstr, "((UBYTE)(");
	break;
     case sz_word:
	strcpy(vstr, "((WORD)(");
	strcpy(usstr, "((UWORD)(");
	break;
     case sz_long:
	strcpy(vstr, "((LONG)(");
	strcpy(usstr, "((ULONG)(");
	break;
     default:
	abort();
    }
    strcpy(unsstr, usstr); 

    strcpy(sstr, vstr);
    strcpy(dstr, vstr);
    strcat(vstr, value); strcat(vstr,"))");
    strcat(dstr, dst); strcat(dstr,"))");
    strcat(sstr, src); strcat(sstr,"))");
    
    strcpy(udstr, usstr);
    strcat(udstr, dst); strcat(udstr,"))");
    strcat(usstr, src); strcat(usstr,"))");
    
    strcpy(undstr, unsstr);
    strcat(unsstr, "-");
    strcat(undstr, "~");
    strcat(undstr, dst); strcat(undstr,"))");
    strcat(unsstr, src); strcat(unsstr,"))");

    switch (type) {
     case flag_logical:
     case flag_zn:
     case flag_av:
     case flag_sv:
     case flag_addx:
     case flag_subx:
	break;
    
     case flag_add:
	start_brace();
	printf("ULONG %s = %s + %s;\n", value, dstr, sstr);
	break;
     case flag_sub:
     case flag_cmp:
	start_brace();
	printf("ULONG %s = %s - %s;\n", value, dstr, sstr);
	break;
    }


    switch (type) {
     case flag_logical:
     case flag_zn:
	break;
	
     case flag_add:
     case flag_sub:
     case flag_addx:
     case flag_subx:
     case flag_cmp:
     case flag_av:
     case flag_sv:
	start_brace();
	printf("\t"BOOL_TYPE" flgs = %s < 0;\n", sstr);
	printf("\t"BOOL_TYPE" flgo = %s < 0;\n", dstr);
	printf("\t"BOOL_TYPE" flgn = %s < 0;\n", vstr);
	break;
    }
    
    switch(type) {
     case flag_logical:
	printf("\tVFLG = CFLG = 0;\n");
	printf("\tZFLG = %s == 0;\n", vstr);
	printf("\tNFLG = %s < 0;\n", vstr);
	break;
     case flag_av:
	printf("\tVFLG = (flgs == flgo) && (flgn != flgo);\n");
	break;
     case flag_sv:
	printf("\tVFLG = (flgs != flgo) && (flgn != flgo);\n");
	break;
     case flag_zn:
	printf("\tif (%s != 0) ZFLG = 0;\n", vstr);
	printf("\tNFLG = %s < 0;\n", vstr);
	break;
     case flag_add:
	printf("\tZFLG = %s == 0;\n", vstr);
	printf("\tVFLG = (flgs == flgo) && (flgn != flgo);\n");
	printf("\tCFLG = XFLG = %s < %s;\n", undstr, usstr);
	printf("\tNFLG = flgn != 0;\n");
	break;
     case flag_sub:
	printf("\tZFLG = %s == 0;\n", vstr);
	printf("\tVFLG = (flgs != flgo) && (flgn != flgo);\n");
	printf("\tCFLG = XFLG = %s > %s;\n", usstr, udstr);
	printf("\tNFLG = flgn != 0;\n");
	break;
     case flag_addx:
	printf("\tVFLG = (flgs && flgo && !flgn) || (!flgs && !flgo && flgn);\n");
	printf("\tXFLG = CFLG = (flgs && flgo) || (!flgn && (flgo || flgs));\n");
	break;
     case flag_subx:
	printf("\tVFLG = (!flgs && flgo && !flgn) || (flgs && !flgo && flgn);\n");
	printf("\tXFLG = CFLG = (flgs && !flgo) || (flgn && (!flgo || flgs));\n");
	break;
     case flag_cmp:
	printf("\tZFLG = %s == 0;\n", vstr);
	printf("\tVFLG = (flgs != flgo) && (flgn != flgo);\n");
	printf("\tCFLG = %s > %s;\n", usstr, udstr);
	printf("\tNFLG = flgn != 0;\n");
	break;
    }
}

static void genflags(flagtypes type, wordsizes size, char *value, char *src, char *dst)
{
#ifdef INTEL_FLAG_OPT
    start_brace();
    printf("\tULONG scratch = 0;\n");
    switch (type) {
     case flag_logical:
     case flag_av:
     case flag_sv:
     case flag_zn:
     case flag_addx:
     case flag_subx:
     case flag_cmp:
	break;

     case flag_add:
     case flag_sub:
	start_brace();
	printf("\tULONG %s;\n", value);
	break;
    }

    switch(type) {
     case flag_av:
     case flag_sv:
     case flag_zn:
     case flag_addx:
     case flag_subx:
	break;

     case flag_logical:
	if (strcmp(value, "0") == 0) {
	    printf("\t*(ULONG *)&regflags = 64;\n");
	} else {
	    switch(size) {
	     case sz_byte:
		printf("\t__asm__ __volatile__(\"testb %%b1,%%b1\\n\\tpushfl\\n\\tpopl %%0\\n\\tmovl %%0,regflags\\n\""
		       ": \"=r\" (scratch) : \"q\" (%s) :\"cc\");", value);
		break;
	     case sz_word:
		printf("\t__asm__ __volatile__(\"testw %%w1,%%w1\\n\\tpushfl\\n\\tpopl %%0\\n\\tmovl %%0,regflags\\n\""
		       ": \"=r\" (scratch) : \"r\" (%s) : \"cc\");", value);
		break;
	     case sz_long:
		printf("\t__asm__ __volatile__(\"testl %%1,%%1\\n\\tpushfl\\n\\tpopl %%0\\n\\tmovl %%0,regflags\\n\""
		       ": \"=r\" (scratch) : \"r\" (%s) : \"cc\");", value);
		
		break;
	    }
	}
	return;

     case flag_add:
	switch (size) {
	 case sz_byte:
	    printf("\t__asm__ __volatile__(\"addb %%b3,%%b0\\n\\tpushfl\\n\\tpopl %%1\\n\\tmovl %%1,regflags\\n\\tmovl %%1,regflags+4\\n\""
		   ": \"=&q\" (%s) : \"r\" (scratch), "
		   " \"0\" ((BYTE)(%s)), \"qm\" ((BYTE)(%s)) : \"cc\");",
		   value, src, dst);
	    break;
	 case sz_word:
	    printf("\t__asm__ __volatile__(\"addw %%w3,%%w0\\n\\tpushfl\\n\\tpopl %%1\\n\\tmovl %%1,regflags\\n\\tmovl %%1,regflags+4\\n\""
		   ": \"=&r\" (%s) : \"r\" (scratch), "
		   " \"0\" ((WORD)(%s)), \"rmi\" ((WORD)(%s)) : \"cc\");",
		   value, src, dst);
	    break;
	 case sz_long:
	    printf("\t__asm__ __volatile__(\"addl %%3,%%0\\n\\tpushfl\\n\\tpopl %%1\\n\\tmovl %%1,regflags\\n\\tmovl %%1,regflags+4\\n\""
		   ": \"=&r\" (%s) : \"r\" (scratch), "
		   " \"0\" ((LONG)(%s)), \"rmi\" ((LONG)(%s)) : \"cc\");",
		   value, src, dst);
	    break;
	}
	return;

     case flag_sub:
	switch (size) {
	 case sz_byte:
	    printf("\t__asm__ __volatile__(\"subb %%b2,%%b0\\n\\tpushfl\\n\\tpopl %%1\\n\\tmovl %%1,regflags\\n\\tmovl %%1,regflags+4\\n\""
		   ": \"=&q\" (%s) : \"r\" (scratch), "
		   " \"qmi\" ((BYTE)(%s)), \"0\" ((BYTE)(%s)) : \"cc\");",
		   value, src, dst);
	    break;
	 case sz_word:
	    printf("\t__asm__ __volatile__(\"subw %%w2,%%w0\\n\\tpushfl\\n\\tpopl %%1\\n\\tmovl %%1,regflags\\n\\tmovl %%1,regflags+4\\n\""
		   ": \"=&r\" (%s) : \"r\" (scratch), "
		   " \"rmi\" ((WORD)(%s)), \"0\" ((WORD)(%s)) : \"cc\");",
		   value, src, dst);
	    break;
	 case sz_long:
	    printf("\t__asm__ __volatile__(\"subl %%2,%%0\\n\\tpushfl\\n\\tpopl %%1\\n\\tmovl %%1,regflags\\n\\tmovl %%1,regflags+4\\n\""
		   ": \"=&r\" (%s) : \"r\" (scratch), "
		   " \"rmi\" ((LONG)(%s)), \"0\" ((LONG)(%s)) : \"cc\");",
		   value, src, dst);
	    break;
	}
	return;

     case flag_cmp:
	switch (size) {
	 case sz_byte:
	    printf("\t__asm__ __volatile__(\"cmpb %%b1,%%b2\\n\\tpushfl\\n\\tpopl %%0\\n\\tmovl %%0,regflags\\n\""
		   ": : \"r\" (scratch), "
		   " \"qmi\" ((BYTE)(%s)), \"q\" ((BYTE)(%s)) : \"cc\");",
		   src, dst);
	    break;
	 case sz_word:
	    printf("\t__asm__ __volatile__(\"cmpw %%w1,%%w2\\n\\tpushfl\\n\\tpopl %%0\\n\\tmovl %%0,regflags\\n\""
		   ": : \"r\" (scratch), "
		   " \"rmi\" ((WORD)(%s)), \"r\" ((WORD)(%s)) : \"cc\");",
		   src, dst);
	    break;
	 case sz_long:
	    printf("\t__asm__ __volatile__(\"cmpl %%1,%%2\\n\\tpushfl\\n\\tpopl %%0\\n\\tmovl %%0,regflags\\n\""
		   ": : \"r\" (scratch), "
		   " \"rmi\" ((LONG)(%s)), \"r\" ((LONG)(%s)) : \"cc\");",
		   src, dst);
	    break;
	}
	return;
    }
#elif defined(M68K_FLAG_OPT)
/*
 * sam: This is different from 0.6.3. Might be buggy!
 */
    switch (type) {
     case flag_logical:
     case flag_av:
     case flag_sv:
     case flag_zn:
     case flag_addx:
     case flag_subx:
	break;

     case flag_add:
     case flag_sub:
     case flag_cmp:
	start_brace();
	printf("\tULONG %s;\n", value);
	break;
    }

    switch(type) {
     case flag_av:
     case flag_sv:
     case flag_zn:
     case flag_addx:
     case flag_subx:
        /* normal code: genflags_normal() */
	break;

     case flag_logical:
	if (strcmp(value, "0") == 0) {
	    /* v=c=n=0 z=1 */
	    printf("\t*(UWORD*)&regflags = 4;\n");
	} else {
            printf("\t{\n");
	    switch(size) {
	     case sz_byte:
		printf("\t__asm__(\"tstb %%0\\n\\tmovw ccr,_regflags\""
		       ": " 
                       ": \"dmi\" ((BYTE)(%s)) : \"cc\");\n",value);
		break;
	     case sz_word:
		printf("\t__asm__(\"tstw %%0\\n\\tmovw ccr,_regflags\""
		       ": " 
                       ": \"dmi\" ((WORD)(%s)) : \"cc\");\n",value);
		break;
	     case sz_long:
		printf("\t__asm__(\"tstl %%0\\n\\tmovw ccr,_regflags\""
		       ": " 
                       ": \"dmi\" ((LONG)(%s)) : \"cc\");\n",value);
		break;
	    }
	    printf("\t}\n");
	}
	return;

     case flag_add:
        printf("\t{UWORD ccr;\n");
	switch (size) {
	 case sz_byte:
	 	printf("\t__asm__(\"addb %%3,%%1\\n\\tmovw ccr,%%0\""
		       ": \"=d\" (ccr), \"=&d\" ((BYTE)(%s))" 
		       ": \"1\" ((BYTE)(%s)), \"dmi\" ((BYTE)(%s)) : \"cc\");\n",
		       value, src, dst);
	    break;
	 case sz_word:
	 	printf("\t__asm__(\"addw %%3,%%1\\n\\tmovw ccr,%%0\""
		       ": \"=d\" (ccr), \"=&d\" ((WORD)(%s))" 
		       ": \"1\" ((WORD)(%s)), \"dmi\" ((WORD)(%s)) : \"cc\");\n",
		       value, src, dst);
	    break;
	 case sz_long:
	 	printf("\t__asm__(\"addl %%3,%%1\\n\\tmovw ccr,%%0\""
		       ": \"=d\" (ccr), \"=&d\" ((LONG)(%s))" 
		       ": \"1\" ((LONG)(%s)), \"dmi\" ((LONG)(%s)) : \"cc\");\n",
		       value, src, dst);
	    break;
	}
        printf("\t((UWORD*)&regflags)[1] = ((UWORD*)&regflags)[0] = ccr;}\n");
	return;

     case flag_sub:
        printf("\t{UWORD ccr;\n");
	switch (size) {
	 case sz_byte:
	    if(strcmp(dst,"0")!=0)
	 	printf("\t__asm__(\"subb %%2,%%1\\n\\tmovw ccr,%%0\""
		       ": \"=d\" (ccr), \"=&d\" ((BYTE)(%s)) "
                       ": \"dmi\" ((BYTE)(%s)), \"1\" ((BYTE)(%s)) : \"cc\");\n",
		       value, src, dst);
	    else
	 	printf("\t__asm__(\"negb %%1\\n\\tmovw ccr,%%0\""
		       ": \"=d\" (ccr), \"=&d\" ((BYTE)(%s)) "
                       ": \"1\" ((BYTE)(%s)) : \"cc\");\n",
		       value, src);
	    break;
	 case sz_word:
	    if(strcmp(dst,"0")!=0)
	 	printf("\t__asm__(\"subw %%2,%%1\\n\\tmovw ccr,%%0\""
		       ": \"=d\" (ccr), \"=&d\" ((WORD)(%s)) "
                       ": \"dmi\" ((WORD)(%s)), \"1\" ((WORD)(%s)) : \"cc\");\n",
		       value, src, dst);
	    else
	 	printf("\t__asm__(\"negw %%1\\n\\tmovw ccr,%%0\""
		       ": \"=d\" (ccr), \"=&d\" ((WORD)(%s)) "
                       ": \"1\" ((WORD)(%s)) : \"cc\");\n",
		       value, src);
	    break;
	 case sz_long:
	    if(strcmp(dst,"0")!=0)
	 	printf("\t__asm__(\"subl %%2,%%1\\n\\tmovw ccr,%%0\""
		       ": \"=d\" (ccr), \"=&d\" ((LONG)(%s)) "
                       ": \"dmi\" ((LONG)(%s)), \"1\" ((LONG)(%s)) : \"cc\");\n",
		       value, src, dst);
	    else
	 	printf("\t__asm__(\"negl %%1\\n\\tmovw ccr,%%0\""
		       ": \"=d\" (ccr), \"=&d\" ((LONG)(%s)) "
                       ": \"1\" ((LONG)(%s)) : \"cc\");\n",
		       value, src);
	    break;
	}
        printf("\t((UWORD*)&regflags)[1] = ((UWORD*)&regflags)[0] = ccr;}\n");
	return;

     case flag_cmp:
        printf("\t{\n");
	switch (size) {
	 case sz_byte:
	    printf("\t__asm__(\"cmpb %%0,%%1\\n\\tmovw ccr,_regflags\""
		   ": "
                   ": \"dmi\" ((BYTE)(%s)), \"d\" ((BYTE)(%s)) : \"cc\");\n",
		   src, dst);
	    break;
	 case sz_word:
	    printf("\t__asm__(\"cmpw %%0,%%1\\n\\tmovw ccr,_regflags\""
		   ": "
                   ": \"dmi\" ((WORD)(%s)), \"d\" ((WORD)(%s)) : \"cc\");\n",
		   src, dst);
	    break;
	 case sz_long:
	    printf("\t__asm__(\"cmpl %%0,%%1\\n\\tmovw ccr,_regflags\""
		   ": "
                   ": \"dmi\" ((LONG)(%s)), \"d\" ((LONG)(%s)) : \"cc\");\n",
		   src, dst);
	    break;
	}
        printf("\t}\n");
	return;
    }
#endif
    genflags_normal(type, size, value, src, dst);
}
static void gen_opcode(unsigned long int opcode) 
{
    start_brace ();
    switch (table68k[opcode].plev) {
     case 0: /* not priviledged */
	break;
     case 1: /* unpriviledged only on 68000 */
	if (CPU_LEVEL == 0)
	    break;
	/* FALLTHROUGH */
     case 2: /* priviledged */
#ifdef USE_POINTER
	printf("if (!regs.s) { regs.pc_p -= 2; Exception(8,0); } else\n");
#else
	printf("if (!regs.s) { regs.pc -= 2; Exception(8,0); } else\n");
#endif
	start_brace();
	break;
     case 3: /* priviledged if size == word */
	if (table68k[opcode].size == sz_byte)
	    break;
#ifdef USE_POINTER
	printf("if (!regs.s) { regs.pc_p -= 2; Exception(8,0); } else\n");
#else
	printf("if (!regs.s) { regs.pc -= 2; Exception(8,0); } else\n");
#endif
	start_brace();
	break;
    }
    switch(table68k[opcode].mnemo) {
     case i_OR:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst", 1, 0);
	printf("\tsrc |= dst;\n");
	genflags(flag_logical, table68k[opcode].size, "src", "", "");
	genastore("src", table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst");
	break;
     case i_AND:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst", 1, 0);
	printf("\tsrc &= dst;\n");
	genflags(flag_logical, table68k[opcode].size, "src", "", "");
	genastore("src", table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst");
	break;
     case i_EOR:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst", 1, 0);
	printf("\tsrc ^= dst;\n");
	genflags(flag_logical, table68k[opcode].size, "src", "", "");
	genastore("src", table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst");
	break;
     case i_ORSR:
	printf("\tMakeSR();\n");
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	if (table68k[opcode].size == sz_byte) {
	    printf("\tsrc &= 0xFF;\n");
	}
	printf("\tregs.sr |= src;\n");
	printf("\tMakeFromSR();\n");
	break;
     case i_ANDSR: 	
	printf("\tMakeSR();\n");
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	if (table68k[opcode].size == sz_byte) {
	    printf("\tsrc |= 0xFF00;\n");
	}
	printf("\tregs.sr &= src;\n");
	printf("\tMakeFromSR();\n");
	break;
     case i_EORSR:
	printf("\tMakeSR();\n");
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	if (table68k[opcode].size == sz_byte) {
	    printf("\tsrc &= 0xFF;\n");
	}
	printf("\tregs.sr ^= src;\n");
	printf("\tMakeFromSR();\n");
	break;
     case i_SUB: 
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst", 1, 0);
	start_brace ();
	genflags(flag_sub, table68k[opcode].size, "newv", "src", "dst");
	genastore("newv", table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst");
	break;
     case i_SUBA:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", sz_long, "dst", 1, 0);
	start_brace ();
	printf("\tULONG newv = dst - src;\n");
	genastore("newv", table68k[opcode].dmode, "dstreg", sz_long, "dst");
	break;
     case i_SUBX:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst", 1, 0);
	start_brace ();
	printf("\tULONG newv = dst - src - (XFLG ? 1 : 0);\n");
	genflags(flag_subx, table68k[opcode].size, "newv", "src", "dst");
	genflags(flag_zn, table68k[opcode].size, "newv", "", "");
	genastore("newv", table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst");
	break;
     case i_SBCD:
	/* Let's hope this works... */
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst", 1, 0);
	start_brace ();
	printf("\tUWORD newv_lo = (dst & 0xF) - (src & 0xF) - (XFLG ? 1 : 0);\n");
	printf("\tUWORD newv_hi = (dst & 0xF0) - (src & 0xF0);\n");
	printf("\tUWORD newv;\n");
	printf("\tif (newv_lo > 9) { newv_lo-=6; newv_hi-=0x10; }\n");
	printf("\tnewv = newv_hi + (newv_lo & 0xF);");
	printf("\tCFLG = XFLG = (newv_hi & 0x1F0) > 0x90;\n");
	printf("\tif (CFLG) newv -= 0x60;\n");
	genflags(flag_zn, table68k[opcode].size, "newv", "", "");	
	genflags(flag_sv, table68k[opcode].size, "newv", "src", "dst");		
	genastore("newv", table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst");
	break;
     case i_ADD:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst", 1, 0);
	start_brace ();
	genflags(flag_add, table68k[opcode].size, "newv", "src", "dst");
	genastore("newv", table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst");
	break;
     case i_ADDA: 
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", sz_long, "dst", 1, 0);
	start_brace ();
	printf("\tULONG newv = dst + src;\n");
	genastore("newv", table68k[opcode].dmode, "dstreg", sz_long, "dst");
	break;
     case i_ADDX:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst", 1, 0);
	start_brace ();
	printf("\tULONG newv = dst + src + (XFLG ? 1 : 0);\n");
	genflags(flag_addx, table68k[opcode].size, "newv", "src", "dst");
	genflags(flag_zn, table68k[opcode].size, "newv", "", "");
	genastore("newv", table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst");
	break;
     case i_ABCD:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst", 1, 0);
	start_brace ();
	printf("\tUWORD newv_lo = (src & 0xF) + (dst & 0xF) + (XFLG ? 1 : 0);\n");
	printf("\tUWORD newv_hi = (src & 0xF0) + (dst & 0xF0);\n");
	printf("\tUWORD newv;\n");
	printf("\tif (newv_lo > 9) { newv_lo +=6; }\n");
	printf("\tnewv = newv_hi + newv_lo;");
	printf("\tCFLG = XFLG = (newv & 0x1F0) > 0x90;\n");
	printf("\tif (CFLG) newv += 0x60;\n");
	genflags(flag_zn, table68k[opcode].size, "newv", "", "");
	genflags(flag_sv, table68k[opcode].size, "newv", "src", "dst");	
	genastore("newv", table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst");
	break;
     case i_NEG:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	start_brace ();
	genflags(flag_sub, table68k[opcode].size, "dst", "src", "0");
	genastore("dst",table68k[opcode].smode, "srcreg", table68k[opcode].size, "src");
	break;
     case i_NEGX:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	start_brace ();
	printf("\tULONG newv = 0 - src - (XFLG ? 1 : 0);\n");
	genflags(flag_subx, table68k[opcode].size, "newv", "src", "0");
	genflags(flag_zn, table68k[opcode].size, "newv", "", "");
	genastore("newv", table68k[opcode].smode, "srcreg", table68k[opcode].size, "src");
	break;
     case i_NBCD: 
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	start_brace ();
	printf("\tUWORD newv_lo = - (src & 0xF) - (XFLG ? 1 : 0);\n");
	printf("\tUWORD newv_hi = - (src & 0xF0);\n");
	printf("\tUWORD newv;\n");
	printf("\tif (newv_lo > 9) { newv_lo-=6; newv_hi-=0x10; }\n");
	printf("\tnewv = newv_hi + (newv_lo & 0xF);");
	printf("\tCFLG = XFLG = (newv_hi & 0x1F0) > 0x90;\n");
	printf("\tif (CFLG) newv -= 0x60;\n");
	genflags(flag_zn, table68k[opcode].size, "newv", "", "");
	genastore("newv", table68k[opcode].smode, "srcreg", table68k[opcode].size, "src");
	break;
     case i_CLR: 
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 0, 0);
	genflags(flag_logical, table68k[opcode].size, "0", "", "");
	genastore("0",table68k[opcode].smode, "srcreg", table68k[opcode].size, "src");
	break;
     case i_NOT: 
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	start_brace ();
	printf("\tULONG dst = ~src;\n");
	genflags(flag_logical, table68k[opcode].size, "dst", "", "");
	genastore("dst",table68k[opcode].smode, "srcreg", table68k[opcode].size, "src");
	break;
     case i_TST:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	genflags(flag_logical, table68k[opcode].size, "src", "", "");
	break;
     case i_BTST:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst", 1, 0);
	if (table68k[opcode].size == sz_byte)
	    printf("\tsrc &= 7;\n");
	else
	    printf("\tsrc &= 31;\n");
	printf("\tZFLG = !(dst & (1 << src));\n");
	break;
     case i_BCHG:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst", 1, 0);
	if (table68k[opcode].size == sz_byte)
	    printf("\tsrc &= 7;\n");
	else
	    printf("\tsrc &= 31;\n");
	printf("\tZFLG = !(dst & (1 << src));\n");
	printf("\tdst ^= (1 << src);\n");
	genastore("dst", table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst");
	break;
     case i_BCLR:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst", 1, 0);
	if (table68k[opcode].size == sz_byte)
	    printf("\tsrc &= 7;\n");
	else
	    printf("\tsrc &= 31;\n");
	printf("\tZFLG = !(dst & (1 << src));\n");
	printf("\tdst &= ~(1 << src);\n");
	genastore("dst", table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst");
	break;
     case i_BSET:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst", 1, 0);
	if (table68k[opcode].size == sz_byte)
	    printf("\tsrc &= 7;\n");
	else
	    printf("\tsrc &= 31;\n");
	printf("\tZFLG = !(dst & (1 << src));\n");
	printf("\tdst |= (1 << src);\n");
	genastore("dst", table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst");
	break;
     case i_CMPM:
     case i_CMP:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst", 1, 0);
	start_brace ();
	genflags(flag_cmp, table68k[opcode].size, "newv", "src", "dst");
	break;
     case i_CMPA: 
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", sz_long, "dst", 1, 0);
	start_brace ();
	genflags(flag_cmp, sz_long, "newv", "src", "dst");
	break;
	/* The next two are coded a little unconventional, but they are doing
	 * weird things... */
     case i_MVPRM:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	printf("\tCPTR memp = m68k_areg(regs, dstreg) + nextiword();\n");
	if (table68k[opcode].size == sz_word) {
	    printf("\tput_byte(memp, src >> 8); put_byte(memp + 2, src);\n");
	} else {
	    printf("\tput_byte(memp, src >> 24); put_byte(memp + 2, src >> 16);\n");
	    printf("\tput_byte(memp + 4, src >> 8); put_byte(memp + 6, src);\n");
	}
	break;
     case i_MVPMR: 
	printf("\tCPTR memp = m68k_areg(regs, srcreg) + nextiword();\n");
	genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst", 0, 0);
	if (table68k[opcode].size == sz_word) {
	    printf("\tUWORD val = (get_byte(memp) << 8) + get_byte(memp + 2);\n");
	} else {
	    printf("\tULONG val = (get_byte(memp) << 24) + (get_byte(memp + 2) << 16)\n");
	    printf("              + (get_byte(memp + 4) << 8) + get_byte(memp + 6);\n");
	}
	genastore("val", table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst");
	break;
     case i_MOVE:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst", 0, 0);
	genflags(flag_logical, table68k[opcode].size, "src", "", "");
	genastore("src", table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst");
	break;
     case i_MOVEA:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst", 0, 0);
	if (table68k[opcode].size == sz_word) {
	    printf("\tULONG val = (LONG)(WORD)src;\n");
	} else {
	    printf("\tULONG val = src;\n");
	}
	genastore("val", table68k[opcode].dmode, "dstreg", sz_long, "dst");
	break;
     case i_MVSR2: 
	genamode(table68k[opcode].smode, "srcreg", sz_word, "src", 0, 0);
	printf("\tMakeSR();\n");
	if (table68k[opcode].size == sz_byte)
	    genastore("regs.sr & 0xff", table68k[opcode].smode, "srcreg", sz_word, "src");
	else
	    genastore("regs.sr", table68k[opcode].smode, "srcreg", sz_word, "src");
	break;
     case i_MV2SR:
	genamode(table68k[opcode].smode, "srcreg", sz_word, "src", 1, 0);
	if (table68k[opcode].size == sz_byte)
	    printf("\tMakeSR();\n\tregs.sr &= 0xFF00;\n\tregs.sr |= src & 0xFF;\n");
	else {		    
	    printf("\tregs.sr = src;\n");
	}
	printf("\tMakeFromSR();\n");
	break;
     case i_SWAP: 
	genamode(table68k[opcode].smode, "srcreg", sz_long, "src", 1, 0);
	start_brace ();
	printf("\tULONG dst = ((src >> 16)&0xFFFF) | ((src&0xFFFF)<<16);\n");
	genflags(flag_logical, sz_long, "dst", "", "");
	genastore("dst",table68k[opcode].smode, "srcreg", sz_long, "src");
	break;
     case i_EXG:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst", 1, 0);
	genastore("dst",table68k[opcode].smode, "srcreg", table68k[opcode].size, "src");
	genastore("src",table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst");
	break;
     case i_EXT:
	genamode(table68k[opcode].smode, "srcreg", sz_long, "src", 1, 0);
	start_brace ();
	switch(table68k[opcode].size) {
	 case sz_byte: printf("\tULONG dst = (LONG)(BYTE)src;\n"); break;
	 case sz_word: printf("\tUWORD dst = (WORD)(BYTE)src;\n"); break;
	 case sz_long: printf("\tULONG dst = (LONG)(WORD)src;\n"); break;
	 default: abort();
	}
	genflags(flag_logical,
		table68k[opcode].size == sz_word ? sz_word : sz_long, "dst", "", "");
	genastore("dst",table68k[opcode].smode, "srcreg", 
		table68k[opcode].size == sz_word ? sz_word : sz_long, "src");
	break;
     case i_MVMEL:
	genmovemel(opcode);
	break;
     case i_MVMLE:
	genmovemle(opcode);
	break;
     case i_TRAP:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	printf("\tException(src+32,0);\n");
	break;
     case i_MVR2USP:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	printf("\tregs.usp = src;\n");
	break;
     case i_MVUSP2R: 
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 0, 0);
	genastore("regs.usp", table68k[opcode].smode, "srcreg", table68k[opcode].size, "src");
	break;
     case i_RESET:
	printf("\tcustomreset();\n");
	break;
     case i_NOP:
	break;
     case i_STOP:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	printf("\tregs.sr = src;\n");
	printf("\tMakeFromSR();\n");
	printf("\tm68k_setstopped(1);\n");
	break;
     case i_RTE:
	if (CPU_LEVEL == 0) {
	    genamode(Aipi, "7", sz_word, "sr", 1, 0);
	    genamode(Aipi, "7", sz_long, "pc", 1, 0);
	    printf("\tregs.sr = sr; m68k_setpc_rte(pc);\n");
	    printf("\tMakeFromSR();\n");
	}
	else {
	    int old_brace_level = n_braces;
	    printf("\tUWORD newsr; ULONG newpc; for (;;) {\n");
	    genamode(Aipi, "7", sz_word, "sr", 1, 0);
	    genamode(Aipi, "7", sz_long, "pc", 1, 0);
	    genamode(Aipi, "7", sz_word, "format", 1, 0);
	    printf("\tnewsr = sr; newpc = pc;\n");
	    printf("\tif ((format & 0xF000) == 0x0000) { break; }\n");
	    printf("\telse if ((format & 0xF000) == 0x1000) { ; }\n");
	    printf("\telse if ((format & 0xF000) == 0x2000) { m68k_areg(regs, 7) += 4; break; }\n");
	    printf("\telse if ((format & 0xF000) == 0x8000) { m68k_areg(regs, 7) += 50; break; }\n");
	    printf("\telse if ((format & 0xF000) == 0x9000) { m68k_areg(regs, 7) += 12; break; }\n");
	    printf("\telse if ((format & 0xF000) == 0xa000) { m68k_areg(regs, 7) += 24; break; }\n");
	    printf("\telse if ((format & 0xF000) == 0xb000) { m68k_areg(regs, 7) += 84; break; }\n");
	    printf("\telse { Exception(14,0); return; }\n");
	    printf("\tregs.sr = newsr; MakeFromSR();\n}\n");
	    pop_braces (old_brace_level);
	    printf("\tregs.sr = newsr; MakeFromSR();\n");
	    printf("\tm68k_setpc_rte(newpc);\n");
	}
	break;
     case i_RTD:
	printf("\tcompiler_flush_jsr_stack();\n");
	genamode(Aipi, "7", sz_long, "pc", 1, 0);
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "offs", 1, 0);
	printf("\tm68k_areg(regs, 7) += offs;\n");
	printf("\tm68k_setpc_rte(pc);\n");
	break;
     case i_LINK:
	genamode(Apdi, "7", sz_long, "old", 0, 0);
	genamode(table68k[opcode].smode, "srcreg", sz_long, "src", 1, 0);
	genastore("src", Apdi, "7", sz_long, "old");
	genastore("m68k_areg(regs, 7)", table68k[opcode].smode, "srcreg", sz_long, "src");
	genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "offs", 1, 0);
	printf("\tm68k_areg(regs, 7) += offs;\n");
	break;
     case i_UNLK:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	printf("\tm68k_areg(regs, 7) = src;\n");
	genamode(Aipi, "7", sz_long, "old", 1, 0);
	genastore("old", table68k[opcode].smode, "srcreg", table68k[opcode].size, "src");
	break;
     case i_RTS:
	printf("\tm68k_do_rts();\n");
	break;
     case i_TRAPV:
	printf("\tif(VFLG) Exception(7,m68k_getpc()-2);\n");
	break;
     case i_RTR:
	printf("\tcompiler_flush_jsr_stack();\n");
	printf("\tMakeSR();\n");
	genamode(Aipi, "7", sz_word, "sr", 1, 0);
	genamode(Aipi, "7", sz_long, "pc", 1, 0);
	printf("\tregs.sr &= 0xFF00; sr &= 0xFF;\n");
	printf("\tregs.sr |= sr; m68k_setpc(pc);\n");
	printf("\tMakeFromSR();\n");
	break;
     case i_JSR:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 0, 0);
	printf("\tm68k_do_jsr(srca);\n");
	break;
     case i_JMP: 
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 0, 0);
	printf("\tm68k_setpc(srca);\n");
	break;
	/* Put on your cool #ifdef shades... */
     case i_BSR:
#if defined(USE_POINTER)
	printf("\tchar *oldpcp = (char *)regs.pc_p;\n");
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	printf("\tLONG s = (LONG)src - (((char *)regs.pc_p) - oldpcp);\n");
#else
	printf("\tULONG oldpc = m68k_getpc();\n");
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	printf("\tLONG s = (LONG)src - (m68k_getpc() - oldpc);\n");
#endif
	printf("\tm68k_do_bsr(s);\n");
	break;
     case i_Bcc:
#if defined(USE_POINTER) && !defined(USE_COMPILER)
	printf("\tchar *oldpcp = (char *)regs.pc_p;\n");
#else
	printf("\tULONG oldpc = m68k_getpc();\n");
#endif
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	printf("\tif (cctrue(%d)) {\n", table68k[opcode].cc);
#ifdef USE_COMPILER
	printf("\tm68k_setpc_bcc(oldpc + (LONG)src);\n");
#elif defined(USE_POINTER)
	printf("\tregs.pc_p = (UBYTE *)(oldpcp + (LONG)src);\n");
#else
	printf("\tregs.pc = oldpc + (LONG)src;\n");
#endif
#ifndef NO_EXCEPTION_3
	printf("\tif (src & 1) Exception(3,0);\n");
#endif
	printf("\t}\n");
	break;
     case i_LEA:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 0, 0);
	genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst", 0, 0);
	genastore("srca", table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst");
	break;
     case i_PEA:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 0, 0);
	genamode(Apdi, "7", sz_long, "dst", 0, 0);
	genastore("srca", Apdi, "7", sz_long, "dst");
	break;
     case i_DBcc:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "offs", 1, 0);
	printf("\tif (!cctrue(%d)) {\n", table68k[opcode].cc);
	printf("\t\tif (src-- & 0xFFFF)"); /* The & 0xFFFF is for making the broken BeBox compiler happy */
#ifdef USE_COMPILER
	printf("m68k_setpc_bcc(m68k_getpc() + (LONG)offs - 2);\n");
#elif defined(USE_POINTER) 
	printf("regs.pc_p = (UBYTE *)((char *)regs.pc_p + (LONG)offs - 2);\n");
#else
	printf("regs.pc += (LONG)offs - 2;\n");
#endif
	genastore("src", table68k[opcode].smode, "srcreg", table68k[opcode].size, "src");
	printf("\t}\n");
	break;
     case i_Scc: 
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 0, 0);
	start_brace ();
	printf("\tint val = cctrue(%d) ? 0xff : 0;\n", table68k[opcode].cc);
	genastore("val",table68k[opcode].smode, "srcreg", table68k[opcode].size, "src");
	break;
     case i_DIVU:
	printf("\tCPTR oldpc = m68k_getpc();\n");
	genamode(table68k[opcode].smode, "srcreg", sz_word, "src", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", sz_long, "dst", 1, 0);
	printf("\tif(src == 0) Exception(5,oldpc-2); else {\n");
	printf("\tULONG newv = (ULONG)dst / (ULONG)(UWORD)src;\n");
	printf("\tULONG rem = (ULONG)dst %% (ULONG)(UWORD)src;\n");
	/* The N flag appears to be set each time there is an overflow.
	 * Weird. */
	printf("\tif (newv > 0xffff) { VFLG = NFLG = 1; CFLG = 0; } else\n\t{\n");
	genflags(flag_logical, sz_word, "newv", "", "");
	printf("\tnewv = (newv & 0xffff) | ((ULONG)rem << 16);\n");
	genastore("newv",table68k[opcode].dmode, "dstreg", sz_long, "dst");
	printf("\t}\n");
	printf("\t}\n");
	break;
     case i_DIVS: 
	printf("\tCPTR oldpc = m68k_getpc();\n");
	genamode(table68k[opcode].smode, "srcreg", sz_word, "src", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", sz_long, "dst", 1, 0);
	printf("\tif(src == 0) Exception(5,oldpc-2); else {\n");
	printf("\tLONG newv = (LONG)dst / (LONG)(WORD)src;\n");
	printf("\tUWORD rem = (LONG)dst %% (LONG)(WORD)src;\n");
	printf("\tif ((newv & 0xffff8000) != 0 && (newv & 0xffff8000) != 0xffff8000) { VFLG = NFLG = 1; CFLG = 0; } else\n\t{\n");
	printf("\tif (((WORD)rem < 0) != ((LONG)dst < 0)) rem = -rem;\n");
	genflags(flag_logical, sz_word, "newv", "", "");
	printf("\tnewv = (newv & 0xffff) | ((ULONG)rem << 16);\n");
	genastore("newv",table68k[opcode].dmode, "dstreg", sz_long, "dst");
	printf("\t}\n");
	printf("\t}\n");
	break;
     case i_MULU: 
	genamode(table68k[opcode].smode, "srcreg", sz_word, "src", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", sz_word, "dst", 1, 0);
	start_brace ();
	printf("\tULONG newv = (ULONG)(UWORD)dst * (ULONG)(UWORD)src;\n");
	genflags(flag_logical, sz_long, "newv", "", "");
	genastore("newv",table68k[opcode].dmode, "dstreg", sz_long, "dst");
#ifdef WANT_SLOW_MULTIPLY
	printf("\tregs.spcflags |= SPCFLAG_EXTRA_CYCLES;\n");
#endif
	break;
     case i_MULS:
	genamode(table68k[opcode].smode, "srcreg", sz_word, "src", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", sz_word, "dst", 1, 0);
	start_brace ();
	printf("\tULONG newv = (LONG)(WORD)dst * (LONG)(WORD)src;\n");
	genflags(flag_logical, sz_long, "newv", "", "");
	genastore("newv",table68k[opcode].dmode, "dstreg", sz_long, "dst");
#ifdef WANT_SLOW_MULTIPLY
	printf("\tregs.spcflags |= SPCFLAG_EXTRA_CYCLES;\n");
#endif
	break;
     case i_CHK:
	printf("\tCPTR oldpc = m68k_getpc();\n");
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst", 1, 0);
	printf("\tif ((LONG)dst < 0) { NFLG=1; Exception(6,oldpc-2); }\n");
	printf("\telse if (dst > src) { NFLG=0; Exception(6,oldpc-2); }\n");
	break;
	
     case i_CHK2:
	printf("\tCPTR oldpc = m68k_getpc();\n");
	genamode (table68k[opcode].smode, "srcreg", table68k[opcode].size, "extra", 1, 0);
	genamode (table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst", 0, 0);
	printf("\t{LONG upper,lower,reg = regs.regs[(extra >> 12) & 15];\n");
	switch (table68k[opcode].size) {
	case sz_byte: printf("\tlower=(LONG)(BYTE)get_byte(dsta); upper = (LONG)(BYTE)get_byte(dsta+1);\n");
		      printf("\tif ((extra & 0x8000) == 0) reg = (LONG)(BYTE)reg;\n");
		      break;
	case sz_word: printf("\tlower=(LONG)(WORD)get_word(dsta); upper = (LONG)(WORD)get_word(dsta+2);\n");
		      printf("\tif ((extra & 0x8000) == 0) reg = (LONG)(WORD)reg;\n");
		      break;
	case sz_long: printf("\tlower=get_long(dsta); upper = get_long(dsta+4);\n");
		      break;
	default:
		      abort();
	}
	printf("\tZFLG=upper == reg || lower == reg;\n");
	printf("\tCFLG=lower <= upper ? reg < lower || reg > upper : reg > upper || reg < lower;\n");
	printf("\tif ((extra & 0x800) && CFLG) Exception(6,oldpc-2);\n}\n");
	break;
	
     case i_ASR: 
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "cnt", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "data", 1, 0);
	start_brace ();
	switch(table68k[opcode].size) {
	 case sz_byte: printf("\tUBYTE val = data;\n"); break;
	 case sz_word: printf("\tUWORD val = data;\n"); break;
	 case sz_long: printf("\tULONG val = data;\n"); break;
	 default: abort();
	}
	switch(table68k[opcode].size) {
	 case sz_byte: printf("\tULONG cmask = 0x80;\n"); break;
	 case sz_word: printf("\tULONG cmask = 0x8000;\n"); break;
	 case sz_long: printf("\tULONG cmask = 0x80000000;\n"); break;
	 default: abort();
	}
	printf("\tULONG sign = cmask & val;\n");
	printf("\tcnt &= 63;\n");
	printf("\tVFLG = 0;\n");
	printf("\tif (!cnt) { CFLG = 0; } else {\n");
	printf("\tif (cnt >= %d) {\n",bit_size(table68k[opcode].size));
	printf("\t\tval = sign ? %s : 0;\n",bit_mask(table68k[opcode].size));
	printf("\t\tCFLG=XFLG= sign ? 1 : 0;\n");
	printf("\t} else {\n");
	printf("\t\tCFLG=XFLG=(val >> (cnt-1)) & 1;\n");
	printf("\t\tval >>= cnt;\n");
	printf("\t\tif (sign) val |= %s << (%d - cnt);\n",
			bit_mask(table68k[opcode].size),
			bit_size(table68k[opcode].size));
	printf("\t}}\n");
	printf("\tNFLG = sign != 0;\n");
	printf("\tZFLG = val == 0;\n");
	genastore("val", table68k[opcode].dmode, "dstreg", table68k[opcode].size, "data");
	break;
     case i_ASL:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "cnt", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "data", 1, 0);
	start_brace ();
	switch(table68k[opcode].size) {
	 case sz_byte: printf("\tUBYTE val = data;\n"); break;
	 case sz_word: printf("\tUWORD val = data;\n"); break;
	 case sz_long: printf("\tULONG val = data;\n"); break;
	 default: abort();
	}
	switch(table68k[opcode].size) {
	 case sz_byte: printf("\tULONG cmask = 0x80;\n"); break;
	 case sz_word: printf("\tULONG cmask = 0x8000;\n"); break;
	 case sz_long: printf("\tULONG cmask = 0x80000000;\n"); break;
	 default: abort();
	}
	printf("\tcnt &= 63;\n");
	printf("\tVFLG = 0;\n");
	printf("\tif (!cnt) { CFLG = 0; } else {\n");
	printf("\tif (cnt >= %d) {\n",bit_size(table68k[opcode].size));
	printf("\t\tVFLG = val != 0;\n");
	printf("\t\tCFLG=XFLG = cnt == %d ? val & 1 : 0;\n",
					bit_size(table68k[opcode].size));
	printf("\t\tval = 0;\n");
	printf("\t} else {\n");
	printf("\t\tULONG mask = (%s << (%d - cnt)) & %s;\n",
			bit_mask(table68k[opcode].size),
			bit_size(table68k[opcode].size)-1,
			bit_mask(table68k[opcode].size));
	printf("\t\tCFLG=XFLG=(val << (cnt-1)) & cmask ? 1 : 0;\n");
	printf("\t\tVFLG = (val & mask) != mask && (val & mask) != 0;\n");
	printf("\t\tval <<= cnt;\n");
	printf("\t}}\n");
	printf("\tNFLG = (val&cmask) != 0;\n");
	printf("\tZFLG = val == 0;\n");
	genastore("val", table68k[opcode].dmode, "dstreg", table68k[opcode].size, "data");
	break;
     case i_LSR:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "cnt", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "data", 1, 0);
	start_brace ();
	switch(table68k[opcode].size) {
	 case sz_byte: printf("\tUBYTE val = data;\n"); break;
	 case sz_word: printf("\tUWORD val = data;\n"); break;
	 case sz_long: printf("\tULONG val = data;\n"); break;
	 default: abort();
	}
	switch(table68k[opcode].size) {
	 case sz_byte: printf("\tULONG cmask = 0x80;\n"); break;
	 case sz_word: printf("\tULONG cmask = 0x8000;\n"); break;
	 case sz_long: printf("\tULONG cmask = 0x80000000;\n"); break;
	 default: abort();
	}
	printf("\tcnt &= 63;\n");
	printf("\tif (!cnt) { CFLG = 0; } else {\n");
	printf("\tif (cnt >= %d) {\n",bit_size(table68k[opcode].size));
	printf("\t\tCFLG=XFLG = cnt == %d ? (val & cmask ? 1 : 0) : 0;\n",
					bit_size(table68k[opcode].size));
	printf("\t\tval = 0;\n");
	printf("\t} else {\n");
	printf("\t\tCFLG=XFLG=(val >> (cnt-1)) & 1;\n");
	printf("\t\tval >>= cnt;\n");
	printf("\t}}\n");
	printf("\tNFLG = (val & cmask) != 0; ZFLG = val == 0; VFLG = 0;\n");
	genastore("val", table68k[opcode].dmode, "dstreg", table68k[opcode].size, "data");    
	break;
     case i_LSL:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "cnt", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "data", 1, 0);
	start_brace ();
	switch(table68k[opcode].size) {
	 case sz_byte: printf("\tUBYTE val = data;\n"); break;
	 case sz_word: printf("\tUWORD val = data;\n"); break;
	 case sz_long: printf("\tULONG val = data;\n"); break;
	 default: abort();
	}
	switch(table68k[opcode].size) {
	 case sz_byte: printf("\tULONG cmask = 0x80;\n"); break;
	 case sz_word: printf("\tULONG cmask = 0x8000;\n"); break;
	 case sz_long: printf("\tULONG cmask = 0x80000000;\n"); break;
	 default: abort();
	}
	printf("\tcnt &= 63;\n");
	printf("\tif (!cnt) { CFLG = 0; } else {\n");
	printf("\tif (cnt >= %d) {\n",bit_size(table68k[opcode].size));
	printf("\t\tCFLG=XFLG = cnt == %d ? val & 1 : 0;\n",
					bit_size(table68k[opcode].size));
	printf("\t\tval = 0;\n");
	printf("\t} else {\n");
	printf("\t\tCFLG=XFLG=(val << (cnt-1)) & cmask ? 1 : 0;\n");
	printf("\t\tval <<= cnt;\n");
	printf("\t}}\n");
	printf("\tNFLG = (val & cmask) != 0; ZFLG = val == 0; VFLG = 0;\n");
	genastore("val", table68k[opcode].dmode, "dstreg", table68k[opcode].size, "data");
	break;
     case i_ROL:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "cnt", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "data", 1, 0);
	start_brace ();
	switch(table68k[opcode].size) {
	 case sz_byte: printf("\tUBYTE val = data;\n"); break;
	 case sz_word: printf("\tUWORD val = data;\n"); break;
	 case sz_long: printf("\tULONG val = data;\n"); break;
	 default: abort();
	}
	switch(table68k[opcode].size) {
	 case sz_byte: printf("\tULONG cmask = 0x80;\n"); break;
	 case sz_word: printf("\tULONG cmask = 0x8000;\n"); break;
	 case sz_long: printf("\tULONG cmask = 0x80000000;\n"); break;
	 default: abort();
	}
	printf("\tcnt &= 63;\n");	
	printf("\tif (!cnt) { CFLG = 0; } else {\n");
	printf("\tULONG carry;\n");
	printf("\tfor(;cnt;--cnt){\n");
	printf("\tcarry=val&cmask; val <<= 1;\n");
	printf("\tif(carry) val |= 1;\n");
	printf("\t}\n");
	printf("\tCFLG = carry!=0;\n}\n");
	printf("\tNFLG = (val & cmask) != 0; ZFLG = val == 0; VFLG = 0;\n");
	genastore("val", table68k[opcode].dmode, "dstreg", table68k[opcode].size, "data");
	break;
     case i_ROR:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "cnt", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "data", 1, 0);
	start_brace ();
	switch(table68k[opcode].size) {
	 case sz_byte: printf("\tUBYTE val = data;\n"); break;
	 case sz_word: printf("\tUWORD val = data;\n"); break;
	 case sz_long: printf("\tULONG val = data;\n"); break;
	 default: abort();
	}
	switch(table68k[opcode].size) {
	 case sz_byte: printf("\tULONG cmask = 0x80;\n"); break;
	 case sz_word: printf("\tULONG cmask = 0x8000;\n"); break;
	 case sz_long: printf("\tULONG cmask = 0x80000000;\n"); break;
	 default: abort();
	}
	printf("\tcnt &= 63;\n");
	printf("\tif (!cnt) { CFLG = 0; } else {");
	printf("\tULONG carry;\n");	
	printf("\tfor(;cnt;--cnt){\n");
	printf("\tcarry=val&1; val = val >> 1;\n");
	printf("\tif(carry) val |= cmask;\n");
	printf("\t}\n");
	printf("\tCFLG = carry;\n}\n");
	printf("\tNFLG = (val & cmask) != 0; ZFLG = val == 0; VFLG = 0;\n");
	genastore("val", table68k[opcode].dmode, "dstreg", table68k[opcode].size, "data");
	break;
     case i_ROXL: 
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "cnt", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "data", 1, 0);
	start_brace ();
	switch(table68k[opcode].size) {
	 case sz_byte: printf("\tUBYTE val = data;\n"); break;
	 case sz_word: printf("\tUWORD val = data;\n"); break;
	 case sz_long: printf("\tULONG val = data;\n"); break;
	 default: abort();
	}
	switch(table68k[opcode].size) {
	 case sz_byte: printf("\tULONG cmask = 0x80;\n"); break;
	 case sz_word: printf("\tULONG cmask = 0x8000;\n"); break;
	 case sz_long: printf("\tULONG cmask = 0x80000000;\n"); break;
	 default: abort();
	}
	printf("\tULONG carry;\n");
	printf("\tcnt &= 63;\n");
	printf("\tfor(;cnt;--cnt){\n");
	printf("\tcarry=val&cmask; val <<= 1;\n");
	printf("\tif(XFLG) val |= 1;\n");    
	printf("\tXFLG = carry != 0;\n");
	printf("\t}\n");
	printf("\tCFLG = XFLG;\n");
	printf("\tNFLG = (val & cmask) != 0; ZFLG = val == 0; VFLG = 0;\n");
	genastore("val", table68k[opcode].dmode, "dstreg", table68k[opcode].size, "data");
	break;
     case i_ROXR:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "cnt", 1, 0);
	genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "data", 1, 0);
	start_brace ();
	switch(table68k[opcode].size) {
	 case sz_byte: printf("\tUBYTE val = data;\n"); break;
	 case sz_word: printf("\tUWORD val = data;\n"); break;
	 case sz_long: printf("\tULONG val = data;\n"); break;
	 default: abort();
	}
	switch(table68k[opcode].size) {
	 case sz_byte: printf("\tULONG cmask = 0x80;\n"); break;
	 case sz_word: printf("\tULONG cmask = 0x8000;\n"); break;
	 case sz_long: printf("\tULONG cmask = 0x80000000;\n"); break;
	 default: abort();
	}
	printf("\tULONG carry;\n");
	printf("\tcnt &= 63;\n");
	printf("\tfor(;cnt;--cnt){\n");
	printf("\tcarry=val&1; val >>= 1;\n");
	printf("\tif(XFLG) val |= cmask;\n");
	printf("\tXFLG = carry;\n");
	printf("\t}\n");
	printf("\tCFLG = XFLG;\n");
	printf("\tNFLG = (val & cmask) != 0; ZFLG = val == 0; VFLG = 0;\n");
	genastore("val", table68k[opcode].dmode, "dstreg", table68k[opcode].size, "data");
	break;
     case i_ASRW:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "data", 1, 0);
	printf("\tVFLG = 0;\n");
	start_brace ();
	switch(table68k[opcode].size) {
	 case sz_byte: printf("\tUBYTE val = data;\n"); break;
	 case sz_word: printf("\tUWORD val = data;\n"); break;
	 case sz_long: printf("\tULONG val = data;\n"); break;
	 default: abort();
	}
	switch(table68k[opcode].size) {
	 case sz_byte: printf("\tULONG cmask = 0x80;\n"); break;
	 case sz_word: printf("\tULONG cmask = 0x8000;\n"); break;
	 case sz_long: printf("\tULONG cmask = 0x80000000;\n"); break;
	 default: abort();
	}
	printf("\tULONG sign = cmask & val;\n");
	printf("\tCFLG=XFLG=val&1; val = (val >> 1) | sign;\n");
	printf("\tNFLG = sign != 0;\n");
	printf("\tZFLG = val == 0;\n");
	genastore("val", table68k[opcode].smode, "srcreg", table68k[opcode].size, "data");
	break;
     case i_ASLW:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "data", 1, 0);
	printf("\tVFLG = 0;\n");
	start_brace ();
	switch(table68k[opcode].size) {
	 case sz_byte: printf("\tUBYTE val = data;\n"); break;
	 case sz_word: printf("\tUWORD val = data;\n"); break;
	 case sz_long: printf("\tULONG val = data;\n"); break;
	 default: abort();
	}
	switch(table68k[opcode].size) {
	 case sz_byte: printf("\tULONG cmask = 0x80;\n"); break;
	 case sz_word: printf("\tULONG cmask = 0x8000;\n"); break;
	 case sz_long: printf("\tULONG cmask = 0x80000000;\n"); break;
	 default: abort();
	}
	printf("\tULONG sign = cmask & val;\n");
	printf("\tCFLG=XFLG=(val&cmask)!=0; val <<= 1;\n");
	printf("\tif ((val&cmask)!=sign) VFLG=1;\n");
	printf("\tNFLG = (val&cmask) != 0;\n");
	printf("\tZFLG = val == 0;\n");
	genastore("val", table68k[opcode].smode, "srcreg", table68k[opcode].size, "data");
	break;
     case i_LSRW:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "data", 1, 0);
	start_brace ();
	switch(table68k[opcode].size) {
	 case sz_byte: printf("\tUBYTE val = data;\n"); break;
	 case sz_word: printf("\tUWORD val = data;\n"); break;
	 case sz_long: printf("\tULONG val = data;\n"); break;
	 default: abort();
	}
	printf("\tULONG carry = val&1;\n");
	printf("\tval >>= 1;\n");
	genflags(flag_logical, table68k[opcode].size, "val", "", "");
	printf("CFLG = XFLG = carry!=0;\n");
	genastore("val", table68k[opcode].smode, "srcreg", table68k[opcode].size, "data");
	break;
     case i_LSLW:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "data", 1, 0);
	start_brace ();
	switch(table68k[opcode].size) {
	 case sz_byte: printf("\tUBYTE val = data;\n"); break;
	 case sz_word: printf("\tUWORD val = data;\n"); break;
	 case sz_long: printf("\tULONG val = data;\n"); break;
	 default: abort();
	}
	switch(table68k[opcode].size) {
	 case sz_byte: printf("\tULONG cmask = 0x80;\n"); break;
	 case sz_word: printf("\tULONG cmask = 0x8000;\n"); break;
	 case sz_long: printf("\tULONG cmask = 0x80000000;\n"); break;
	 default: abort();
	}
	printf("\tULONG carry = val&cmask;\n");
	printf("\tval <<= 1;\n");
	genflags(flag_logical, table68k[opcode].size, "val", "", "");
	printf("CFLG = XFLG = carry!=0;\n");
	genastore("val", table68k[opcode].smode, "srcreg", table68k[opcode].size, "data");
	break;
     case i_ROLW:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "data", 1, 0);
	start_brace ();
	switch(table68k[opcode].size) {
	 case sz_byte: printf("\tUBYTE val = data;\n"); break;
	 case sz_word: printf("\tUWORD val = data;\n"); break;
	 case sz_long: printf("\tULONG val = data;\n"); break;
	 default: abort();
	}
	switch(table68k[opcode].size) {
	 case sz_byte: printf("\tULONG cmask = 0x80;\n"); break;
	 case sz_word: printf("\tULONG cmask = 0x8000;\n"); break;
	 case sz_long: printf("\tULONG cmask = 0x80000000;\n"); break;
	 default: abort();
	}
	printf("\tULONG carry = val&cmask;\n");
	printf("\tval <<= 1;\n");
	printf("\tif(carry)  val |= 1;\n");
	genflags(flag_logical, table68k[opcode].size, "val", "", "");
	printf("CFLG = carry!=0;\n");
	genastore("val", table68k[opcode].smode, "srcreg", table68k[opcode].size, "data");
	break;
     case i_RORW:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "data", 1, 0);
	start_brace ();
	switch(table68k[opcode].size) {
	 case sz_byte: printf("\tUBYTE val = data;\n"); break;
	 case sz_word: printf("\tUWORD val = data;\n"); break;
	 case sz_long: printf("\tULONG val = data;\n"); break;
	 default: abort();
	}
	printf("\tULONG carry = val&1;\n");
	switch(table68k[opcode].size) {
	 case sz_byte: printf("\tULONG cmask = 0x80;\n"); break;
	 case sz_word: printf("\tULONG cmask = 0x8000;\n"); break;
	 case sz_long: printf("\tULONG cmask = 0x80000000;\n"); break;
	 default: abort();
	}
	printf("\tval >>= 1;\n");
	printf("\tif(carry) val |= cmask;\n");
	genflags(flag_logical, table68k[opcode].size, "val", "", "");
	printf("CFLG = carry!=0;\n");
	genastore("val", table68k[opcode].smode, "srcreg", table68k[opcode].size, "data");
	break;
     case i_ROXLW:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "data", 1, 0);
	start_brace ();
	switch(table68k[opcode].size) {
	 case sz_byte: printf("\tUBYTE val = data;\n"); break;
	 case sz_word: printf("\tUWORD val = data;\n"); break;
	 case sz_long: printf("\tULONG val = data;\n"); break;
	 default: abort();
	}
	switch(table68k[opcode].size) {
	 case sz_byte: printf("\tULONG cmask = 0x80;\n"); break;
	 case sz_word: printf("\tULONG cmask = 0x8000;\n"); break;
	 case sz_long: printf("\tULONG cmask = 0x80000000;\n"); break;
	 default: abort();
	}
	printf("\tULONG carry = val&cmask;\n");
	printf("\tval <<= 1;\n");
	printf("\tif(XFLG) val |= 1;\n");
	printf("\tXFLG = carry != 0;\n");
	genflags(flag_logical, table68k[opcode].size, "val", "", "");
	printf("XFLG = CFLG = carry!=0;\n");
	genastore("val", table68k[opcode].smode, "srcreg", table68k[opcode].size, "data");
	break;
     case i_ROXRW:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "data", 1, 0);
	start_brace ();
	switch(table68k[opcode].size) {
	 case sz_byte: printf("\tUBYTE val = data;\n"); break;
	 case sz_word: printf("\tUWORD val = data;\n"); break;
	 case sz_long: printf("\tULONG val = data;\n"); break;
	 default: abort();
	}
	printf("\tULONG carry = val&1;\n");
	switch(table68k[opcode].size) {
	 case sz_byte: printf("\tULONG cmask = 0x80;\n"); break;
	 case sz_word: printf("\tULONG cmask = 0x8000;\n"); break;
	 case sz_long: printf("\tULONG cmask = 0x80000000;\n"); break;
	 default: abort();
	}
	printf("\tval >>= 1;\n");
	printf("\tif(XFLG) val |= cmask;\n");
	printf("\tXFLG = carry != 0;\n");
	genflags(flag_logical, table68k[opcode].size, "val", "", "");
	printf("XFLG = CFLG = carry!=0;\n");
	genastore("val", table68k[opcode].smode, "srcreg", table68k[opcode].size, "data");
	break;
     case i_MOVEC2:
	genamode (table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	start_brace();
	printf("\tint regno = (src >> 12) & 15;\n");
	printf("\tULONG *regp = regs.regs + regno;\n");
	printf("\tm68k_movec2(src & 0xFFF, regp);\n");
	break;
     case i_MOVE2C:
	genamode (table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	start_brace();
	printf("\tint regno = (src >> 12) & 15;\n");
	printf("\tULONG *regp = regs.regs + regno;\n");
	printf("\tm68k_move2c(src & 0xFFF, regp);\n");
	break;
     case i_CAS:
	{
	    int old_brace_level;
	    genamode (table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	    genamode (table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst", 1, 0);
	    start_brace();
	    printf("\tint ru = (src >> 6) & 7;\n");
	    printf("\tint rc = src & 7;\n");
	    genflags(flag_cmp, table68k[opcode].size, "newv", "m68k_dreg(regs, rc)", "dst");
	    printf("\tif (ZFLG)");
	    old_brace_level = n_braces;
	    start_brace ();
	    genastore("(m68k_dreg(regs, ru))",table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst");
	    pop_braces (old_brace_level);
	    printf("else");
	    start_brace ();
	    printf("m68k_dreg(regs, rc) = dst;\n");
	    pop_braces (old_brace_level);
	}
	break;
     case i_CAS2:
	genamode (table68k[opcode].smode, "srcreg", table68k[opcode].size, "extra", 1, 0);
	printf("\tULONG rn1 = regs.regs[(extra >> 28) & 7];\n");
	printf("\tULONG rn2 = regs.regs[(extra >> 12) & 7];\n");
	if (table68k[opcode].size == sz_word) {
	     int old_brace_level = n_braces;
	     printf("\tUWORD dst1 = get_word(rn1), dst2 = get_word(rn2);\n");
	     genflags(flag_cmp, table68k[opcode].size, "newv", "m68k_dreg(regs, (extra >> 16) & 7)", "dst1");
	     printf("\tif (ZFLG) {\n");
	     genflags(flag_cmp, table68k[opcode].size, "newv", "m68k_dreg(regs, extra & 7)", "dst2");
	     printf("\tif (ZFLG) {\n");
	     printf("\tput_word(rn1, m68k_dreg(regs, (extra >> 22) & 7));\n");
	     printf("\tput_word(rn1, m68k_dreg(regs, (extra >> 6) & 7));\n");
	     printf("\t}}\n");
	     pop_braces (old_brace_level);
	     printf("\tif (!ZFLG) {\n");
	     printf("\tm68k_dreg(regs, (extra >> 22) & 7) = (m68k_dreg(regs, (extra >> 22) & 7) & ~0xffff) | (dst1 & 0xffff);\n");
	     printf("\tm68k_dreg(regs, (extra >> 6) & 7) = (m68k_dreg(regs, (extra >> 6) & 7) & ~0xffff) | (dst2 & 0xffff);\n");
	     printf("\t}\n");
	} else {
	     int old_brace_level = n_braces;
	     printf("\tULONG dst1 = get_long(rn1), dst2 = get_long(rn2);\n");
	     genflags(flag_cmp, table68k[opcode].size, "newv", "m68k_dreg(regs, (extra >> 16) & 7)", "dst1");
	     printf("\tif (ZFLG) {\n");
	     genflags(flag_cmp, table68k[opcode].size, "newv", "m68k_dreg(regs, extra & 7)", "dst2");
	     printf("\tif (ZFLG) {\n");
	     printf("\tput_long(rn1, m68k_dreg(regs, (extra >> 22) & 7));\n");
	     printf("\tput_long(rn1, m68k_dreg(regs, (extra >> 6) & 7));\n");
	     printf("\t}}\n");
	     pop_braces (old_brace_level);
	     printf("\tif (!ZFLG) {\n");
	     printf("\tm68k_dreg(regs, (extra >> 22) & 7) = dst1;\n");
	     printf("\tm68k_dreg(regs, (extra >> 6) & 7) = dst2;\n");
	     printf("\t}\n");
	}
	break;
     case i_MOVES:	/* ignore DFC and SFC because we have no MMU */
	{
	    int old_brace_level;
	    genamode (table68k[opcode].smode, "srcreg", table68k[opcode].size, "extra", 1, 0);
	    printf("\tif (extra & 0x800)\n");
	    old_brace_level = n_braces;
	    start_brace ();
	    printf("\tULONG src = regs.regs[(extra >> 12) & 15];\n");
	    genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst", 0, 0);
            genastore("src", table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst");
	    pop_braces (old_brace_level);
	    printf("else");
	    start_brace ();
	    genamode(table68k[opcode].dmode, "dstreg", table68k[opcode].size, "src", 1, 0);
	    printf("\tif (extra & 0x8000) {\n");
	    switch(table68k[opcode].size) {
	     case sz_byte: printf("\tm68k_areg(regs, (extra >> 12) & 7) = (LONG)(BYTE)src;\n"); break;
	     case sz_word: printf("\tm68k_areg(regs, (extra >> 12) & 7) = (LONG)(WORD)src;\n"); break;
	     case sz_long: printf("\tm68k_areg(regs, (extra >> 12) & 7) = src;\n"); break;
	     default: abort();
	    }
	    printf("\t} else {\n");
	    genastore("src", Dreg, "(extra >> 12) & 7", table68k[opcode].size, "");
	    printf("\t}\n");
	    pop_braces (old_brace_level);
	}
	break;
     case i_BKPT:	/* only needed for hardware emulators */
	printf("\top_illg(opcode);\n");
	break;
     case i_CALLM:	/* not present in 68030 */
	printf("\top_illg(opcode);\n");
	break;
     case i_RTM:	/* not present in 68030 */
	printf("\top_illg(opcode);\n");
	break;
     case i_TRAPcc:
	printf("\tCPTR oldpc = m68k_getpc();\n");
	if (table68k[opcode].smode != am_unknown && table68k[opcode].smode != am_illg)
	    genamode (table68k[opcode].smode, "srcreg", table68k[opcode].size, "dummy", 1, 0);
	printf("\tif (cctrue(%d)) Exception(7,oldpc-2);\n", table68k[opcode].cc);
	break;
     case i_DIVL:
	printf("\tCPTR oldpc = m68k_getpc();\n");
	genamode (table68k[opcode].smode, "srcreg", table68k[opcode].size, "extra", 1, 0);
	genamode (table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst", 1, 0);
	printf("\tm68k_divl(opcode, dst, extra, oldpc);\n");
	break;
     case i_MULL:
	genamode (table68k[opcode].smode, "srcreg", table68k[opcode].size, "extra", 1, 0);
	genamode (table68k[opcode].dmode, "dstreg", table68k[opcode].size, "dst", 1, 0);
	printf("\tm68k_mull(opcode, dst, extra);\n");
	break;
     case i_BFTST:
     case i_BFEXTU:
     case i_BFCHG:
     case i_BFEXTS:
     case i_BFCLR:
     case i_BFFFO:
     case i_BFSET:
     case i_BFINS:
	genamode (table68k[opcode].smode, "srcreg", table68k[opcode].size, "extra", 1, 0);
	genamode (table68k[opcode].dmode, "dstreg", sz_long, "dst", 0, 0);
	start_brace();
	printf("\tLONG offset = extra & 0x800 ? m68k_dreg(regs, (extra >> 6) & 7) : (extra >> 6) & 0x1f;\n");
	printf("\tint width = (((extra & 0x20 ? m68k_dreg(regs, extra & 7) : extra) -1) & 0x1f) +1;\n");
	if (table68k[opcode].dmode == Dreg) {
	    printf("\tULONG tmp = m68k_dreg(regs, dstreg) << (offset & 0x1f);\n");
	}
	else {
	    printf("\tULONG tmp,bf0,bf1;\n");
	    printf("\tdsta += (offset >> 3) | (offset & 0x80000000 ? ~0x1fffffff : 0);\n");
	    printf("\tbf0 = get_long(dsta);bf1 = get_byte(dsta+4) & 0xff;\n");
	    printf("\ttmp = (bf0 << (offset & 7)) | (bf1 >> (8 - (offset & 7)));\n");
	}
	printf("\ttmp >>= (32 - width);\n");
	printf("\tNFLG = tmp & (1 << (width-1)) ? 1 : 0;ZFLG = tmp == 0;VFLG = 0;CFLG = 0;\n");
        switch(table68k[opcode].mnemo) {
	case i_BFTST:
		break;
	case i_BFEXTU:
		printf("\tm68k_dreg(regs, (extra >> 12) & 7) = tmp;\n");
		break;
	case i_BFCHG:
		printf("\ttmp = ~tmp;\n");
		break;
	case i_BFEXTS:
		printf("\tif (NFLG) tmp |= width == 32 ? 0 : (-1 << width);\n");
		printf("\tm68k_dreg(regs, (extra >> 12) & 7) = tmp;\n");
		break;
	case i_BFCLR:
		printf("\ttmp = 0;\n");
		break;
	case i_BFFFO:
		printf("\t{ ULONG mask = 1 << (width-1);\n");
		printf("\twhile (mask) { if (tmp & mask) break; mask >>= 1; offset++; }}\n");
		printf("\tm68k_dreg(regs, (extra >> 12) & 7) = offset;\n");
		break;
	case i_BFSET:
		printf("\ttmp = 0xffffffff;\n");
		break;
	case i_BFINS:
		printf("\ttmp = m68k_dreg(regs, (extra >> 12) & 7);\n");
		break;
	default:
		break;
	}
	if (table68k[opcode].mnemo == i_BFCHG ||
	    table68k[opcode].mnemo == i_BFCLR ||
	    table68k[opcode].mnemo == i_BFSET ||
	    table68k[opcode].mnemo == i_BFINS) {
		printf("\ttmp <<= (32 - width);\n");
		if (table68k[opcode].dmode == Dreg) {
		    printf("\tm68k_dreg(regs, dstreg) = (m68k_dreg(regs, dstreg) & ((offset & 0x1f) == 0 ? 0 :\n");
		    printf("\t\t(0xffffffff << (32 - (offset & 0x1f))))) |\n");
		    printf("\t\t(tmp >> (offset & 0x1f)) |\n");
		    printf("\t\t(((offset & 0x1f) + width) >= 32 ? 0 :\n");
		    printf(" (m68k_dreg(regs, dstreg) & ((ULONG)0xffffffff >> ((offset & 0x1f) + width))));\n");
		}
		else {
		    printf("\tbf0 = (bf0 & (0xff000000 << (8 - (offset & 7)))) |\n");
		    printf("\t\t(tmp >> (offset & 7)) |\n");
		    printf("\t\t(((offset & 7) + width) >= 32 ? 0 :\n");
		    printf("\t\t (bf0 & ((ULONG)0xffffffff >> ((offset & 7) + width))));\n");
		    printf("\tput_long(dsta,bf0 );\n");
		    printf("\tif (((offset & 7) + width) > 32) {\n");
		    printf("\t\tbf1 = (bf1 & (0xff >> (width - 32 + (offset & 7)))) |\n");
		    printf("\t\t\t(tmp << (8 - (offset & 7)));\n");
		    printf("\t\tput_byte(dsta+4,bf1);\n");
		    printf("\t}\n");
		}
	}
	break;
     case i_PACK:
	if (table68k[opcode].smode == Dreg) {
	    printf("\tUWORD val = m68k_dreg(regs, srcreg) + nextiword();\n");
	    printf("\tm68k_dreg(regs, dstreg) = (m68k_dreg(regs, dstreg) & 0xffffff00) | ((val >> 4) & 0xf0) | (val & 0xf);\n");
	}
	else {
	    printf("\tUWORD val;\n");
	    printf("\tm68k_areg(regs, srcreg) -= areg_byteinc[srcreg];\n");
	    printf("\tval = (UWORD)get_byte(m68k_areg(regs, srcreg));\n");
	    printf("\tm68k_areg(regs, srcreg) -= areg_byteinc[srcreg];\n");
	    printf("\tval = (val | ((UWORD)get_byte(m68k_areg(regs, srcreg)) << 8)) + nextiword();\n");
	    printf("\tm68k_areg(regs, dstreg) -= areg_byteinc[dstreg];\n");
	    printf("\tput_byte(m68k_areg(regs, dstreg),((val >> 4) & 0xf0) | (val & 0xf));\n");
	}
	break;
     case i_UNPK:
	if (table68k[opcode].smode == Dreg) {
	    printf("\tUWORD val = m68k_dreg(regs, srcreg);\n");
	    printf("\tval = (((val << 4) & 0xf00) | (val & 0xf)) + nextiword();\n");
	    printf("\tm68k_dreg(regs, dstreg) = (m68k_dreg(regs, dstreg) & 0xffff0000) | (val & 0xffff);\n");
	}
	else {
	    printf("\tUWORD val;\n");
	    printf("\tm68k_areg(regs, srcreg) -= areg_byteinc[srcreg];\n");
	    printf("\tval = (UWORD)get_byte(m68k_areg(regs, srcreg));\n");
	    printf("\tval = (((val << 4) & 0xf00) | (val & 0xf)) + nextiword();\n");
	    printf("\tm68k_areg(regs, dstreg) -= areg_byteinc[dstreg];\n");
	    printf("\tput_byte(m68k_areg(regs, dstreg),val);\n");
	    printf("\tm68k_areg(regs, dstreg) -= areg_byteinc[dstreg];\n");
	    printf("\tput_byte(m68k_areg(regs, dstreg),val >> 8);\n");
	}
	break;
     case i_TAS:
	genamode(table68k[opcode].smode, "srcreg", table68k[opcode].size, "src", 1, 0);
	genflags(flag_logical, table68k[opcode].size, "src", "", "");
	printf("\tsrc |= 0x80;\n");
	genastore("src", table68k[opcode].smode, "srcreg", table68k[opcode].size, "src");
	break;
     case i_FPP:
	genamode (table68k[opcode].smode, "srcreg", table68k[opcode].size, "extra", 1, 0);
	printf("\tfpp_opp(opcode,extra);\n");
	break;
     case i_FDBcc:
	genamode (table68k[opcode].smode, "srcreg", table68k[opcode].size, "extra", 1, 0);
	printf("\tfdbcc_opp(opcode,extra);\n");
	break;
     case i_FScc:
	genamode (table68k[opcode].smode, "srcreg", table68k[opcode].size, "extra", 1, 0);
	printf("\tfscc_opp(opcode,extra);\n");
	break;
     case i_FTRAPcc:
	printf("\tCPTR oldpc = m68k_getpc();\n");
	if (table68k[opcode].smode != am_unknown && table68k[opcode].smode != am_illg)
	    genamode (table68k[opcode].smode, "srcreg", table68k[opcode].size, "dummy", 1, 0);
	printf("\tftrapcc_opp(opcode,oldpc);\n");
	break;
     case i_FBcc:
	printf("\tCPTR pc = m68k_getpc();\n");
	genamode (table68k[opcode].dmode, "srcreg", table68k[opcode].size, "extra", 1, 0);
	printf("\tfbcc_opp(opcode,pc,extra);\n");
	break;
     case i_FSAVE:
	printf("\tfsave_opp(opcode);\n");
	break;
     case i_FRESTORE:
	printf("\tfrestore_opp(opcode);\n");
	break;
     case i_MMUOP:
	genamode (table68k[opcode].smode, "srcreg", table68k[opcode].size, "extra", 1, 0);
	printf("\tmmu_op(opcode,extra);\n");
	break;
     default:
	abort();
	break;
    };
    finish_braces ();
}

static void generate_func(long int from, long int to)
{
    int illg = 0;
    long int opcode;
    int i;
    UWORD smsk; 
    UWORD dmsk;    

    printf("#include \"sysconfig.h\"\n");
    printf("#include \"sysdeps.h\"\n");
    printf("#include \"config.h\"\n");
    printf("#include \"options.h\"\n");
    printf("#include \"machdep/m68k.h\"\n");
    printf("#include \"memory.h\"\n");
#ifndef BASILISK
    printf("#include \"custom.h\"\n");    
#endif
    printf("#include \"readcpu.h\"\n");
    printf("#include \"newcpu.h\"\n");
    printf("#include \"compiler.h\"\n");
    printf("#include \"cputbl.h\"\n");
    printf("#if !defined (MEMFUNCS_DIRECT_REQUESTED) || defined (DIRECT_MEMFUNCS_SUCCESSFUL)\n");
    for(opcode=from; opcode < to; opcode++) {
	if (table68k[opcode].mnemo == i_ILLG) {
	    illg++;
	    continue;
	}
	for (i = 0 ; lookuptab[i].name[0] ; i++) {
		if (table68k[opcode].mnemo == lookuptab[i].mnemo)
			break;
	}
	if (isspecific(opcode)) {
	    printf("void REGPARAM2 CPU_OP_NAME(_%lx) ## _s(ULONG opcode) /* %s */\n{\n", opcode, lookuptab[i].name);
	    if (table68k[opcode].suse
		&& table68k[opcode].smode != imm  && table68k[opcode].smode != imm0
		&& table68k[opcode].smode != imm1 && table68k[opcode].smode != imm2
		&& table68k[opcode].smode != absw && table68k[opcode].smode != absl
		&& table68k[opcode].smode != PC8r && table68k[opcode].smode != PC16)
	    {
		if (((int)table68k[opcode].sreg) >= 128)
		    printf("\tULONG srcreg = (LONG)(BYTE)%d;\n", (int)table68k[opcode].sreg);
		else
		    printf("\tULONG srcreg = %d;\n", (int)table68k[opcode].sreg);
	    }
	    if (table68k[opcode].duse
		/* Yes, the dmode can be imm, in case of LINK or DBcc */
		&& table68k[opcode].dmode != imm  && table68k[opcode].dmode != imm0
		&& table68k[opcode].dmode != imm1 && table68k[opcode].dmode != imm2
		&& table68k[opcode].dmode != absw && table68k[opcode].dmode != absl)
	    {
		if (((int)table68k[opcode].dreg) >= 128)
		    printf("\tULONG dstreg = (LONG)(BYTE)%d;\n", (int)table68k[opcode].dreg);
		else
		    printf("\tULONG dstreg = %d;\n", (int)table68k[opcode].dreg);
	    }
	    gen_opcode(opcode);
	    printf("}\n");
	}
	    
	if (table68k[opcode].handler != -1)
	    continue;


	switch (table68k[opcode].stype) {
	 case 0:
	    smsk = 7; break;
	 case 1:
	    smsk = 255; break;
	 case 2:
	    smsk = 15; break;
	 case 3:
	    smsk = 7; break;
	 case 4:
	    smsk = 7; break;
	 case 5:
	    smsk = 63; break;
	 default:
	    abort();
	}
	dmsk = 7;
	
	printf("void REGPARAM2 CPU_OP_NAME(_%lx)(ULONG opcode) /* %s */\n{\n", opcode, lookuptab[i].name);
	if (table68k[opcode].suse
	    && table68k[opcode].smode != imm  && table68k[opcode].smode != imm0
	    && table68k[opcode].smode != imm1 && table68k[opcode].smode != imm2
	    && table68k[opcode].smode != absw && table68k[opcode].smode != absl
	    && table68k[opcode].smode != PC8r && table68k[opcode].smode != PC16)
	{
	    if (table68k[opcode].spos == -1) {
		if (((int)table68k[opcode].sreg) >= 128)
		    printf("\tULONG srcreg = (LONG)(BYTE)%d;\n", (int)table68k[opcode].sreg);
		else
		    printf("\tULONG srcreg = %d;\n", (int)table68k[opcode].sreg);
	    } else {
	        char source[100];

	        if (table68k[opcode].spos)
		    sprintf(source,"((opcode >> %d) & %d)",(int)table68k[opcode].spos,smsk);
	        else
		    sprintf(source,"(opcode & %d)",smsk);

		if (table68k[opcode].stype == 3)
		    printf("\tULONG srcreg = imm8_table[%s];\n", source);
		else if (table68k[opcode].stype == 1)
		    printf("\tULONG srcreg = (LONG)(BYTE)%s;\n", source);
		else
		    printf("\tULONG srcreg = %s;\n", source);
	    }
	}
	if (table68k[opcode].duse
	    /* Yes, the dmode can be imm, in case of LINK or DBcc */
	    && table68k[opcode].dmode != imm  && table68k[opcode].dmode != imm0
	    && table68k[opcode].dmode != imm1 && table68k[opcode].dmode != imm2
	    && table68k[opcode].dmode != absw && table68k[opcode].dmode != absl) {
	    if (table68k[opcode].dpos == -1) {		
		if (((int)table68k[opcode].dreg) >= 128)
		    printf("\tULONG dstreg = (LONG)(BYTE)%d;\n", (int)table68k[opcode].dreg);
		else
		    printf("\tULONG dstreg = %d;\n", (int)table68k[opcode].dreg);
	    } else {
		if (table68k[opcode].dpos)
		    printf("\tULONG dstreg = (opcode >> %d) & %d;\n", 
				(int)table68k[opcode].dpos,dmsk);
		else
		    printf("\tULONG dstreg = opcode & %d;\n", dmsk);
	    }
	}
	gen_opcode(opcode);
        printf("}\n");
    }
    printf("#endif\n");
    fprintf (stderr, "%d illegals generated.\n", illg);
}

static void generate_table(void)
{
    int illg = 0;
    long int opcode;
    
    printf("#include \"sysconfig.h\"\n");
    printf("#include \"sysdeps.h\"\n");
    printf("#include \"config.h\"\n");
    printf("#include \"options.h\"\n");
    printf("#include \"machdep/m68k.h\"\n");
    printf("#include \"memory.h\"\n");
#ifndef BASILISK
    printf("#include \"custom.h\"\n");
#endif
    printf("#include \"readcpu.h\"\n");
    printf("#include \"newcpu.h\"\n");
    printf("#include \"compiler.h\"\n");
    printf("#include \"cputbl.h\"\n");
    
    printf("#if !defined (MEMFUNCS_DIRECT_REQUESTED) || defined (DIRECT_MEMFUNCS_SUCCESSFUL)\n");
    printf("cpuop_func *CPU_OP_NAME(_functbl)[65536] = {\n");
    for(opcode=0; opcode < 65536; opcode++) {
	if (table68k[opcode].mnemo == i_ILLG) {
	    printf("op_illg");
	    illg++;
	} else if (isspecific(opcode))
	    printf("CPU_OP_NAME(_%lx) ## _s", opcode);
	else if (table68k[opcode].handler != -1)
	    printf("CPU_OP_NAME(_%lx)", table68k[opcode].handler);
	else
	    printf("CPU_OP_NAME(_%lx)", opcode);
	
	if (opcode < 65535) printf(",");
	if ((opcode & 7) == 7) printf("\n");
    }
    printf("\n};\n#endif\n");
    fprintf (stderr, "%d illegals generated.\n", illg);
    if (get_no_mismatches())
	fprintf(stderr, "%d mismatches.\n", get_no_mismatches());
}

static void generate_smalltable(void)
{
    long int opcode;
    int i;
    
    printf("#include \"sysconfig.h\"\n");
    printf("#include \"sysdeps.h\"\n");
    printf("#include \"config.h\"\n");
    printf("#include \"options.h\"\n");
    printf("#include \"machdep/m68k.h\"\n");
    printf("#include \"memory.h\"\n");
#ifndef BASILISK
    printf("#include \"custom.h\"\n");
#endif
    printf("#include \"readcpu.h\"\n");
    printf("#include \"newcpu.h\"\n");
    printf("#include \"compiler.h\"\n");
    printf("#include \"cputbl.h\"\n");
    
    printf("struct cputbl CPU_OP_NAME(_smalltbl)[] = {\n");
    printf("#if !defined (MEMFUNCS_DIRECT_REQUESTED) || defined (DIRECT_MEMFUNCS_SUCCESSFUL)\n");
    for(opcode=0; opcode < 65536; opcode++) {
	if ((isspecific(opcode) || table68k[opcode].handler == -1)
	    && table68k[opcode].mnemo != i_ILLG) 
	{
	    for (i = 0 ; lookuptab[i].name[0] ; i++) {
		if (table68k[opcode].mnemo == lookuptab[i].mnemo)
			break;
	    }
	    if (isspecific(opcode))
		printf("{ CPU_OP_NAME(_%lx) ## _s, 1, %ld }, /* %s */\n", opcode, opcode, lookuptab[i].name);
	    if (table68k[opcode].handler == -1)
		printf("{ CPU_OP_NAME(_%lx), 0, %ld }, /* %s */\n", opcode, opcode, lookuptab[i].name);
	}
    }
    printf("#endif\n{ 0, 0, 0 }};\n");
}

static void generate_header(void)
{
    int illg = 0;
    long int opcode;
    
    printf("#if !defined (MEMFUNCS_DIRECT_REQUESTED) || defined (DIRECT_MEMFUNCS_SUCCESSFUL)\n");
    for(opcode=0; opcode < 65536; opcode++) {
	if (table68k[opcode].mnemo == i_ILLG) {
	    illg++;
	    continue;
	}
	if (isspecific(opcode))
	    printf("extern cpuop_func CPU_OP_NAME(_%lx) ## _s;\n", opcode);
	if (table68k[opcode].handler != -1)
	    continue;
	
	printf("extern cpuop_func CPU_OP_NAME(_%lx);\n", opcode);
    }
    
    printf("#endif\n");
    fprintf (stderr, "%d illegals generated.\n", illg);
    if (get_no_mismatches())
	fprintf(stderr, "%d mismatches.\n", get_no_mismatches());
}

int main(int argc, char **argv)
{
    long int range = -1;
    char mode = 'n';
    
    if (argc == 2)
    	mode = *argv[1];

    if (argc == 3) {
	range = atoi(argv[2]);
	mode = *argv[1];
    }
    
    read_table68k ();
    read_counts();
    do_merges ();
    
    switch(mode) {
     case 'f':
    	generate_func(range * 0x1000, (range + 1) * 0x1000);
	break;
     case 'h':
    	generate_header();
	break;
     case 't':
	generate_table();
	break;
     case 's':
	generate_smalltable();
	break;
     default:
	abort();
    }
    free(table68k);
    return 0;
}
