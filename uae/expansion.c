 /* 
  * UAE - The Un*x Amiga Emulator
  * 
  *  Expansion Slots
  *
  * Copyright 1996 Stefan Reinauer <stepan@matrix.kommune.schokola.de>
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"
#include "memory.h"
#include "custom.h"
#include "readcpu.h"
#include "newcpu.h"
#include "autoconf.h"

#define MAX_EXPANSION_BOARDS 5

/* 00 / 02 */

#define MEM_8MB    0x00        /* Size of Memory Block           */
#define MEM_4MB    0x07
#define MEM_2MB    0x06
#define MEM_1MB    0x05
#define MEM_512KB  0x04
#define MEM_256KB  0x03
#define MEM_128KB  0x02
#define MEM_64KB   0x01
 
#define same_slot  0x08     /* Next card is in the same Slot  */
#define rom_card   0x10     /* Card has valid ROM             */
#define add_memory 0x20     /* Add Memory to List of Free Ram */

#define generic    0xc0     /* Type of Expansion Card         */
#define future1    0x00
#define future2    0x40
#define future3    0x80

/* ********************************************************** */

/* Card Data */

/* 04 - 06 & 10-16 */
#define commodore_g   513  /* Commodore Braunschweig (Germany) */
#define commodore     514  /* Commodore West Chester           */
#define gvp	     2017  /* GVP */
#define hackers_id   2011

#define   commodore_a2091         3  /* A2091 / A590 Card from C=   */
#define   commodore_a2091_ram    10  /* A2091 / A590 Ram on HD-Card */
#define   commodore_a2232        70  /* A2232 Multiport Expansion   */

#define   gvp_series_2_scsi      11
#define   gvp_iv_24_gfx          32

/* ********************************************************** */
/* 08-0A */

#define no_shutup  64  /* Card cannot receive Shut_up_forever */
#define care_addr 128  /* Adress HAS to be $200000-$9fffff    */

/* ********************************************************** */

/* 40-42 */
#define enable_irq   1  /* enable Interrupt                   */ 
#define reset_card   4  /* Reset of Expansion Card            */
#define card_int2   16  /* READ ONLY: IRQ 2 occured           */
#define card_irq6   32  /* READ ONLY: IRQ 6 occured           */
#define card_irq7   64  /* READ ONLY: IRQ 7 occured           */
#define does_irq   128  /* READ ONLY: Karte loest ger. IRQ aus*/

/* ********************************************************** */

/* ROM defines */

#define rom_4bit    (0x00<<14) /* ROM width */
#define rom_8bit    (0x01<<14)
#define rom_16bit   (0x02<<14)

#define rom_never   (0x00<<12) /* Never run Boot Code       */
#define rom_install (0x01<<12) /* run code at install time  */
#define rom_binddrv (0x02<<12) /* run code with binddrivers */

CPTR ROM_filesys_resname = 0, ROM_filesys_resid = 0;
CPTR ROM_filesys_diagentry = 0;
CPTR ROM_hardfile_resname = 0, ROM_hardfile_resid = 0;
CPTR ROM_hardfile_init = 0;

/* ********************************************************** */

static void expamem_init_clear(void);
static void expamem_map_clear(void);
static void expamem_init_fastcard(void);
static void expamem_map_fastcard(void);
static void expamem_init_filesys(void);
static void expamem_map_filesys(void);

void (*card_init[MAX_EXPANSION_BOARDS])(void);
void (*card_map[MAX_EXPANSION_BOARDS])(void);

int ecard = 0;

/*
 *  Fast Memory
 */

static ULONG fastmem_mask;

static ULONG fastmem_alget(CPTR) REGPARAM;
static ULONG fastmem_awget(CPTR) REGPARAM;
static ULONG fastmem_lget(CPTR) REGPARAM;
static ULONG fastmem_wget(CPTR) REGPARAM;
static ULONG fastmem_bget(CPTR) REGPARAM;
static void  fastmem_lput(CPTR, ULONG) REGPARAM;
static void  fastmem_wput(CPTR, ULONG) REGPARAM;
static void  fastmem_bput(CPTR, ULONG) REGPARAM;
static int   fastmem_check(CPTR addr, ULONG size) REGPARAM;
static UBYTE *fastmem_xlate(CPTR addr) REGPARAM;

static ULONG fastmem_start; /* Determined by the OS */
static UBYTE *fastmemory = NULL;

ULONG REGPARAM2 fastmem_alget(CPTR addr)
{
    UBYTE *m;
    addr -= fastmem_start & fastmem_mask;
    addr &= fastmem_mask;
    m = fastmemory + addr;
    return (((ULONG)m[0] << 24) + ((ULONG)m[1] << 16) 
	    + ((ULONG)m[2] << 8) + ((ULONG)m[3]));
}

ULONG REGPARAM2 fastmem_awget(CPTR addr)
{
    UBYTE *m;
    addr -= fastmem_start & fastmem_mask;
    addr &= fastmem_mask;
    m = fastmemory + addr;
    return ((UWORD)m[0] << 8) + m[1];
}

ULONG REGPARAM2 fastmem_lget(CPTR addr)
{
    UBYTE *m;
    addr -= fastmem_start & fastmem_mask;
    addr &= fastmem_mask;
    m = fastmemory + addr;
    return (((ULONG)m[0] << 24) + ((ULONG)m[1] << 16) 
	    + ((ULONG)m[2] << 8) + ((ULONG)m[3]));
}

ULONG REGPARAM2 fastmem_wget(CPTR addr)
{
    UBYTE *m;
    addr -= fastmem_start & fastmem_mask;
    addr &= fastmem_mask;
    m = fastmemory + addr;
    return ((UWORD)m[0] << 8) + m[1];
}

ULONG REGPARAM2 fastmem_bget(CPTR addr)
{
    addr -= fastmem_start & fastmem_mask;
    addr &= fastmem_mask;
    return fastmemory[addr];
}

void REGPARAM2 fastmem_lput(CPTR addr, ULONG l)
{
    UBYTE *m;
    addr -= fastmem_start & fastmem_mask;
    addr &= fastmem_mask;
    m = fastmemory + addr;
    m[0] = l >> 24;
    m[1] = l >> 16;
    m[2] = l >> 8;
    m[3] = l;
}

void REGPARAM2 fastmem_wput(CPTR addr, ULONG w)
{
    UBYTE *m;
    addr -= fastmem_start & fastmem_mask;
    addr &= fastmem_mask;
    m = fastmemory + addr;
    m[0] = w >> 8;
    m[1] = w;
}

void REGPARAM2 fastmem_bput(CPTR addr, ULONG b)
{
    addr -= fastmem_start & fastmem_mask;
    addr &= fastmem_mask;
    fastmemory[addr] = b;
}

static int REGPARAM2 fastmem_check(CPTR addr, ULONG size)
{
    addr -= fastmem_start & fastmem_mask;
    addr &= fastmem_mask;
    return (addr + size) < fastmem_size;
}

static UBYTE REGPARAM2 *fastmem_xlate(CPTR addr)
{
    addr -= fastmem_start & fastmem_mask;
    addr &= fastmem_mask;
    return fastmemory + addr;
}

addrbank fastmem_bank = {
    fastmem_alget, fastmem_awget,
    fastmem_lget, fastmem_wget, fastmem_bget,
    fastmem_lput, fastmem_wput, fastmem_bput,
    fastmem_xlate, fastmem_check
};


/*
 * Filesystem device ROM
 * This is very simple, the Amiga shouldn't be doing things with it. 
 */

static ULONG filesys_lget(CPTR) REGPARAM;
static ULONG filesys_wget(CPTR) REGPARAM;
static ULONG filesys_bget(CPTR) REGPARAM;
static void  filesys_lput(CPTR, ULONG) REGPARAM;
static void  filesys_wput(CPTR, ULONG) REGPARAM;
static void  filesys_bput(CPTR, ULONG) REGPARAM;

static ULONG filesys_start; /* Determined by the OS */
static UBYTE filesysory[65536];

ULONG REGPARAM2 filesys_lget(CPTR addr)
{
    UBYTE *m;
    addr -= filesys_start & 65535;
    addr &= 65535;
    m = filesysory + addr;
    return (((ULONG)m[0] << 24) + ((ULONG)m[1] << 16) 
	    + ((ULONG)m[2] << 8) + ((ULONG)m[3]));
}

ULONG REGPARAM2 filesys_wget(CPTR addr)
{
    UBYTE *m;
    addr -= filesys_start & 65535;
    addr &= 65535;
    m = filesysory + addr;
    return ((UWORD)m[0] << 8) + m[1];
}

ULONG REGPARAM2 filesys_bget(CPTR addr)
{
    addr -= filesys_start & 65535;
    addr &= 65535;
    return filesysory[addr];
}

static void REGPARAM2 filesys_lput(CPTR addr, ULONG l)
{
    fprintf(stderr, "filesys_lput called\n");
}

static void REGPARAM2 filesys_wput(CPTR addr, ULONG w)
{
    fprintf(stderr, "filesys_wput called\n");
}

static void REGPARAM2 filesys_bput(CPTR addr, ULONG b)
{
    fprintf(stderr, "filesys_bput called\n");
}

addrbank filesys_bank = {
    default_alget, default_awget,
    filesys_lget, filesys_wget, filesys_bget,
    filesys_lput, filesys_wput, filesys_bput,
    default_xlate, default_check
};

/* Autoconfig address space at 0xE80000 */
static UBYTE expamem[65536];

static UBYTE expamem_lo;
static UBYTE expamem_hi;

static ULONG expamem_lget(CPTR) REGPARAM;
static ULONG expamem_wget(CPTR) REGPARAM;
static ULONG expamem_bget(CPTR) REGPARAM;
static void  expamem_lput(CPTR, ULONG) REGPARAM;
static void  expamem_wput(CPTR, ULONG) REGPARAM;
static void  expamem_bput(CPTR, ULONG) REGPARAM;

addrbank expamem_bank = {
    expamem_lget, expamem_wget,
    expamem_lget, expamem_wget, expamem_bget,
    expamem_lput, expamem_wput, expamem_bput,
    default_xlate, default_check
};

static ULONG REGPARAM2 expamem_lget(CPTR addr)
{
    fprintf(stderr,"warning: READ.L from address $%lx \n",addr);
    return 0xfffffffful;
}

static ULONG REGPARAM2 expamem_wget(CPTR addr)
{
    fprintf(stderr,"warning: READ.W from address $%lx \n",addr);
    return 0xffff;
}

static ULONG REGPARAM2 expamem_bget(CPTR addr)
{
    UBYTE value;
    addr &= 0xFFFF;
    return expamem[addr];
}

static void  REGPARAM2 expamem_write(CPTR addr, ULONG value)
{
    addr &= 0xffff;
    if (addr==00 || addr==02 || addr==0x40 || addr==0x42) {
	expamem[addr] = (value & 0xf0);
	expamem[addr+2] = (value & 0x0f) << 4;
    } else {
	expamem[addr] = ~(value & 0xf0);
	expamem[addr+2] = ~((value & 0x0f) << 4);
    }
}

static void REGPARAM2 expamem_lput(CPTR addr, ULONG value)
{
    fprintf(stderr,"warning: WRITE.L to address $%lx : value $%lx\n",addr,value);
}

static void REGPARAM2 expamem_wput(CPTR addr, ULONG value)
{
    fprintf(stderr,"warning: WRITE.W to address $%lx : value $%x\n",addr,value);
}

static void REGPARAM2 expamem_bput(CPTR addr, ULONG value)
{
    switch (addr&0xff) {
     case 0x30:
     case 0x32:
	expamem_hi = 0;
	expamem_lo = 0;
	expamem_write (0x48, 0x00);
	break;

     case 0x48:    
	expamem_hi = value;
        (*card_map[ecard])();
        fprintf (stderr,"   Card %d done.\n",ecard+1);
        ++ecard;
        if (ecard <= MAX_EXPANSION_BOARDS)
	    (*card_init[ecard])();
	else
	    expamem_init_clear();
       	break;
    
     case 0x4a:	
	expamem_lo = value;
	break;
	
     case 0x4c:
        fprintf (stderr,"   Card %d had no success.\n",ecard+1);
        ++ecard;
        if (ecard <= MAX_EXPANSION_BOARDS) 
	    (*card_init[ecard])();
	else  
	    expamem_init_clear();
        break;
    }
}

void expamem_reset()
{
    int cardno = 0;

    ecard = 0;
    
    if (fastmemory != NULL) {
	card_init[cardno] = expamem_init_fastcard;
	card_map[cardno++] = expamem_map_fastcard;
    }

    if (automount_uaedev && !ersatzkickfile) {
	card_init[cardno] = expamem_init_filesys;
	card_map[cardno++] = expamem_map_filesys;
    }

    while (cardno < MAX_EXPANSION_BOARDS) {
	card_init[cardno] = expamem_init_clear;
	card_map[cardno++] = expamem_map_clear;
    }
    
    (*card_init[0])();
}

void expansion_init(void)
{
    if (fastmem_size > 0) {
	fastmem_mask = fastmem_size - 1;
	fastmemory = (UBYTE *)calloc(fastmem_size,1);
	if (fastmemory == NULL) {
	    fprintf (stderr,"Out of memory for fastmem card.\n");
	}
    }
}

/*
 *     Expansion Card (ZORRO II) for 1/2/4/8 MB of Fast Memory
 */

void expamem_map_fastcard()
{
    fastmem_start = ((expamem_hi|(expamem_lo>>4)) << 16);
    map_banks (&fastmem_bank, fastmem_start >> 16, fastmem_size >> 16);
    fprintf (stderr,"Fastcard: mapped @$%lx: %dMB fast memory\n",fastmem_start, fastmem_size >>20);
}

void expamem_init_fastcard()
{
    expamem_init_clear();
    if (fastmem_size==0x100000)
	expamem_write (0x00, MEM_1MB+add_memory+generic);
    else if (fastmem_size==0x200000)
	expamem_write (0x00, MEM_2MB+add_memory+generic);
    else if (fastmem_size==0x400000)
	expamem_write (0x00, MEM_4MB+add_memory+generic);
    else if (fastmem_size==0x800000)
	expamem_write (0x00, MEM_8MB+add_memory+generic);

    expamem_write (0x08, care_addr);

    expamem_write (0x04, 1);
    expamem_write (0x10, hackers_id >> 8);
    expamem_write (0x14, hackers_id & 0x0f);

    expamem_write (0x18, 0x00); /* ser.no. Byte 0 */
    expamem_write (0x1c, 0x00); /* ser.no. Byte 1 */
    expamem_write (0x20, 0x00); /* ser.no. Byte 2 */
    expamem_write (0x24, 0x01); /* ser.no. Byte 3 */

    expamem_write (0x28, 0x00); /* Rom-Offset hi  */
    expamem_write (0x2c, 0x00); /* ROM-Offset lo  */

    expamem_write (0x40, 0x00); /* Ctrl/Statusreg.*/
}

/* 
 * Filesystem device
 */

void expamem_map_filesys()
{
    filesys_start=((expamem_hi | (expamem_lo >> 4)) << 16);
    map_banks(&filesys_bank, filesys_start >> 16, 1);
}

void expamem_init_filesys()
{
    UBYTE diagarea[] = { 0x90, 0x00, 0x01, 0x0C, 0x01, 0x00, 0x01, 0x06 };

    expamem_init_clear();
    expamem_write (0x00, MEM_64KB | rom_card | generic);

    expamem_write (0x08, care_addr | no_shutup);

    expamem_write (0x04, 2);
    expamem_write (0x10, hackers_id >> 8);
    expamem_write (0x14, hackers_id & 0x0f);

    expamem_write (0x18, 0x00); /* ser.no. Byte 0 */
    expamem_write (0x1c, 0x00); /* ser.no. Byte 1 */
    expamem_write (0x20, 0x00); /* ser.no. Byte 2 */
    expamem_write (0x24, 0x01); /* ser.no. Byte 3 */

    expamem_write (0x28, 0x10); /* Rom-Offset hi  */
    expamem_write (0x2c, 0x00); /* ROM-Offset lo  */

    expamem_write (0x40, 0x00); /* Ctrl/Statusreg.*/

    /* Build a DiagArea */
    memcpy(expamem + 0x1000, diagarea, sizeof diagarea);
    
    /* Call DiagEntry */
    expamem[0x1100] = 0x4E;
    expamem[0x1101] = 0xF9; /* JMP */
    expamem[0x1102] = ROM_filesys_diagentry>>24;
    expamem[0x1103] = ROM_filesys_diagentry>>16;
    expamem[0x1104] = ROM_filesys_diagentry>>8;
    expamem[0x1105] = ROM_filesys_diagentry;
    
    /* What comes next is a plain bootblock */
    expamem[0x1106] = 0x4E;
    expamem[0x1107] = 0xF9; /* JMP */
    expamem[0x1108] = EXPANSION_bootcode>>24;
    expamem[0x1109] = EXPANSION_bootcode>>16;
    expamem[0x110A] = EXPANSION_bootcode>>8;
    expamem[0x110B] = EXPANSION_bootcode;
    memcpy(filesysory, expamem, 0x2000);
}

/*
 *  Dummy Entries to show that there's no card in a slot
 */

void expamem_map_clear()
{
    fprintf(stderr, "expamem_map_clear() got called. Shouldn't happen.\n");
}

void expamem_init_clear()
{
    memset (expamem,0xff,sizeof expamem);
}
