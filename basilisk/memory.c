/*
 *  memory.c - Memory management
 *
 *  Basilisk (C) 1996-1997 Christian Bauer
 */

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"
#include "memory.h"
#include "zfile.h"
#include "patches.h"
#include "readcpu.h"
#include "newcpu.h"
#include "compiler.h"

#ifdef USE_MAPPED_MEMORY
#include <sys/mman.h>
#endif

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
    fprintf(stderr, "Your Mac program just did something terribly stupid\n");
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

/* Main memory */

UBYTE *mainmemory;

static ULONG mainmem_alget(CPTR) REGPARAM;
static ULONG mainmem_awget(CPTR) REGPARAM;

static ULONG mainmem_lget(CPTR) REGPARAM;
static ULONG mainmem_wget(CPTR) REGPARAM;
static ULONG mainmem_bget(CPTR) REGPARAM;
static void  mainmem_lput(CPTR, ULONG) REGPARAM;
static void  mainmem_wput(CPTR, ULONG) REGPARAM;
static void  mainmem_bput(CPTR, ULONG) REGPARAM;

static int   mainmem_check(CPTR addr, ULONG size) REGPARAM;
static UBYTE *mainmem_xlate(CPTR addr) REGPARAM;

ULONG REGPARAM2 mainmem_alget(CPTR addr)
{
    ULONG *m;
    addr -= mainmem_start & mainmem_mask;
    addr &= mainmem_mask;
    m = (ULONG *)(mainmemory + addr);
    return do_get_mem_long(m);
}

ULONG REGPARAM2 mainmem_awget(CPTR addr)
{
    UWORD *m;
    addr -= mainmem_start & mainmem_mask;
    addr &= mainmem_mask;
    m = (UWORD *)(mainmemory + addr);
    return do_get_mem_word(m);
}

ULONG REGPARAM2 mainmem_lget(CPTR addr)
{
    ULONG *m;
    addr -= mainmem_start & mainmem_mask;
    addr &= mainmem_mask;
    m = (ULONG *)(mainmemory + addr);
    return do_get_mem_long(m);
}

ULONG REGPARAM2 mainmem_wget(CPTR addr)
{
    UWORD *m;
    addr -= mainmem_start & mainmem_mask;
    addr &= mainmem_mask;
    m = (UWORD *)(mainmemory + addr);
    return do_get_mem_word(m);
}

ULONG REGPARAM2 mainmem_bget(CPTR addr)
{
    addr -= mainmem_start & mainmem_mask;
    addr &= mainmem_mask;
    return mainmemory[addr];
}

void REGPARAM2 mainmem_lput(CPTR addr, ULONG l)
{
    ULONG *m;
    addr -= mainmem_start & mainmem_mask;
    addr &= mainmem_mask;
    m = (ULONG *)(mainmemory + addr);
    do_put_mem_long(m, l);
}

void REGPARAM2 mainmem_wput(CPTR addr, ULONG w)
{
    UWORD *m;
    addr -= mainmem_start & mainmem_mask;
    addr &= mainmem_mask;
    m = (UWORD *)(mainmemory + addr);
    do_put_mem_word(m, w);
}

void REGPARAM2 mainmem_bput(CPTR addr, ULONG b)
{
    addr -= mainmem_start & mainmem_mask;
    addr &= mainmem_mask;
    mainmemory[addr] = b;
}

int REGPARAM2 mainmem_check(CPTR addr, ULONG size)
{
    addr -= mainmem_start & mainmem_mask;
    addr &= mainmem_mask;
    return (addr + size) < mainmem_size;
}

UBYTE REGPARAM2 *mainmem_xlate(CPTR addr)
{
    addr -= mainmem_start & mainmem_mask;
    addr &= mainmem_mask;
    return mainmemory + addr;
}

/* ROM memory */

static UBYTE *rommemory;

static ULONG rommem_alget(CPTR) REGPARAM;
static ULONG rommem_awget(CPTR) REGPARAM;
static ULONG rommem_lget(CPTR) REGPARAM;
static ULONG rommem_wget(CPTR) REGPARAM;
static ULONG rommem_bget(CPTR) REGPARAM;
static void  rommem_lput(CPTR, ULONG) REGPARAM;
static void  rommem_wput(CPTR, ULONG) REGPARAM;
static void  rommem_bput(CPTR, ULONG) REGPARAM;
static int   rommem_check(CPTR addr, ULONG size) REGPARAM;
static UBYTE *rommem_xlate(CPTR addr) REGPARAM;

ULONG REGPARAM2 rommem_alget(CPTR addr)
{
    ULONG *m;
    addr -= rommem_start & rommem_mask;
    addr &= rommem_mask;
    m = (ULONG *)(rommemory + addr);
    return do_get_mem_long(m); 
}

ULONG REGPARAM2 rommem_awget(CPTR addr)
{
    UWORD *m;
    addr -= rommem_start & rommem_mask;
    addr &= rommem_mask;
    m = (UWORD *)(rommemory + addr);
    return do_get_mem_word(m); 
}

ULONG REGPARAM2 rommem_lget(CPTR addr)
{
    ULONG *m;
    addr -= rommem_start & rommem_mask;
    addr &= rommem_mask;
    m = (ULONG *)(rommemory + addr);
    return do_get_mem_long(m); 
}

ULONG REGPARAM2 rommem_wget(CPTR addr)
{
    UWORD *m;
    addr -= rommem_start & rommem_mask;
    addr &= rommem_mask;
    m = (UWORD *)(rommemory + addr);
    return do_get_mem_word(m); 
}

ULONG REGPARAM2 rommem_bget(CPTR addr)
{
    addr -= rommem_start & rommem_mask;
    addr &= rommem_mask;
    return rommemory[addr];
}

void REGPARAM2 rommem_lput(CPTR addr, ULONG b)
{
    if (illegal_mem) printf("Illegal rommem lput at %08lx pc=%08lx\n",addr,m68k_getpc());
}

void REGPARAM2 rommem_wput(CPTR addr, ULONG b)
{
    if (illegal_mem) printf("Illegal rommem wput at %08lx pc=%08lx\n",addr,m68k_getpc());
}

void REGPARAM2 rommem_bput(CPTR addr, ULONG b)
{
    if (illegal_mem) printf("Illegal rommem lput at %08lx pc=%08lx\n",addr,m68k_getpc());
}

int REGPARAM2 rommem_check(CPTR addr, ULONG size)
{
    addr -= rommem_start & rommem_mask;
    addr &= rommem_mask;
    return (addr + size) < rommem_size;
}

UBYTE REGPARAM2 *rommem_xlate(CPTR addr)
{
    addr -= rommem_start & rommem_mask;
    addr &= rommem_mask;
    return rommemory + addr;
}

/* Emulator private memory */

static UBYTE *emulmemory;

static ULONG emulmem_alget(CPTR) REGPARAM;
static ULONG emulmem_awget(CPTR) REGPARAM;

static ULONG emulmem_lget(CPTR) REGPARAM;
static ULONG emulmem_wget(CPTR) REGPARAM;
static ULONG emulmem_bget(CPTR) REGPARAM;
static void  emulmem_lput(CPTR, ULONG) REGPARAM;
static void  emulmem_wput(CPTR, ULONG) REGPARAM;
static void  emulmem_bput(CPTR, ULONG) REGPARAM;

static int   emulmem_check(CPTR addr, ULONG size) REGPARAM;
static UBYTE *emulmem_xlate(CPTR addr) REGPARAM;

ULONG REGPARAM2 emulmem_alget(CPTR addr)
{
    ULONG *m;
    addr -= emulmem_start & emulmem_mask;
    addr &= emulmem_mask;
    m = (ULONG *)(emulmemory + addr);
    return do_get_mem_long(m);
}

ULONG REGPARAM2 emulmem_awget(CPTR addr)
{
    UWORD *m;
    addr -= emulmem_start & emulmem_mask;
    addr &= emulmem_mask;
    m = (UWORD *)(emulmemory + addr);
    return do_get_mem_word(m);
}

ULONG REGPARAM2 emulmem_lget(CPTR addr)
{
    ULONG *m;
    addr -= emulmem_start & emulmem_mask;
    addr &= emulmem_mask;
    m = (ULONG *)(emulmemory + addr);
    return do_get_mem_long(m);
}

ULONG REGPARAM2 emulmem_wget(CPTR addr)
{
    UWORD *m;
    addr -= emulmem_start & emulmem_mask;
    addr &= emulmem_mask;
    m = (UWORD *)(emulmemory + addr);
    return do_get_mem_word(m);
}

ULONG REGPARAM2 emulmem_bget(CPTR addr)
{
    addr -= emulmem_start & emulmem_mask;
    addr &= emulmem_mask;
    return emulmemory[addr];
}

void REGPARAM2 emulmem_lput(CPTR addr, ULONG l)
{
    ULONG *m;
    addr -= emulmem_start & emulmem_mask;
    addr &= emulmem_mask;
    m = (ULONG *)(emulmemory + addr);
    do_put_mem_long(m, l);
}

void REGPARAM2 emulmem_wput(CPTR addr, ULONG w)
{
    UWORD *m;
    addr -= emulmem_start & emulmem_mask;
    addr &= emulmem_mask;
    m = (UWORD *)(emulmemory + addr);
    do_put_mem_word(m, w);
}

void REGPARAM2 emulmem_bput(CPTR addr, ULONG b)
{
    addr -= emulmem_start & emulmem_mask;
    addr &= emulmem_mask;
    emulmemory[addr] = b;
}

int REGPARAM2 emulmem_check(CPTR addr, ULONG size)
{
    addr -= emulmem_start & emulmem_mask;
    addr &= emulmem_mask;
    return (addr + size) < emulmem_size;
}

UBYTE REGPARAM2 *emulmem_xlate(CPTR addr)
{
    addr -= emulmem_start & emulmem_mask;
    addr &= emulmem_mask;
    return emulmemory + addr;
}


static int load_rom(void)
{
	int i;
	FILE *f = zfile_open(romfile, "rb");

    if (f == NULL) {	
    	fprintf(stderr, "No Macintosh ROM found.\n");
	return 0;
    }

    i = fread(rommemory, 1, rommem_size, f);
	if (i != rommem_size) {
		fprintf(stderr, "Error while reading ROM.\n");
		return 0;
    }
    zfile_close(f);
    return 1;
}

/* Address banks */

addrbank dummy_bank = {
    default_alget, default_awget,
    dummy_lget, dummy_wget, dummy_bget,
    dummy_lput, dummy_wput, dummy_bput,
    default_xlate, dummy_check
};

addrbank mainmem_bank = {
    mainmem_alget, mainmem_awget,
    mainmem_lget, mainmem_wget, mainmem_bget,
    mainmem_lput, mainmem_wput, mainmem_bput,
    mainmem_xlate, mainmem_check
};

addrbank rommem_bank = {
    rommem_alget, rommem_awget,
    rommem_lget, rommem_wget, rommem_bget,
    rommem_lput, rommem_wput, rommem_bput,
    rommem_xlate, rommem_check
};

addrbank emulmem_bank = {
    emulmem_alget, emulmem_awget,
    emulmem_lget, emulmem_wget, emulmem_bget,
    emulmem_lput, emulmem_wput, emulmem_bput,
    emulmem_xlate, emulmem_check
};

void memory_init(void)
{
	int i;
	buserr = 0;

	mainmemory = malloc(mainmem_size);
	rommemory = malloc(rommem_size);
	emulmemory = malloc(emulmem_size);

	memset(mainmemory, 0, mainmem_size);
	memset(emulmemory, 0, emulmem_size);

	load_rom();
	patch_rom(rommemory);

	for(i = 0; i < 65536; i++)
		membanks[i] = dummy_bank;

	map_banks(&mainmem_bank, mainmem_start >> 16, mainmem_size >> 16);
	map_banks(&rommem_bank, rommem_start >> 16, rommem_size >> 16);
	map_banks(&emulmem_bank, emulmem_start >> 16, emulmem_size >> 16);
	map_banks(&via_bank, 0xef, 0x01);
}

void map_banks(addrbank *bank, int start, int size)
{
	int bnr;
	int hioffs = 0;
//always 68k?
#if CPU_LEVEL < 2
	for (hioffs = 0; hioffs < 256; hioffs++)
#endif
		for (bnr = start; bnr < start+size; bnr++) 
			membanks[bnr + hioffs * 256] = *bank;
}
