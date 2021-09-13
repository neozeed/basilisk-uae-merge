/*
 *  sony.c - .Sony driver (floppy drives)
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
#include "patches.h"
#include "zfile.h"
#include "gui.h"


// Field offsets in Device Control Entry
#define dCtlDriver		0
#define dCtlFlags		4
#define dCtlQHdr		6
#define dCtlPosition	16
#define dCtlStorage		20
#define dCtlRefNum		24
#define dCtlCurTicks	26
#define dCtlWindow		30
#define dCtlDelay		34
#define dCtlEMask		36
#define dCtlMenu		38

// Field offsets in Parameter Block
#define ioTrap			6
#define ioCmdAddr		8
#define ioCompletion	12
#define ioResult		16
#define ioNamePtr		18
#define ioVRefNum		22
#define ioRefNum		24
#define csCode			26
#define csParam			28
#define ioVersNum		26
#define ioPermssn		27
#define ioMisc			28
#define ioBuffer		32
#define ioReqCount		36
#define ioActCount		40
#define ioPosMode		44
#define ioPosOffset		46

// Field offsets in Drive Status
#define dsTrack			0
#define dsWriteProt		2
#define dsDiskInPlace	3
#define dsInstalled		4
#define dsSides			5
#define dsQLink			6
#define dsQType			10
#define dsDrive			12
#define dsRefNum		14
#define dsFSID			16
#define dsTwoSideFmt	18
#define dsNewIntf		19
#define dsDriveErrs		20
#define dsMFMDrive		22
#define dsMFMDisk		23
#define dsTwoMegFmt		24
#define dsDriveSize		18
#define dsDriveS1		20
#define dsDriveType		22
#define dsDriveManf		24
#define dsDriveChar		26
#define dsDriveMisc		27

// Field offsets in SonyVars
#define sv_drive1		74
#define sv_drive2		140


static CPTR	SonyVars;

// Struct for each drive
typedef struct {
	int fd;			// Floppy driver file descriptor
	CPTR status;	// Pointer to drive status record
	int num;		// Drive number
} drive_info;

static drive_info drive[2];


/*
 *  Initialization
 */

void sony_init(void)
{
	drive[0].fd = open("/dev/floppy_disk", O_RDWR);
	drive[1].fd = -1;
}


/*
 *  Deinitialization
 */

void sony_exit(void)
{
	if (drive[0].fd != -1)
		close(drive[0].fd);
	if (drive[1].fd != -1)
		close(drive[1].fd);
}


/*
 *  Get pointer to drive info, NULL = invalid drive number
 */

static drive_info *get_drive_info(UWORD drvnum)
{
	if (drvnum == drive[0].num)
		return &drive[0];
	else if (drvnum == drive[1].num)
		return &drive[1];
	else
		return NULL;
}


/*
 *  Driver Open() routine
 */

LONG SonyOpen(CPTR pb, CPTR dce)
{
	UBYTE buf[512];
	int i;

//	printf("SonyOpen\n");

	// Allocate SonyVars
	m68k_dreg(regs, 0) = 0x34a;
	CallTrap(0xa71e);			// NewPtrSysClear()
	put_long(0x134, SonyVars = m68k_areg(regs, 0));
	put_long(m68k_areg(regs, 0), dce);

	// Install driver with refnum #-2
	put_long(get_long(0x11c) + 4, get_long(get_long(0x11c) + 16));
	put_byte(dce + dCtlQHdr+1, 2);
	put_long(dce + dCtlPosition, 0);

	// Install drives
	drive[0].status = SonyVars + sv_drive1;
	drive[1].status = SonyVars + sv_drive2;

	for (i=0; i<2; i++)
		if (drive[i].fd != -1) {
			drive[i].num = 1 + i;		// Start with drive number 1
			lseek(drive[i].fd, 0, SEEK_SET);
			if (read(drive[i].fd, buf, 512) == 512)	// Disk in drive?
				put_byte(SonyVars + sv_drive1 + dsDiskInPlace, drive[i].num);
			put_byte(drive[i].status + dsWriteProt, 0x80);
			put_byte(drive[i].status + dsInstalled, 1);
			put_byte(drive[i].status + dsSides, -1);
			put_byte(drive[i].status + dsTwoSideFmt, -1);
			put_byte(drive[i].status + dsNewIntf, -1);
			put_byte(drive[i].status + dsMFMDrive, -1);
			put_byte(drive[i].status + dsMFMDisk, -1);
			put_byte(drive[i].status + dsTwoMegFmt, -1);

			m68k_dreg(regs, 0) = (drive[i].num << 16) | 0xfffb;	// Driver -5
			m68k_areg(regs, 0) = drive[i].status + dsQLink;
			CallTrap(0xa04e);			// AddDrive()
		}

	return 0;
}


/*
 *  Driver Prime() routine
 */

LONG SonyPrime(CPTR pb, CPTR dce)
{
	drive_info *info;
	ULONG length, position, actual;
	UBYTE *buf;

	if ((info = get_drive_info(get_word(pb + ioVRefNum))) == NULL)
		return -64;		// No drive
	if (get_byte(info->status + dsDiskInPlace) <= 0)
		return -65;		// Volume off-line

	// Get parameters and seek
	buf = get_real_address(get_long(pb + ioBuffer));
	length = get_long(pb + ioReqCount);
	position = get_long(dce + dCtlPosition);
	put_long(pb + ioActCount, 0);
	if (lseek(info->fd, position, SEEK_SET) < 0)
		return -80;	// Seek error

	if ((get_word(pb + ioTrap) & 0xff) == 2) {

		// Read
		actual = read(info->fd, buf, length);
		if (actual < 0)
			actual = 0;
		put_long(pb + ioActCount, actual);
		put_long(dce + dCtlPosition, position + actual);

		if (actual != length)
			return -19;	// Read error

	} else {

		// Write
//		actual = write(info->fd, buf, length);
//		if (actual < 0)
//			actual = 0;
//		put_long(pb + ioActCount, actual);
//		put_long(dce + dCtlPosition, position + actual);

		return -44;	// Write protected

//		if (actual != length)
//			return -20;	// Write error
	}

	return 0;
}


/*
 *  Driver Control() routine
 */

LONG SonyControl(CPTR pb, CPTR dce)
{
	drive_info *info;
	UWORD code = get_word(pb + csCode);

//	printf("SonyControl %04x\n", code);

	// General codes
	switch (code) {
		case 1:		// KillIO
		case 8:		// Set Tag Buffer
		case 9:		// Track Cache
			return 0;

		case 65:	// Periodic action ("insert" disks on startup)
			if (drive[0].fd != -1 && get_byte(drive[0].status + dsDiskInPlace) > 0) {
				m68k_dreg(regs, 0) = drive[0].num;
				m68k_areg(regs, 0) = 7;		// diskEvent
				CallTrap(0xa02f);	// PostEvent()
			}
			if (drive[1].fd != -1 && get_byte(drive[1].status + dsDiskInPlace) > 0) {
				m68k_dreg(regs, 0) = drive[1].num;
				m68k_areg(regs, 0) = 7;		// diskEvent
				CallTrap(0xa02f);	// PostEvent()
			}
			put_word(m68k_areg(regs, 1) + dCtlFlags, get_word(m68k_areg(regs, 1) + dCtlFlags) & 0x4fff);
			return 0;
	}

	if ((info = get_drive_info(get_word(pb + ioVRefNum))) == NULL)
		return -64;		// No drive

	// Drive-specific codes
	switch (code) {
		case 5:		// Verify disk
			if (get_byte(info->status + dsDiskInPlace) > 0)
				return 0;
			else
				return -65;	// Volume off-line

		case 6:		// Format disk
			if (get_byte(info->status + dsDiskInPlace) > 0)
				return -44;	// Write protected
			else
				return -65;	// Volume off-line

		case 7:		// Eject
			if (get_byte(info->status + dsDiskInPlace) > 0) {
				put_byte(info->status + dsDiskInPlace, 0);
				gui_disk_unmounted(info->num);
				return 0;
			} else
				return -65;	// Volume off-line

		case 21:	// Get drive icon
			return -17;

		case 22:	// Get disk icon
			return -17;

		case 23:	// Get drive info
			if (info->num == 1)
				put_word(pb + csParam, 0x0004);	// Internal drive
			else
				put_word(pb + csParam, 0x0104);	// External drive
			return 0;

		case 0x5343:	// Format and write to disk
			if (get_byte(info->status + dsDiskInPlace) > 0)
				return -44;	// Write protected
			else
				return -65;	// Volume off-line

		default:
			return -17;	// Unimplemented control call
	}
}


/*
 *  Driver Status() routine
 */

LONG SonyStatus(CPTR pb, CPTR dce)
{
	drive_info *info;
	UWORD code = get_word(pb + csCode);
	CPTR ptr;

//	printf("SonyStatus %04x\n", code);

	if ((info = get_drive_info(get_word(pb + ioVRefNum))) == NULL)
		return -64;		// No drive

	switch (code) {
		case 6:		// Return format list
			if (get_word(pb + csParam) > 0) {
				CPTR ptr = get_long(pb + csParam + 2);
				put_word(pb + csParam, 1);		// 1 format
				put_long(ptr, 2880);			// 2880 sectors
				put_long(ptr + 4, 0xd2120050);	// 2 heads, 18 secs/track, 80 tracks
				return 0;
			} else
				return -50;		// Parameter error

		case 8:		// Get Drive Status
			m68k_areg(regs, 0) = info->status;
			m68k_areg(regs, 1) = pb + csParam;
			m68k_dreg(regs, 0) = 22;
			CallTrap(0xa02e);	// BlockMove()
			return 0;

		case 10:	// Get disk type
			put_long(pb + csParam, get_long(info->status + dsMFMDrive) | 0xff);
			return 0;

		case 0x4456:	// Duplicator version supported
			put_word(pb + csParam, 0x0410);
			return 0;

		case 0x5343:	// Get address header format byte
			put_byte(pb + csParam, 0x22);
			return 0;

		default:
			return -18;	// Unimplemented status call
	}
}


/*
 *  Floppy disk was inserted, test and mount it
 */

void disk_inserted(int num)
{
	UBYTE buf[512];
	drive_info *info;
	if ((info = get_drive_info(num)) == NULL)
		return;

	if (info->fd != -1 && get_byte(info->status + dsDiskInPlace) <= 0) {
		lseek(info->fd, 0, SEEK_SET);
		if (read(info->fd, buf, 512) == 512) {		// Disk in drive?
			put_byte(info->status + dsDiskInPlace, info->num);
			m68k_dreg(regs, 0) = info->num;
			m68k_areg(regs, 0) = 7;		// diskEvent
			CallTrap(0xa02f);	// PostEvent()
		}
	}
}
