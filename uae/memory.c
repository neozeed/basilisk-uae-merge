 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Memory management
  *
  * (c) 1995 Bernd Schmidt
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"
#include "memory.h"
#include "custom.h"
#include "cia.h"
#include "ersatz.h"
#include "zfile.h"
#include "events.h"
#include "readcpu.h"
#include "newcpu.h"
#include "compiler.h"

#ifdef USE_MAPPED_MEMORY
#include <sys/mman.h>
#endif

int ersatzkickfile = 0;

int buserr;
addrbank membanks[65536];

#ifdef NO_INLINE_MEMORY_ACCESS
__inline__ ULONG alongget(CPTR addr)
{
    return call_mem_get_func(membanks[bankindex(addr)].alget, addr);
}
__inline__ ULONG awordget(CPTR addr)
{
    return call_mem_get_func(membanks[bankindex(addr)].awget, addr);
}
__inline__ ULONG longget(CPTR addr)
{
    return call_mem_get_func(membanks[bankindex(addr)].lget, addr);
}
__inline__ ULONG wordget(CPTR addr)
{
    return call_mem_get_func(membanks[bankindex(addr)].wget, addr);
}
__inline__ ULONG byteget(CPTR addr) 
{
    return call_mem_get_func(membanks[bankindex(addr)].bget, addr);
}
__inline__ void longput(CPTR addr, ULONG l)
{
    call_mem_put_func(membanks[bankindex(addr)].lput, addr, l);
}
__inline__ void wordput(CPTR addr, ULONG w)
{
    call_mem_put_func(membanks[bankindex(addr)].wput, addr, w);
}
__inline__ void byteput(CPTR addr, ULONG b)
{
    call_mem_put_func(membanks[bankindex(addr)].bput, addr, b);
}
#endif

/* Default memory access functions */

int REGPARAM2 default_check(CPTR a, ULONG b)
{
    return 0;
}

UBYTE REGPARAM2 *default_xlate(CPTR a)
{
    fprintf(stderr, "Your Amiga program just did something terribly stupid\n");
    return 0;
}

ULONG REGPARAM2 default_awget(CPTR addr)
{
    default_xlate(addr);
    return 0;
}

ULONG REGPARAM2 default_alget(CPTR addr)
{
    default_xlate(addr);
    return 0;
}

/* A dummy bank that only contains zeros */

static ULONG dummy_lget(CPTR) REGPARAM;
static ULONG dummy_wget(CPTR) REGPARAM;
static ULONG dummy_bget(CPTR) REGPARAM;
static void  dummy_lput(CPTR, ULONG) REGPARAM;
static void  dummy_wput(CPTR, ULONG) REGPARAM;
static void  dummy_bput(CPTR, ULONG) REGPARAM;
static int   dummy_check(CPTR addr, ULONG size) REGPARAM;

ULONG REGPARAM2 dummy_lget(CPTR addr)
{
    if (illegal_mem) printf("Illegal lget at %08lx pc=%08lx\n",addr,m68k_getpc());
    return 0;
}

ULONG REGPARAM2 dummy_wget(CPTR addr)
{
    if (illegal_mem) printf("Illegal wget at %08lx pc=%08lx\n",addr,m68k_getpc());
    return 0;
}

ULONG REGPARAM2 dummy_bget(CPTR addr)
{
    if (illegal_mem) printf("Illegal bget at %08lx pc=%08lx\n",addr,m68k_getpc());
    return 0;
}

void REGPARAM2 dummy_lput(CPTR addr, ULONG l)
{
    if (illegal_mem) printf("Illegal lput at %08lx pc=%08lx\n",addr,m68k_getpc());
}
void REGPARAM2 dummy_wput(CPTR addr, ULONG w)
{
    if (illegal_mem) printf("Illegal wput at %08lx pc=%08lx\n",addr,m68k_getpc());
}
void REGPARAM2 dummy_bput(CPTR addr, ULONG b)
{
    if (illegal_mem) printf("Illegal bput at %08lx pc=%08lx\n",addr,m68k_getpc());
}

int REGPARAM2 dummy_check(CPTR addr, ULONG size)
{
    if (illegal_mem) printf("Illegal check at %08lx pc=%08lx\n",addr,m68k_getpc());
    return 0;
}

/* Chip memory */

ULONG chipmem_mask,kickmem_mask,bogomem_mask;

UBYTE *chipmemory;

static ULONG chipmem_alget(CPTR) REGPARAM;
static ULONG chipmem_awget(CPTR) REGPARAM;

static ULONG chipmem_lget(CPTR) REGPARAM;
static ULONG chipmem_wget(CPTR) REGPARAM;
static ULONG chipmem_bget(CPTR) REGPARAM;
static void  chipmem_lput(CPTR, ULONG) REGPARAM;
static void  chipmem_wput(CPTR, ULONG) REGPARAM;
static void  chipmem_bput(CPTR, ULONG) REGPARAM;

static int   chipmem_check(CPTR addr, ULONG size) REGPARAM;
static UBYTE *chipmem_xlate(CPTR addr) REGPARAM;

ULONG REGPARAM2 chipmem_alget(CPTR addr)
{
    ULONG *m;
    addr -= chipmem_start & chipmem_mask;
    addr &= chipmem_mask;
    m = (ULONG *)(chipmemory + addr);
    return do_get_mem_long(m);
}

ULONG REGPARAM2 chipmem_awget(CPTR addr)
{
    UWORD *m;
    addr -= chipmem_start & chipmem_mask;
    addr &= chipmem_mask;
    m = (UWORD *)(chipmemory + addr);
    return do_get_mem_word(m);
}

ULONG REGPARAM2 chipmem_lget(CPTR addr)
{
    ULONG *m;
    addr -= chipmem_start & chipmem_mask;
    addr &= chipmem_mask;
    m = (ULONG *)(chipmemory + addr);
    return do_get_mem_long(m);
}

ULONG REGPARAM2 chipmem_wget(CPTR addr)
{
    UWORD *m;
    addr -= chipmem_start & chipmem_mask;
    addr &= chipmem_mask;
    m = (UWORD *)(chipmemory + addr);
    return do_get_mem_word(m);
}

ULONG REGPARAM2 chipmem_bget(CPTR addr)
{
    addr -= chipmem_start & chipmem_mask;
    addr &= chipmem_mask;
    return chipmemory[addr];
}

void REGPARAM2 chipmem_lput(CPTR addr, ULONG l)
{
    ULONG *m;
    addr -= chipmem_start & chipmem_mask;
    addr &= chipmem_mask;
    m = (ULONG *)(chipmemory + addr);
    do_put_mem_long(m, l);
}

void REGPARAM2 chipmem_wput(CPTR addr, ULONG w)
{
    UWORD *m;
    addr -= chipmem_start & chipmem_mask;
    addr &= chipmem_mask;
    m = (UWORD *)(chipmemory + addr);
    do_put_mem_word(m, w);
}

void REGPARAM2 chipmem_bput(CPTR addr, ULONG b)
{
    addr -= chipmem_start & chipmem_mask;
    addr &= chipmem_mask;
    chipmemory[addr] = b;
}

int REGPARAM2 chipmem_check(CPTR addr, ULONG size)
{
    addr -= chipmem_start & chipmem_mask;
    addr &= chipmem_mask;
    return (addr + size) < chipmem_size;
}

UBYTE REGPARAM2 *chipmem_xlate(CPTR addr)
{
    addr -= chipmem_start & chipmem_mask;
    addr &= chipmem_mask;
    return chipmemory + addr;
}

/* Slow memory */

static UBYTE *bogomemory;

static ULONG bogomem_alget(CPTR) REGPARAM;
static ULONG bogomem_awget(CPTR) REGPARAM;
static ULONG bogomem_lget(CPTR) REGPARAM;
static ULONG bogomem_wget(CPTR) REGPARAM;
static ULONG bogomem_bget(CPTR) REGPARAM;
static void  bogomem_lput(CPTR, ULONG) REGPARAM;
static void  bogomem_wput(CPTR, ULONG) REGPARAM;
static void  bogomem_bput(CPTR, ULONG) REGPARAM;
static int  bogomem_check(CPTR addr, ULONG size) REGPARAM;
static UBYTE *bogomem_xlate(CPTR addr) REGPARAM;

ULONG REGPARAM2 bogomem_alget(CPTR addr)
{
    ULONG *m;
    addr -= bogomem_start & bogomem_mask;
    addr &= bogomem_mask;
    m = (ULONG *)(bogomemory + addr);
    return do_get_mem_long(m);
}

ULONG REGPARAM2 bogomem_awget(CPTR addr)
{
    UWORD *m;
    addr -= bogomem_start & bogomem_mask;
    addr &= bogomem_mask;
    m = (UWORD *)(bogomemory + addr);
    return do_get_mem_word(m);
}

ULONG REGPARAM2 bogomem_lget(CPTR addr)
{
    ULONG *m;
    addr -= bogomem_start & bogomem_mask;
    addr &= bogomem_mask;
    m = (ULONG *)(bogomemory + addr);
    return do_get_mem_long(m);
}

ULONG REGPARAM2 bogomem_wget(CPTR addr)
{
    UWORD *m;
    addr -= bogomem_start & bogomem_mask;
    addr &= bogomem_mask;
    m = (UWORD *)(bogomemory + addr);
    return do_get_mem_word(m);
}

ULONG REGPARAM2 bogomem_bget(CPTR addr)
{
    addr -= bogomem_start & bogomem_mask;
    addr &= bogomem_mask;
    return bogomemory[addr];
}

void REGPARAM2 bogomem_lput(CPTR addr, ULONG l)
{
    ULONG *m;
    addr -= bogomem_start & bogomem_mask;
    addr &= bogomem_mask;
    m = (ULONG *)(bogomemory + addr);
    do_put_mem_long(m, l);
}

void REGPARAM2 bogomem_wput(CPTR addr, ULONG w)
{
    UWORD *m;
    addr -= bogomem_start & bogomem_mask;
    addr &= bogomem_mask;
    m = (UWORD *)(bogomemory + addr);
    do_put_mem_word(m, w);
}

void REGPARAM2 bogomem_bput(CPTR addr, ULONG b)
{
    addr -= bogomem_start & bogomem_mask;
    addr &= bogomem_mask;
    bogomemory[addr] = b;
}

int REGPARAM2 bogomem_check(CPTR addr, ULONG size)
{
    addr -= bogomem_start & bogomem_mask;
    addr &= bogomem_mask;
    return (addr + size) < bogomem_size;
}

UBYTE REGPARAM2 *bogomem_xlate(CPTR addr)
{
    addr -= bogomem_start & bogomem_mask;
    addr &= bogomem_mask;
    return bogomemory + addr;
}

/* Kick memory */

static int zkickfile = 0;
UBYTE *kickmemory;

static ULONG kickmem_alget(CPTR) REGPARAM;
static ULONG kickmem_awget(CPTR) REGPARAM;
static ULONG kickmem_lget(CPTR) REGPARAM;
static ULONG kickmem_wget(CPTR) REGPARAM;
static ULONG kickmem_bget(CPTR) REGPARAM;
static void  kickmem_lput(CPTR, ULONG) REGPARAM;
static void  kickmem_wput(CPTR, ULONG) REGPARAM;
static void  kickmem_bput(CPTR, ULONG) REGPARAM;
static int  kickmem_check(CPTR addr, ULONG size) REGPARAM;
static UBYTE *kickmem_xlate(CPTR addr) REGPARAM;

ULONG REGPARAM2 kickmem_alget(CPTR addr)
{
    ULONG *m;
    addr -= kickmem_start & kickmem_mask;
    addr &= kickmem_mask;
    m = (ULONG *)(kickmemory + addr);
    return do_get_mem_long(m); 
}

ULONG REGPARAM2 kickmem_awget(CPTR addr)
{
    UWORD *m;
    addr -= kickmem_start & kickmem_mask;
    addr &= kickmem_mask;
    m = (UWORD *)(kickmemory + addr);
    return do_get_mem_word(m); 
}

ULONG REGPARAM2 kickmem_lget(CPTR addr)
{
    ULONG *m;
    addr -= kickmem_start & kickmem_mask;
    addr &= kickmem_mask;
    m = (ULONG *)(kickmemory + addr);
    return do_get_mem_long(m); 
}

ULONG REGPARAM2 kickmem_wget(CPTR addr)
{
    UWORD *m;
    addr -= kickmem_start & kickmem_mask;
    addr &= kickmem_mask;
    m = (UWORD *)(kickmemory + addr);
    return do_get_mem_word(m); 
}

ULONG REGPARAM2 kickmem_bget(CPTR addr)
{
    addr -= kickmem_start & kickmem_mask;
    addr &= kickmem_mask;
    return kickmemory[addr];
}

void REGPARAM2 kickmem_lput(CPTR addr, ULONG b)
{
    if (illegal_mem) printf("Illegal kickmem lput at %08lx pc=%08lx\n",addr,m68k_getpc());
}

void REGPARAM2 kickmem_wput(CPTR addr, ULONG b)
{
    if (illegal_mem) printf("Illegal kickmem wput at %08lx pc=%08lx\n",addr,m68k_getpc());
}

void REGPARAM2 kickmem_bput(CPTR addr, ULONG b)
{
    if (illegal_mem) printf("Illegal kickmem lput at %08lx pc=%08lx\n",addr,m68k_getpc());
}

int REGPARAM2 kickmem_check(CPTR addr, ULONG size)
{
    addr -= kickmem_start & kickmem_mask;
    addr &= kickmem_mask;
    return (addr + size) < kickmem_size;
}

UBYTE REGPARAM2 *kickmem_xlate(CPTR addr)
{
    addr -= kickmem_start & kickmem_mask;
    addr &= kickmem_mask;
    return kickmemory + addr;
}

static int load_kickstart(void)
{
    int i;
    ULONG cksum = 0, prevck = 0;
    unsigned char buffer[8];
    
    FILE *f = zfile_open(romfile, "rb");
    
    if (f == NULL) {	
    	fprintf(stderr, "No Kickstart ROM found.\n");
#if defined(AMIGA)
#define USE_UAE_ERSATZ "USE_UAE_ERSATZ"
	fprintf(stderr, "Using current ROM. (create ENV:%s to "
		"use uae's ROM replacement)\n",USE_UAE_ERSATZ);
	if(!getenv(USE_UAE_ERSATZ)) {
	    memcpy(kickmemory,(char*)0x1000000-kickmem_size,kickmem_size);
	    goto chk_sum;
	}
#endif
	return 0;
    }
    
    fread(buffer, 1, 8, f);
    if (buffer[4] == 0 && buffer[5] == 8 && buffer[6] == 0 && buffer[7] == 0) {
	fprintf(stderr, "You seem to have a ZKick file there... You probably lose.\n");
	zkickfile = 1;
    } else 
	fseek(f, 0, SEEK_SET);
    
    i = fread(kickmemory, 1, kickmem_size, f);
    if (i == kickmem_size/2) {
/*	fprintf(stderr, "Warning: Kickstart is only 256K.\n"); */
	memcpy (kickmemory + kickmem_size/2, kickmemory, kickmem_size/2);
    } else if (i != kickmem_size) {
	fprintf(stderr, "Error while reading Kickstart.\n");
	return 0;
    }
    zfile_close (f);
    
#if defined(AMIGA)
    chk_sum:
#endif

    for (i = 0; i < kickmem_size; i+=4) {
	ULONG data = kickmemory[i]*65536*256 + kickmemory[i+1]*65536 + kickmemory[i+2]*256 + kickmemory[i+3];
	cksum += data;
	if (cksum < prevck)
	    cksum++;
	prevck = cksum;
    }
    if (cksum != 0xFFFFFFFFul) {
	fprintf(stderr, "Warning: Kickstart checksum incorrect. You probably have a corrupted ROM image.\n");
    }
    return 1;
}

/* Address banks */

addrbank dummy_bank = {
    default_alget, default_awget,
    dummy_lget, dummy_wget, dummy_bget,
    dummy_lput, dummy_wput, dummy_bput,
    default_xlate, dummy_check
};

addrbank chipmem_bank = {
    chipmem_alget, chipmem_awget,
    chipmem_lget, chipmem_wget, chipmem_bget,
    chipmem_lput, chipmem_wput, chipmem_bput,
    chipmem_xlate, chipmem_check
};

addrbank bogomem_bank = {
    bogomem_alget, bogomem_awget,
    bogomem_lget, bogomem_wget, bogomem_bget,
    bogomem_lput, bogomem_wput, bogomem_bput,
    bogomem_xlate, bogomem_check
};

addrbank kickmem_bank = {
    kickmem_alget, kickmem_awget,
    kickmem_lget, kickmem_wget, kickmem_bget,
    kickmem_lput, kickmem_wput, kickmem_bput,
    kickmem_xlate, kickmem_check
};

char *address_space, *good_address_map;
int good_address_fd;
#define MAKE_USER_PROGRAMS_BEHAVE 1
void memory_init(void)
{
    char buffer[4096];
    char *nam;
    int i, fd;
    buserr = 0;

    chipmem_mask = chipmem_size - 1;
    kickmem_mask = kickmem_size - 1;
    bogomem_mask = bogomem_size - 1;
    
#ifdef USE_MAPPED_MEMORY
    fd = open("/dev/zero", O_RDWR);
    good_address_map = mmap(NULL, 1 << 24, PROT_READ, MAP_PRIVATE, fd, 0);
    /* Don't believe USER_PROGRAMS_BEHAVE. Otherwise, we'd segfault as soon
     * as a decrunch routine tries to do color register hacks. */
    address_space = mmap(NULL, 1 << 24, PROT_READ | (USER_PROGRAMS_BEHAVE || MAKE_USER_PROGRAMS_BEHAVE? PROT_WRITE : 0), MAP_PRIVATE, fd, 0);
    if ((int)address_space < 0 || (int)good_address_map < 0) {
	fprintf(stderr, "Your system does not have enough virtual memory - increase swap.\n");
	abort();
    }
#ifdef MAKE_USER_PROGRAMS_BEHAVE
    memset(address_space + 0xDFF180, 0xFF, 32*2);
#else
    /* Likewise. This is mostly for mouse button checks. */
    if (USER_PROGRAMS_BEHAVE)
	memset(address_space + 0xA00000, 0xFF, 0xF00000 - 0xA00000);
#endif
    chipmemory = mmap(address_space, 0x200000, PROT_READ|PROT_WRITE, MAP_PRIVATE | MAP_FIXED, fd, 0);
    kickmemory = mmap(address_space + 0xF80000, 0x80000, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_FIXED, fd, 0);

    close(fd);

    good_address_fd = open(nam = tmpnam(NULL), O_CREAT|O_RDWR, 0600);
    memset(buffer,1,sizeof(buffer));
    write(good_address_fd, buffer,sizeof(buffer));
    unlink(nam);

    for (i = 0; i < chipmem_size; i += 4096)
	mmap(good_address_map + i, 4096, PROT_READ, MAP_FIXED | MAP_PRIVATE,
	     good_address_fd, 0);
    for (i = 0; i < kickmem_size; i += 4096)
	mmap(good_address_map + i + 0x1000000 - kickmem_size, 4096, PROT_READ,
	     MAP_FIXED | MAP_PRIVATE, good_address_fd, 0);
#else
    chipmemory = (UBYTE *)malloc(chipmem_size);
    kickmemory = (UBYTE *)malloc(kickmem_size);
    if(!chipmemory || !kickmemory) {
       fprintf(stderr,"virtual memory exhausted !\n");
       abort();
    }
#endif

    for(i = 0; i < 65536; i++)
	membanks[i] = dummy_bank;
    
    /* Map the chipmem into all of the lower 16MB */
    map_banks(&chipmem_bank, 0x00, 256);
    map_banks(&custom_bank, 0xC0, 0x20);
    map_banks(&cia_bank, 0xA0, 32);
    map_banks(&clock_bank, 0xDC, 1);
    
    if (bogomem_size > 0) {
	bogomemory = (UBYTE *)malloc(bogomem_size);
	if(!bogomemory) {
	    fprintf(stderr,"virtual memory exhausted !\n");
	    abort();
	}
    	map_banks(&bogomem_bank, 0xC0, bogomem_size >> 16);
    }

    map_banks(&rtarea_bank, 0xF0, 1); 
    if (!load_kickstart()) {
	init_ersatz_rom(kickmemory);
	ersatzkickfile = 1;
    }
    if (zkickfile)
	map_banks(&kickmem_bank, 0x20, 8);
    
    map_banks(&kickmem_bank, 0xF8, 8);
    if (!zkickfile)
	map_banks(&expamem_bank, 0xE8, 1);
}

void map_banks(addrbank *bank, int start, int size)
{
    int bnr;
    int hioffs = 0;
#if 1 || CPU_LEVEL < 2
    for (hioffs = 0; hioffs < 256; hioffs++)
#endif
	for (bnr = start; bnr < start+size; bnr++) 
	    membanks[bnr + hioffs * 256] = *bank;
}
