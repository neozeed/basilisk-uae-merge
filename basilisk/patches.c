/*
 *  patches.c - Mac ROM patches
 *
 *  Basilisk (C) 1996-1997 Christian Bauer
 */

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"
#include "memory.h"
#include "readcpu.h"
#include "newcpu.h"
#include "sony.h"
#include "filedisk.h"
#include "patches.h"


// Mouse/keyboard variables
int mousex = 0, mousey = 0;					// Current mouse position
int mousebutton = 0, oldbutton = 0;		// Current/previous mouse button state
UBYTE key_states[16], old_key_states[16];	// Current/previous key states
UWORD mouse_reg_3 = 0x0301;					// Mouse ADB register 3
UWORD key_reg_3 = 0x0205;					// Keyboard ADB register 3

// Flag: Mount floppy disks on next VBL
int mount_floppy = 0;

// Mac parameter RAM
UBYTE XPRam[0x100];

// Mac time starts in 1904, Be time in 1970, this is the offset in seconds
const ULONG TIME_OFFSET = 0x7c25b080;


// Extended opcodes
enum {
	EOP_VIAINT,
	EOP_ADBOP,
	EOP_READPARAM,
	EOP_WRITEPARAM,
	EOP_READXPRAM,
	EOP_WRITEXPRAM,
	EOP_READDATETIME,
	EOP_SETDATETIME,
	EOP_CHECKLOAD,
	EOP_INSTALL_DRIVERS,
	EOP_SONY_OPEN,
	EOP_SONY_PRIME,
	EOP_SONY_CONTROL,
	EOP_SONY_STATUS,
	EOP_FILEDISK_OPEN,
	EOP_FILEDISK_PRIME,
	EOP_FILEDISK_CONTROL,
	EOP_FILEDISK_STATUS,
	EOP_BREAK
};


// Layout of private emulator memory
#define EMEM_TRAP 0			// 0xff0e instruction for CallTrap (2 bytes)
#define EMEM_ADBDATA 2		// Holds fake ADB data when calling ADB handlers (4 bytes)


// 68000 machine code patches
const UBYTE adbop_patch[] = {	// Call ADBOP() completion procedure
								// (this can't be done in ext_adbop() with CallRoutine()
								// because the completion procedure may call ADBOp() again)
	0x48, 0xe7, 0x70, 0xf0,	//	movem.l	d1-d3/a0-a3,-(sp)
	0x26, 0x48,				//	move.l	a0,a3
	0x4a, 0xab, 0x00, 0x04,	//	tst.l	4(a3)
	0x67, 0x00, 0x00, 0x18,	//	beq		1
	0x20, 0x53,				//	move.l	(a3),a0
	0x22, 0x6b, 0x00, 0x04,	//	move.l	4(a3),a1
	0x24, 0x6b, 0x00, 0x08,	//	move.l	8(a3),a2
	0x26, 0x78, 0x0c, 0xf8,	//	move.l	$cf8,a3
	0x4e, 0x91,				//	jsr		(a1)
	0x70, 0x00,				//	moveq	#0,d0
	0x60, 0x00, 0x00, 0x04,	//	bra		2
	0x70, 0xff,				//1	moveq	#-1,d0
	0x4c, 0xdf, 0x0f, 0x0e,	//2	movem.l	(sp)+,d1-d3/a0-a3
	0x4e, 0x75				//	rts
};

const UBYTE sony_patch[] = {	// Replacement for .Sony driver
	// Driver header
	0x6f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x18,			// Open() offset
	0x00, 0x26,			// Prime() offset
	0x00, 0x34,			// Control() offset
	0x00, 0x4a,			// Status() offset
	0x00, 0x7a,			// Close() offset
	0x05, 0x2e, 0x53, 0x6f, 0x6e, 0x79,

	// Open()
	0x48, 0xe7, 0x00, 0xc0,
	0xff, 0x0d, EOP_SONY_OPEN >> 8, EOP_SONY_OPEN,
	0x4c, 0xdf, 0x03, 0x00,
	0x4e, 0x75,

	// Prime()
	0x48, 0xe7, 0x00, 0xc0,
	0xff, 0x0d, EOP_SONY_PRIME >> 8, EOP_SONY_PRIME,
	0x4c, 0xdf, 0x03, 0x00,
	0x60, 0x22,

	// Control()
	0x48, 0xe7, 0x00, 0xc0,
	0xff, 0x0d, EOP_SONY_CONTROL >> 8, EOP_SONY_CONTROL,
	0x4c, 0xdf, 0x03, 0x00,
	0x0c, 0x68, 0x00, 0x01, 0x00, 0x1a,
	0x66, 0x0e,
	0x4e, 0x75,

	// Status()
	0x48, 0xe7, 0x00, 0xc0,
	0xff, 0x0d, EOP_SONY_STATUS >> 8, EOP_SONY_STATUS,
	0x4c, 0xdf, 0x03, 0x00,

	// IOReturn
	0x32, 0x28, 0x00, 0x06,
	0x08, 0x01, 0x00, 0x09,
	0x67, 0x0c,
	0x4a, 0x40,
	0x6f, 0x02,
	0x42, 0x40,
	0x31, 0x40, 0x00, 0x10,
	0x4e, 0x75,
	0x4a, 0x40,
	0x6f, 0x04,
	0x42, 0x40,
	0x4e, 0x75,
	0x2f, 0x38, 0x08, 0xfc,
	0x4e, 0x75,

	// Close()
	0x70, 0xe8,
	0x4e, 0x75
};

const UBYTE filedisk_patch[] = {	// Filedisk driver
	// Driver header
	0x6f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x18,			// Open() offset
	0x00, 0x26,			// Prime() offset
	0x00, 0x34,			// Control() offset
	0x00, 0x4a,			// Status() offset
	0x00, 0x7a,			// Close() offset
	0x05, 0x2e, 0x46, 0x69, 0x6c, 0x65,

	// Open()
	0x48, 0xe7, 0x00, 0xc0,
	0xff, 0x0d, EOP_FILEDISK_OPEN >> 8, EOP_FILEDISK_OPEN,
	0x4c, 0xdf, 0x03, 0x00,
	0x4e, 0x75,

	// Prime()
	0x48, 0xe7, 0x00, 0xc0,
	0xff, 0x0d, EOP_FILEDISK_PRIME >> 8, EOP_FILEDISK_PRIME,
	0x4c, 0xdf, 0x03, 0x00,
	0x60, 0x22,

	// Control()
	0x48, 0xe7, 0x00, 0xc0,
	0xff, 0x0d, EOP_FILEDISK_CONTROL >> 8, EOP_FILEDISK_CONTROL,
	0x4c, 0xdf, 0x03, 0x00,
	0x0c, 0x68, 0x00, 0x01, 0x00, 0x1a,
	0x66, 0x0e,
	0x4e, 0x75,

	// Status()
	0x48, 0xe7, 0x00, 0xc0,
	0xff, 0x0d, EOP_FILEDISK_STATUS >> 8, EOP_FILEDISK_STATUS,
	0x4c, 0xdf, 0x03, 0x00,

	// IOReturn
	0x32, 0x28, 0x00, 0x06,
	0x08, 0x01, 0x00, 0x09,
	0x67, 0x0c,
	0x4a, 0x40,
	0x6f, 0x02,
	0x42, 0x40,
	0x31, 0x40, 0x00, 0x10,
	0x4e, 0x75,
	0x4a, 0x40,
	0x6f, 0x04,
	0x42, 0x40,
	0x4e, 0x75,
	0x2f, 0x38, 0x08, 0xfc,
	0x4e, 0x75,

	// Close()
	0x70, 0xe8,
	0x4e, 0x75
};


// Be -> Mac raw keycode translation table
UBYTE keycode2mac[0x80] = {
	0xff, 0x35, 0x7a, 0x78, 0x63, 0x76, 0x60, 0x61,	// inv Esc  F1  F2  F3  F4  F5  F6
	0x62, 0x64, 0x65, 0x6d, 0x67, 0x6f, 0x69, 0x6b,	//  F7  F8  F9 F10 F11 F12 F13 F14
	0x71, 0x0a, 0x12, 0x13, 0x14, 0x15, 0x17, 0x16,	// F15   `   1   2   3   4   5   6
	0x1a, 0x1c, 0x19, 0x1d,	0x1b, 0x18, 0x33, 0x72,	//   7   8   9   0   -   = BSP INS
	0x73, 0x74, 0x47, 0x4b, 0x43, 0x4e, 0x30, 0x0c,	// HOM PUP NUM   /   *   - TAB   Q
	0x0d, 0x0e, 0x0f, 0x11, 0x10, 0x20, 0x22, 0x1f,	//   W   E   R   T   Y   U   I   O
	0x23, 0x21, 0x1e, 0x2a, 0x75, 0x77, 0x79, 0x59,	//   P   [   ]   \ DEL END PDN   7
	0x5b, 0x5c, 0x45, 0x39, 0x00, 0x01, 0x02, 0x03,	//   8   9   + CAP   A   S   D   F
	0x05, 0x04, 0x26, 0x28, 0x25, 0x29, 0x27, 0x24,	//   G   H   J   K   L   ;   ' RET
	0x56, 0x57, 0x58, 0x38, 0x06, 0x07, 0x08, 0x09,	//   4   5   6 SHL   Z   X   C   V
	0x0b, 0x2d, 0x2e, 0x2b, 0x2f, 0x2c, 0x38, 0x3e,	//   B   N   M   ,   .   / SHR CUP
	0x53, 0x54, 0x55, 0x4c, 0x36, 0x37, 0x31, 0x37, //   1   2   3 ENT CTL ALT SPC ALT
	0x3a, 0x3b, 0x3d, 0x3c, 0x52, 0x41, 0x3a, 0x3a,	// CTR CLF CDN CRT   0   . CMD CMD
	0x37, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,	// MNU inv inv inv inv inv inv inv
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,	// inv inv inv inv inv inv inv inv
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff	// inv inv inv inv inv inv inv inv
};


/*
 *  Patch Mac ROM
 */

#define rom_word(adr, word) rom[adr] = word >> 8; rom[adr+1] = word & 0xff;

void patch_rom(UBYTE *rom)
{
	rom_word(0x1c40, 0x601e);	// Don't jump into debugger (VIA line)

	rom_word(0x1c6c, 0x7c00);	// Don't complain about incorrect ROM checksum

	rom_word(0x50, 0x4e71);		// Don't initialize IWM
	rom_word(0x52, 0x4e71);

	rom_word(0x6a, 0x4e71);		// Skip startup sound
	rom_word(0x6c, 0x4e71);

	rom_word(0x3364, 0x4e71);	// Don't loop in ADB init

	rom_word(0x11e, 0x4e71);	// Skip main memory test
	rom_word(0x120, 0x4e71);

	rom_word(0xd5a, 0x601e);	// Don't look for SCSI devices

	if (boot_edisk) {	// Don't remove these braces!
		rom_word(0x3f83c, 0x4e71);	// Mount EDisk
	}

	rom_word(0x2be4, 0xff0d);	// Intercept VIA interrupt
	rom_word(0x2be6, EOP_VIAINT);

	rom_word(0x3880, 0xff0d);	// ADBOp() patch
	rom_word(0x3882, EOP_ADBOP);
	memcpy(rom + 0x3884, adbop_patch, sizeof(adbop_patch));

	rom_word(0xa0d4, 0xff0d);	// Read from parameter RAM
	rom_word(0xa0d6, EOP_READPARAM);
	rom_word(0xa0d8, 0x7000);
	rom_word(0xa0da, 0x4e75);

	rom_word(0xa20c, 0xff0d);	// Write to parameter RAM
	rom_word(0xa20e, EOP_WRITEPARAM);
	rom_word(0xa210, 0x7000);
	rom_word(0xa212, 0x4e75);

	rom_word(0xa262, 0xff0d);	// Read from extended parameter RAM
	rom_word(0xa264, EOP_READXPRAM);
	rom_word(0xa266, 0x7000);
	rom_word(0xa268, 0x4e75);

	rom_word(0xa26c, 0xff0d);	// Write to extended parameter RAM
	rom_word(0xa26e, EOP_WRITEXPRAM);
	rom_word(0xa270, 0x7000);
	rom_word(0xa272, 0x4e75);

	rom_word(0xa190, 0xff0d);	// Read RTC
	rom_word(0xa192, EOP_READDATETIME);
	rom_word(0xa194, 0x7000);
	rom_word(0xa196, 0x4e75);

	rom_word(0xa1c6, 0xff0d);	// Write RTC
	rom_word(0xa1c8, EOP_SETDATETIME);
	rom_word(0xa1ca, 0x7000);
	rom_word(0xa1cc, 0x4e75);

	rom_word(0xe740, 0xff0d);	// vCheckLoad() patch
	rom_word(0xe742, EOP_CHECKLOAD);
	rom_word(0xe744, 0x4e75);

	rom_word(0x18e32, 0x4238);	// InitCursor() patch (hide cursor)
	rom_word(0x18e34, 0x08cd);
	rom_word(0x18e36, 0x4e75);

	memcpy(rom + 0x34680, sony_patch, sizeof(sony_patch));	// Replace .Sony driver
	memcpy(rom + 0x34780, filedisk_patch, sizeof(filedisk_patch));	// Insert filedisk driver

	rom_word(0x78a, 0xff0d);	// Install extra drivers
	rom_word(0x78c, EOP_INSTALL_DRIVERS);
}


/*
 *  Load/save parameter RAM
 */

void load_pram(void)
{
	FILE *f;
	memset(XPRam, 0, 0x100);
	if ((f = fopen(pramfile, "rb")) != NULL) {
		fread(XPRam, 1, 0x100, f);
		fclose(f);
	}
}

void save_pram(void)
{
	FILE *f;
	if ((f = fopen(pramfile, "wb")) != NULL) {
		fwrite(XPRam, 1, 0x100, f);
		fclose(f);
	}
}


/*
 *  Execute Mac OS trap from the emulator
 */

ULONG CallTrap(UWORD trap)
{
	CPTR oldpc = m68k_getpc();

	put_word(emulmem_start + EMEM_TRAP, 0xff0e);	// Install special opcode to return from 68k mode

	MakeSR();						// Fake exception stack frame
	m68k_areg(regs, 7) -= 4;
	put_long(m68k_areg(regs, 7), emulmem_start + EMEM_TRAP);
	m68k_areg(regs, 7) -= 2;
	put_word(m68k_areg(regs, 7), regs.sr);

	m68k_areg(regs, 7) -= 2;		// subq.l #2,sp

	m68k_areg(regs, 7) -= 4;		// movem.l d1-d2/a2,-(sp)
	put_long(m68k_areg(regs, 7), m68k_areg(regs, 2));
	m68k_areg(regs, 7) -= 4;
	put_long(m68k_areg(regs, 7), m68k_dreg(regs, 2));
	m68k_areg(regs, 7) -= 4;
	put_long(m68k_areg(regs, 7), m68k_dreg(regs, 1));

	m68k_dreg(regs, 2) = trap;		// Load trap number to d2

	m68k_setpc(0x402ce6);			// Classic ROM 68000 trap handler
	m68k_go(0);
	m68k_setpc(oldpc);
	return m68k_dreg(regs, 0);
}


/*
 *  Execute 68k subroutine from the emulator
 */

ULONG CallRoutine(CPTR addr)
{
	CPTR oldpc = m68k_getpc();

	put_word(emulmem_start + EMEM_TRAP, 0xff0e);	// Install special opcode to return from 68k mode

	MakeSR();						// Push fake return address
	m68k_areg(regs, 7) -= 4;
	put_long(m68k_areg(regs, 7), emulmem_start + EMEM_TRAP);

	m68k_setpc(addr);				// Execute subroutine
	m68k_go(0);
	m68k_setpc(oldpc);
	return m68k_dreg(regs, 0);
}


/*
 *  Execute extended opcodes
 */

void ext_viaint(void)
{
	CPTR adbbase;
	ULONG time;

	// Update mouse position
	put_word(0x832, mousex);
	put_word(0x830, mousey);
	put_byte(0x8ce, 0);			// CrsrNew
	put_byte(0x8d2, 0);			// CrsrObscure

	// Call mouse ADB handler if button pressed or released
	if (mousebutton != oldbutton) {
		if ((adbbase = get_long(0xcf8)) < 0x80000000) {			// ADBBase valid?
			if (m68k_areg(regs, 1) = get_long(adbbase + 16)) {	// Handler present?
				m68k_areg(regs, 2) = get_long(adbbase + 20);	// Get handler data area
				put_byte(emulmem_start + EMEM_ADBDATA, 2);		// Fake ADB data
				put_byte(emulmem_start + EMEM_ADBDATA+1, mousebutton ? 0 : 0x80);
				put_byte(emulmem_start + EMEM_ADBDATA+2, 0x80);
				m68k_areg(regs, 0) = emulmem_start + EMEM_ADBDATA;
				m68k_dreg(regs, 0) = 0x3c;						// Talk 0
				CallRoutine(m68k_areg(regs, 1));
			}
		}
		oldbutton = mousebutton;
	}

	// Did the state of any key change?
	if (memcmp(key_states, old_key_states, sizeof(key_states))) {

		// Yes, test all keys
		int be_code, be_byte, be_bit, mac_code;
		for (be_code=0; be_code<0x80; be_code++) {
			be_byte = be_code >> 3;
			be_bit = 1 << (~be_code & 7);

			// Key state changed?
			if ((key_states[be_byte] & be_bit)
	    		!= (old_key_states[be_byte] & be_bit)) {

				// Yes, translate keycode
				mac_code = keycode2mac[be_code];
				if (!(key_states[be_byte] & be_bit))
					mac_code |= 0x80;

				// Call keyboard ADB handler
				if ((adbbase = get_long(0xcf8)) < 0x80000000) {			// ADBBase valid?
					if (m68k_areg(regs, 1) = get_long(adbbase + 4)) {	// Handler present?
						m68k_areg(regs, 2) = get_long(adbbase + 8);		// Get handler data area
						put_byte(emulmem_start + EMEM_ADBDATA, 2);		// Fake ADB data
						put_byte(emulmem_start + EMEM_ADBDATA+1, mac_code);
						put_byte(emulmem_start + EMEM_ADBDATA+2, 0xff);
						m68k_areg(regs, 0) = emulmem_start + EMEM_ADBDATA;
						m68k_dreg(regs, 0) = 0x2c;						// Talk 0
						CallRoutine(m68k_areg(regs, 1));
					}
				}
			}
		}

		memcpy(old_key_states, key_states, sizeof(key_states));
	}

	// Mount floppy disks if requested
	if (mount_floppy) {
		disk_inserted(1);
		disk_inserted(2);
		mount_floppy = 0;
	}

	// Increment ticks variable
	put_long(0x16a, get_long(0x16a) + 1);
}

void ext_adbop(void)
{
	int adr = m68k_dreg(regs, 0) & 0xff;
	int data = m68k_areg(regs, 0);

//	printf("ADBOp adr %x\n", adr);

	// Check which device was addressed and act accordingly
	if ((adr >> 4) == (mouse_reg_3 >> 8)) {			// Mouse
		if ((adr & 0x0f) == 0x0b) {					// Mouse listen reg 3
			if (get_byte(get_long(data) + 2) != 0xff)
				mouse_reg_3 = mouse_reg_3 & 0xff | (get_byte(get_long(data) + 1) << 8);
		} else if ((adr & 0x0c) == 0x0c) {			// Mouse talk
			put_byte(get_long(data), 0);
			if ((adr & 3) == 3) {					// Mouse talk reg 3
				put_byte(get_long(data), 2);
				put_byte(get_long(data) + 1, mouse_reg_3 >> 8);
				put_byte(get_long(data) + 2, mouse_reg_3 & 0xff);
			}
		}
	} else if ((adr >> 4) == (key_reg_3 >> 8)) {	// Keyboard
		if ((adr & 0x0f) == 0x0b) {					// Keyboard listen reg 3
			if (get_byte(get_long(data) + 2) != 0xff)
				key_reg_3 = key_reg_3 & 0xff | (get_byte(get_long(data) + 1) << 8);
		} else if ((adr & 0x0c) == 0x0c) {			// Keyboard talk
			put_byte(get_long(data), 0);
			if ((adr & 3) == 3) {					// Keyboard talk reg 3
				put_byte(get_long(data), 2);
				put_byte(get_long(data) + 1, key_reg_3 >> 8);
				put_byte(get_long(data) + 2, key_reg_3 & 0xff);
			}
		}
	} else											// Unknown address
		if ((adr & 0x0c) == 0x0c)
			put_byte(get_long(data), 0);			// Talk: 0 bytes of data
}

void ersatz_perform(UWORD op)
{
	ULONG old_a0;
	ULONG cl_type;
	WORD cl_id;
	int i;

	switch (op) {
		case EOP_VIAINT:		// VIA interrupt, mouse and keyboard handling
			ext_viaint();
			break;

		case EOP_ADBOP:			// ADBOp() trap
			ext_adbop();
			break;

		case EOP_READPARAM:		// Read 20 bytes from parameter RAM
//			printf("ReadParam\n");
			for (i=0; i<16; i++)
				put_byte(m68k_areg(regs, 1) + i, XPRam[i + 16]);
			for (i=0; i<4; i++)
				put_byte(m68k_areg(regs, 1) + i + 16, XPRam[i + 8]);
			break;

		case EOP_WRITEPARAM:	// Write 20 bytes to parameter RAM
//			printf("WriteParam\n");
			for (i=0; i<16; i++)
				XPRam[i + 16] = get_byte(0x1f8 + i);
			for (i=0; i<4; i++)
				XPRam[i + 8] = get_byte(0x208 + i);
			break;

		case EOP_READXPRAM:		// Read from extended PRAM
//			printf("ReadXPRam %08x\n", m68k_dreg(regs, 0));
			for (i=0; i<(m68k_dreg(regs, 0) >> 16); i++)
				put_byte(m68k_areg(regs, 0) + i, XPRam[(m68k_dreg(regs, 0) + i) & 0xff]);
			break;

		case EOP_WRITEXPRAM:	// Write to extended PRAM
//			printf("WriteXPRam %08x\n", m68k_dreg(regs, 0));
			for (i=0; i<(m68k_dreg(regs, 0) >> 16); i++)
				XPRam[(m68k_dreg(regs, 0) + i) & 0xff] = get_byte(m68k_areg(regs, 0) + i);
			break;

		case EOP_READDATETIME:	// Read RTC
//			printf("ReadDateTime\n");
#ifdef __BEOS__
			put_long(0x20c, real_time_clock() + TIME_OFFSET);
#else
			put_long(0x20c, 0);
#endif
			break;

		case EOP_SETDATETIME:	// Write RTC
//			printf("WriteDateTime\n");
#ifdef __BEOS__
			set_real_time_clock(get_long(0x20c) - TIME_OFFSET);
#endif
			break;

		case EOP_CHECKLOAD:		// vCheckLoad() patch
			cl_type = m68k_dreg(regs, 3);
			cl_id = get_word(m68k_areg(regs, 2));
//			printf("CheckLoad %c%c%c%c (%08x) ID %d\n", cl_type >> 24, (cl_type >> 16) & 0xff, (cl_type >> 8) & 0xff, cl_type & 0xff, cl_type, cl_id);
			CallRoutine(get_long(0x7f0));

			if (cl_type == 0x70746368 && cl_id == 34) {		// 'ptch'
				CPTR ptr;
//				printf("ptch 34 found\n");
				if (m68k_areg(regs, 0) && (ptr = get_long(m68k_areg(regs, 0)))) {
					long size = (get_long(m68k_areg(regs, 0) - 8) & 0xffffff) / 2;
					while (size--) {

						if (get_long(ptr) == 0x227801d4	&& get_long(ptr+4) == 0x10110200
							&& get_word(ptr+8) == 0x0030)
							put_word(ptr+14, 0x4e71);	// Don't wait for VIA (6.0.8)

						if (get_long(ptr) == 0x21c005f0) {
							put_word(ptr, 0x4e71);		// Don't replace ADBOp() (6.0.8)
							put_word(ptr+2, 0x4e71);
						}

						ptr += 2;
					}
				}

			} else if (cl_type == 0x6c706368 && cl_id == 6) {	// 'lpch'
				CPTR ptr;
//				printf("lpch 6 found\n");
				if (m68k_areg(regs, 0) && (ptr = get_long(m68k_areg(regs, 0)))) {

					if (get_long(ptr+0x7e) == 0x227801d4 && get_long(ptr+0x82) == 0x10110200
						&& get_word(ptr+0x86) == 0x0030)
						put_word(ptr+0x8c, 0x4e71);			// Don't wait for VIA (7.1.0)

					if (get_long(ptr+0x2c) == 0x48e71030 && get_long(ptr+0x30) == 0x26780cf8) {
						put_word(ptr+0x2c, 0x4ef9);			// Override ADBOp() patch (7.1.0)
						put_long(ptr+0x2e, 0x403880);
					}
				}
			}
			break;

		case EOP_INSTALL_DRIVERS:	// Install extra drivers

			// Complete opening of .Sony driver
			put_long(m68k_areg(regs, 0) + 0x12, m68k_areg(regs, 1));
			CallTrap(0xa000);

			// Install filedisk driver
			old_a0 = m68k_areg(regs, 0);
			m68k_areg(regs, 0) = 0x434780;
			m68k_dreg(regs, 0) = -63;	// refNum
			CallTrap(0xa43d);	// DrvrInstallRsrvMem

			// Lock driver
			m68k_areg(regs, 0) = get_long(get_long(0x11c) + 62*4);
			CallTrap(0xa029);	// HLock

			// Initialize DCE
			put_long(get_long(get_long(get_long(0x11c) + 62*4)), 0x434780);
			put_word(get_long(get_long(get_long(0x11c) + 62*4)) + 4, 0x6f00);

			// Prepare to open driver
			m68k_areg(regs, 0) = old_a0;
			put_long(m68k_areg(regs, 0) + 0x12, 0x434792);
			break;

		case EOP_SONY_OPEN:
			m68k_dreg(regs, 0) = SonyOpen(m68k_areg(regs, 0), m68k_areg(regs, 1));
			break;

		case EOP_SONY_PRIME:
			m68k_dreg(regs, 0) = SonyPrime(m68k_areg(regs, 0), m68k_areg(regs, 1));
			break;

		case EOP_SONY_CONTROL:
			m68k_dreg(regs, 0) = SonyControl(m68k_areg(regs, 0), m68k_areg(regs, 1));
			break;

		case EOP_SONY_STATUS:
			m68k_dreg(regs, 0) = SonyStatus(m68k_areg(regs, 0), m68k_areg(regs, 1));
			break;

		case EOP_FILEDISK_OPEN:
			m68k_dreg(regs, 0) = FileDiskOpen(m68k_areg(regs, 0), m68k_areg(regs, 1));
			break;

		case EOP_FILEDISK_PRIME:
			m68k_dreg(regs, 0) = FileDiskPrime(m68k_areg(regs, 0), m68k_areg(regs, 1));
			break;

		case EOP_FILEDISK_CONTROL:
			m68k_dreg(regs, 0) = FileDiskControl(m68k_areg(regs, 0), m68k_areg(regs, 1));
			break;

		case EOP_FILEDISK_STATUS:
			m68k_dreg(regs, 0) = FileDiskStatus(m68k_areg(regs, 0), m68k_areg(regs, 1));
			break;

		case EOP_BREAK:
			printf("Breakpoint reached\n");
			break;

		default:
			fprintf(stderr, "Internal error. Giving up.\n");
			abort();
	}
}


/*
 *  Special reset actions
 */

void customreset(void)
{
    regs.spcflags = 0;
	put_long(0xcfc, 0);	// Clear Mac warm start flag
}
