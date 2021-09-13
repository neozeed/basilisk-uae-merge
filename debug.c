 /* 
  * UAE - The Un*x Amiga Emulator
  * 
  * Debugger
  * 
  * (c) 1995 Bernd Schmidt
  * 
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include <ctype.h>
#include <signal.h>

#include "config.h"
#include "options.h"
#include "memory.h"
#ifndef BASILISK
#include "custom.h"
#endif
#include "readcpu.h"
#include "newcpu.h"
#include "debug.h"
#ifndef BASILISK
#include "cia.h"
#endif
#include "xwin.h"
#include "gui.h"


static int debugger_active = 0;
static CPTR skipaddr;
static int do_skip;
int debugging = 0;

void activate_debugger(void)
{
    do_skip = 0;
    if (debugger_active)
	return;
    debugger_active = 1;
    regs.spcflags |= SPCFLAG_BRK;
    debugging = 1;
    use_debugger = 1;
}

int firsthist = 0;
int lasthist = 0;
#ifdef NEED_TO_DEBUG_BADLY
struct regstruct history[MAX_HIST];
union flagu historyf[MAX_HIST];
#else
CPTR history[MAX_HIST];
#endif

static void ignore_ws(char **c)
{
    while (**c && isspace(**c)) (*c)++;
}

static ULONG readhex(char **c)
{
    ULONG val = 0;
    char nc;

    ignore_ws(c);
    
    while (isxdigit(nc = **c)){
	(*c)++;
	val *= 16;
	nc = toupper(nc);
	if (isdigit(nc)) {
	    val += nc - '0';
	} else {
	    val += nc - 'A' + 10;
	}
    }
    return val;
}

static char next_char(char **c)
{
    ignore_ws(c);
    return *(*c)++;
}

static int more_params(char **c)
{
    ignore_ws(c);
    return (**c) != 0;
}

static void dumpmem(CPTR addr, CPTR *nxmem, int lines)
{
    broken_in = 0;
    for (;lines-- && !broken_in;){
	int i;
	printf("%08lx ", addr);
	for(i=0; i< 16; i++) {
	    printf("%04x ", get_word(addr)); addr += 2;
	}
	printf("\n");
    }
    *nxmem = addr;
}

#ifndef BASILISK
static void modulesearch(void)
{
    ULONG ptr;
    
    for(ptr=0x438;ptr<chipmem_size;ptr+=4) {
	/* Check for Mahoney & Kaktus */
	/* Anyone got the format of old 15 Sample (SoundTracker)modules? */
	if(chipmemory[ptr] == 'M' && chipmemory[ptr+1] == '.'
	   && chipmemory[ptr+2] == 'K' && chipmemory[ptr+3] == '.')
	{
	    char name[21];
	    UBYTE *ptr2 = &chipmemory[ptr-0x438];
	    int i,j,length;

	    printf("Found possible ProTracker (31 samples) module at 0x%lx.\n",ptr-0x438);
	    memcpy(name,ptr2,20);
	    name[20] = '\0';
	    
	    /* Browse playlist */
	    ptr2 += 0x3b8;
	    i = ptr2[-2]; /* length of playlist */
	    length = 0;
	    while(i--)
		if((j=*ptr2++)>length)
		    length=j;
	    
	    length = (length+1)*1024+0x43c;
	    
	    /* Add sample lengths */
	    ptr2 = &chipmemory[ptr-0x438+0x2a];
	    for(i=0;i<31;i++,ptr2+=30)
		length += 2*((ptr2[0]<<8)+ptr2[1]);
	    
	    printf("Name \"%s\", Length %ld (0x%lx) bytes.\n", name, length, length);
	}
    }
}
#endif

void debug(void)
{
    char input[80],c;
    CPTR nextpc,nxdis,nxmem;

#ifndef BASILISK
    bogusframe = 1;
#endif
    
    if (do_skip && (m68k_getpc() != skipaddr/* || regs.a[0] != 0x1e558*/)) {
	regs.spcflags |= SPCFLAG_BRK;
	return;
    }
    do_skip = 0;

#ifdef NEED_TO_DEBUG_BADLY
    history[lasthist] = regs;
    historyf[lasthist] = regflags;
#else
    history[lasthist] = m68k_getpc();
#endif
    if (++lasthist == MAX_HIST) lasthist = 0;
    if (lasthist == firsthist) {
	if (++firsthist == MAX_HIST) firsthist = 0;
    }

    m68k_dumpstate(&nextpc);
    nxdis = nextpc; nxmem = 0;

    for(;;){
	char cmd,*inptr;
	
	printf(">");
	fflush (stdout);
	if (fgets(input, 80, stdin) == 0)
	    return;
	inptr = input;
	cmd = next_char(&inptr);
	switch(cmd){
	 case 'q': quit_program = 1; debugging = 0; regs.spcflags |= SPCFLAG_BRK; return;
#ifndef BASILISK
	 case 'c': dumpcia(); dumpcustom(); break;
#endif
	 case 'r': m68k_dumpstate(&nextpc); break;
#ifndef BASILISK
	 case 'M': modulesearch(); break;
#endif
	 case 'd': 
	    {
		ULONG daddr;
		int count;
		
		if (more_params(&inptr))
		    daddr = readhex(&inptr);
		else 
		    daddr = nxdis;
		if (more_params(&inptr))
		    count = readhex(&inptr); 
		else
		    count = 10;
		m68k_disasm(daddr, &nxdis, count);
	    }
	    break;
	 case 't': regs.spcflags |= SPCFLAG_BRK; return;
	 case 'z':
	    skipaddr = nextpc;
	    do_skip = 1;
	    regs.spcflags |= SPCFLAG_BRK;
	    return;

	 case 'f': 
	    skipaddr = readhex(&inptr);
	    do_skip = 1;
	    regs.spcflags |= SPCFLAG_BRK;
	    return;

	 case 'g':
	    if (more_params (&inptr))
		m68k_setpc (readhex (&inptr));
	    debugger_active = 0;
	    debugging = 0;
	    return;

	 case 'H':
	    {
		int count;
		int temp;
#ifdef NEED_TO_DEBUG_BADLY
		struct regstruct save_regs = regs;
		union flagu save_flags = regflags;
#endif

	        if (more_params(&inptr))
		    count = readhex(&inptr); 
	        else
		    count = 10;
	        if (count < 0)
		    break;
	        temp = lasthist;
	        while (count-- > 0 && temp != firsthist) {
		    if (temp == 0) temp = MAX_HIST-1; else temp--;
	        }
	        while (temp != lasthist) {
#ifdef NEED_TO_DEBUG_BADLY
		    regs = history[temp];
		    regflags = historyf[temp];
		    m68k_dumpstate(NULL);
#else
		    m68k_disasm(history[temp], NULL, 1);
#endif
		    if (++temp == MAX_HIST) temp = 0;
	        }
#ifdef NEED_TO_DEBUG_BADLY
		regs = save_regs;
		regflags = save_flags;
#endif
	    }
	    break;
	 case 'm':
	    {
		ULONG maddr; int lines;
		if (more_params(&inptr))
		    maddr = readhex(&inptr); 
		else 
		    maddr = nxmem;
		if (more_params(&inptr))
		    lines = readhex(&inptr); 
		else 
		    lines = 16;
		dumpmem(maddr, &nxmem, lines);
	    }
	    break;
          case 'h':
          case '?':
	    {
#ifndef BASILISK
             printf ("          HELP for UAE Debugger\n");
             printf ("         -----------------------\n\n");
#else
             printf ("          HELP for Basilisk Debugger\n");
             printf ("         ----------------------------\n\n");
#endif
             printf ("  g: <address>          ");
                printf("Start execution at the current address or <address>\n");
#ifndef BASILISK
             printf ("  c:                    ");
                printf("Dump state of the CIA and custom chips\n");
#endif
             printf ("  r:                    ");
                printf("Dump state of the CPU\n");
             printf ("  m <address> <lines>:  ");
                printf ("Memory dump starting at <address>\n");
             printf ("  d <address> <lines>:  ");
                printf ("Disassembly starting at <address>\n");
             printf ("  t:                    ");
                printf ("Step one instruction\n");
             printf ("  z:                    ");
                printf ("Step through one instruction - useful for JSR, DBRA etc\n");
             printf ("  f <address>:          ");
                printf ("Step forward until PC == <address>\n");
             printf ("  H <count>:            ");
                printf ("Show PC history <count> instructions\n");
#ifndef BASILISK
             printf ("  M:                   ");
                printf ("Search for *Tracker sound modules\n");
#endif
             printf ("  h,?:                  ");
                printf ("Show this help page\n");
             printf ("  q:                    ");
                printf ("Quit the emulator. You don't want to use this command.\n\n");
            }
            break;
             
	}
    }
}
