 /* 
  * UAE - The Un*x Amiga Emulator
  * 
  * A "replacement" for a missing Kickstart
  * Warning! Q&D
  *
  * (c) 1995 Bernd Schmidt
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"
#include "memory.h"
#include "custom.h"
#include "readcpu.h"
#include "newcpu.h"
#include "cia.h"
#include "disk.h"
#include "ersatz.h"

#define EOP_INIT     0
#define EOP_NIMP     1
#define EOP_SERVEINT 2
#define EOP_DOIO     3
#define EOP_OPENLIB  4
#define EOP_AVAILMEM 5
#define EOP_ALLOCMEM 6
#define EOP_ALLOCABS 7

void init_ersatz_rom (UBYTE *data)
{
    fprintf(stderr, "Trying to use Kickstart replacement.\n");
    *data++ = 0x00; *data++ = 0x08; /* initial SP */
    *data++ = 0x00; *data++ = 0x00;
    *data++ = 0x00; *data++ = 0xF8; /* initial PC */
    *data++ = 0x00; *data++ = 0x08;

    *data++ = 0xFF; *data++ = 0x0D;
    *data++ = 0x00; *data++ = EOP_INIT;
    *data++ = 0xFF; *data++ = 0x0D;
    *data++ = 0x00; *data++ = EOP_NIMP;
    
    *data++ = 0xFF; *data++ = 0x0D;
    *data++ = 0x00; *data++ = EOP_DOIO;
    *data++ = 0x4E; *data++ = 0x75;
    *data++ = 0xFF; *data++ = 0x0D;
    
    *data++ = 0x00; *data++ = EOP_SERVEINT;
    *data++ = 0x4E; *data++ = 0x73;
    *data++ = 0xFF; *data++ = 0x0D;
    *data++ = 0x00; *data++ = EOP_AVAILMEM;
    
    *data++ = 0x4E; *data++ = 0x75;
    *data++ = 0xFF; *data++ = 0x0D;
    *data++ = 0x00; *data++ = EOP_ALLOCMEM;    
    *data++ = 0x4E; *data++ = 0x75;

    *data++ = 0xFF; *data++ = 0x0D;
    *data++ = 0x00; *data++ = EOP_ALLOCABS;
    *data++ = 0x4E; *data++ = 0x75;
}

static void ersatz_doio (void)
{
    CPTR request = m68k_areg(regs, 1);
    switch (get_word (request + 0x1C)) {
     case 9: /* TD_MOTOR is harmless */
     case 2: case 0x8002: /* READ commands */
	break;
	
     default:
	fprintf(stderr, "Only CMD_READ supported in DoIO()\n");
	abort();
    }
    {
	CPTR dest = get_long (request + 0x28);
	int start = get_long (request + 0x2C) / 512;
	int nsecs = get_long (request + 0x24) / 512;
	int tr = start / 11;
	int sec = start % 11;
	while (nsecs--) {
	    DISK_ersatz_read (tr, sec, dest);
	    dest += 512;
	    if (++sec == 11)
		sec = 0, tr++;
	}
    }
}

static void ersatz_init (void)
{
    int f;
    CPTR request;
    CPTR a;
    
    regs.s = 0;
    /* Set some interrupt vectors */
    for (a = 8; a < 0xC0; a += 4) {
	put_long (a, 0xF80016);
    }
    regs.isp = regs.msp = regs.usp = 0x800;
    m68k_areg(regs, 7) = 0x80000;
    regs.intmask = 0;
    
    /* Build a dummy execbase */
    put_long (4, m68k_areg(regs, 6) = 0x676);
    put_byte (0x676 + 0x129, 0);    
    for (f = 1; f < 105; f++) {	
    	put_word (0x676 - 6*f, 0x4EF9);
	put_long (0x676 - 6*f + 2, 0xF8000C);
    }
    /* Some "supported" functions */
    put_long (0x676 - 456 + 2, 0xF80010);
    put_long (0x676 - 216 + 2, 0xF8001C);
    put_long (0x676 - 198 + 2, 0xF80022);
    put_long (0x676 - 204 + 2, 0xF80028);
    put_long (0x676 - 210 + 2, 0xF80026);
    
    /* Build an IORequest */
    request = 0x800;
    put_word (request + 0x1C, 2);
    put_long (request + 0x28, 0x4000);
    put_long (request + 0x2C, 0);
    put_long (request + 0x24, 0x200 * 4);
    m68k_areg(regs, 1) = request;
    ersatz_doio ();
    m68k_setpc (0x400C);

    /* Init the hardware */
    put_long (0x3000, 0xFFFFFFFEul);
    put_long (0xDFF080, 0x3000);
    put_word (0xDFF088, 0);
    put_word (0xDFF096, 0xE390);
    put_word (0xDFF09A, 0xE02C);
    put_word (0xDFF09E, 0x0000);
    put_word (0xDFF092, 0x0038);
    put_word (0xDFF094, 0x00D0);
    put_word (0xDFF08E, 0x2C81);
    put_word (0xDFF090, 0xF4C1);
    put_word (0xDFF02A, 0x8000);
    
    put_byte (0xBFD100, 0xF7);
    put_byte (0xBFEE01, 0);
    put_byte (0xBFEF01, 0x08);
    put_byte (0xBFDE00, 0x04);
    put_byte (0xBFDF00, 0x84);
    put_byte (0xBFDD00, 0x9F);
    put_byte (0xBFED01, 0x9F);
}

void ersatz_perform (UWORD what)
{
    switch (what) {
     case EOP_INIT:
	ersatz_init ();
	break;
	
     case EOP_SERVEINT:
	/* Just reset all the interrupt request bits */
	put_word (0xDFF09C, get_word (0xDFF01E) & 0x3FFF);
	break;
	
     case EOP_DOIO:
	ersatz_doio ();
	break;
	
     case EOP_AVAILMEM:
	m68k_dreg(regs, 0) = m68k_dreg(regs, 1) & 4 ? 0 : 0x70000;
	break;
	
     case EOP_ALLOCMEM:
	m68k_dreg(regs, 0) = m68k_dreg(regs, 1) & 4 ? 0 : 0x0F000;
	break;

     case EOP_ALLOCABS:
	m68k_dreg(regs, 0) = m68k_areg(regs, 1);
	break;

     case EOP_NIMP:
	fprintf(stderr, "Unimplemented Kickstart function called\n");
	abort ();
     case EOP_OPENLIB:	
     default:
	fprintf(stderr, "Internal error. Giving up.\n");
	abort ();
    }
}
