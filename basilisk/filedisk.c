/*
 *  filedisk.c - Filedisk driver
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
#include "filedisk.h"
#include "patches.h"
#include "zfile.h"


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
#define ds_SIZEOF		28

// Field offsets in private data area
#define pd_drive1		0
#define pd_drive2		ds_SIZEOF
#define pd_drive3		(2*ds_SIZEOF)
#define pd_drive4		(3*ds_SIZEOF)
#define pd_SIZEOF		(4*ds_SIZEOF)


static CPTR	PrivData;

// Struct for each drive
typedef struct {
	FILE *file;			// Disk image file
	CPTR status;		// Pointer to drive status record
	int num;			// Drive number
	long blocks;		// Number of blocks
	long header_len;	// Length of file header in bytes
} drive_info;

static drive_info drive[4];


/*
 *  Initialization
 */

static void open_filedisk(int i, char *path)
{
	long size;

	// Open image file
	if ((drive[i].file = zfile_open(path, "rb+")) != NULL) {

		// Get file size and determine number of blocks and header length
		fseek(drive[i].file, 0, SEEK_END);
		size = ftell(drive[i].file);

		if (size == 838484) {	// 800K DiskCopy image
			drive[i].blocks = 1600;
			drive[i].header_len = 84;
		} else {
			drive[i].blocks = size / 512;
			drive[i].header_len = size % 512;
		}
	}
}

void filedisk_init(void)
{
	open_filedisk(0, df0);
	open_filedisk(1, df1);
	open_filedisk(2, df2);
	open_filedisk(3, df3);
}


/*
 *  Deinitialization
 */

void filedisk_exit(void)
{
	int i;

	for (i=0; i<4; i++)
		if (drive[i].file != NULL)
			zfile_close(drive[i].file);
}


/*
 *  Get pointer to drive info, NULL = invalid drive number
 */

static drive_info *get_drive_info(UWORD drvnum)
{
	int i;

	for (i=0; i<4; i++)
		if (drvnum == drive[i].num)
			return &drive[i];
	return NULL;
}


/*
 *  Driver Open() routine
 */

LONG FileDiskOpen(CPTR pb, CPTR dce)
{
	int i;

//	printf("FileDiskOpen\n");

	// Allocate private data area
	m68k_dreg(regs, 0) = pd_SIZEOF;
	CallTrap(0xa71e);			// NewPtrSysClear()

	put_long(dce + dCtlStorage, PrivData = m68k_areg(regs, 0));
	put_long(dce + dCtlPosition, 0);

	// Install drives
	drive[0].status = PrivData + pd_drive1;
	drive[1].status = PrivData + pd_drive2;
	drive[2].status = PrivData + pd_drive3;
	drive[3].status = PrivData + pd_drive4;

	for (i=0; i<4; i++)
		if (drive[i].file != NULL) {
			drive[i].num = 3 + i;		// Start with drive number 3
			put_byte(drive[i].status + dsDiskInPlace, 8);	// Fixed disk
			put_byte(drive[i].status + dsInstalled, 1);
			put_word(drive[i].status + dsQType, 1);
			put_word(drive[i].status + dsDriveType, drive[i].num);
			put_word(drive[i].status + dsDriveSize, drive[i].blocks);
			put_word(drive[i].status + dsDriveS1, drive[i].blocks >> 16);

			m68k_dreg(regs, 0) = (drive[i].num << 16) | 0xffc1;	// Driver -63
			m68k_areg(regs, 0) = drive[i].status + dsQLink;
			CallTrap(0xa04e);			// AddDrive()
		}

	return 0;
}


/*
 *  Driver Prime() routine
 */

LONG FileDiskPrime(CPTR pb, CPTR dce)
{
	drive_info *info;
	ULONG length, position, actual;
	UBYTE *buf;

	if ((info = get_drive_info(get_word(pb + ioVRefNum))) == NULL)
		return -64;		// No drive

	// Get parameters and seek
	buf = get_real_address(get_long(pb + ioBuffer));
	length = get_long(pb + ioReqCount);
	position = get_long(dce + dCtlPosition);
	put_long(pb + ioActCount, 0);
	if (fseek(info->file, position + info->header_len, SEEK_SET) != 0)
		return -80;	// Seek error

	if ((get_word(pb + ioTrap) & 0xff) == 2) {

		// Read
		actual = fread(buf, 1, length, info->file);
		put_long(pb + ioActCount, actual);
		put_long(dce + dCtlPosition, position + actual);

		if (actual != length)
			return -19;	// Read error
	
	} else {

		// Write
		actual = fwrite(buf, 1, length, info->file);
		put_long(pb + ioActCount, actual);
		put_long(dce + dCtlPosition, position + actual);

		if (actual != length)
			return -20;	// Write error
	}

	return 0;
}


/*
 *  Driver Control() routine
 */

LONG FileDiskControl(CPTR pb, CPTR dce)
{
	drive_info *info;
	UWORD code = get_word(pb + csCode);
	int i;

//	printf("FileDiskControl %04x\n", code);

	// General codes
	switch (code) {
		case 1:		// KillIO
			return 0;

		case 65:	// Periodic action ("insert" disks)
			for (i=0; i<4; i++)
				if (drive[i].file != NULL) {
					m68k_dreg(regs, 0) = drive[i].num;
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
			return 0;

		case 6:		// Format disk
			return 0;

		case 7:		// Eject (re-insert disk)
			m68k_dreg(regs, 0) = info->num;
			m68k_areg(regs, 0) = 7;		// diskEvent
			CallTrap(0xa02f);	// PostEvent()
			return -17;

		case 21:	// Get drive icon
			return -17;

		case 22:	// Get disk icon
			return -17;

		case 23:	// Get drive info
			put_word(pb + csParam, 0x0401);	// Unspecified fixed drive
			return 0;

		default:
			return -17;	// Unimplemented control call
	}
}


/*
 *  Driver Status() routine
 */

LONG FileDiskStatus(CPTR pb, CPTR dce)
{
	drive_info *info;
	UWORD code = get_word(pb + csCode);
	CPTR ptr;

//	printf("FileDiskStatus %04x\n", code);

	if ((info = get_drive_info(get_word(pb + ioVRefNum))) == NULL)
		return -64;		// No drive

	switch (code) {
		case 6:		// Return format list
			if (get_word(pb + csParam) > 0) {
				CPTR ptr = get_long(pb + csParam + 2);
				put_word(pb + csParam, 1);		// 1 format
				put_long(ptr, info->blocks);
				put_long(ptr + 4, 0x40000000);
				return 0;
			} else
				return -50;		// Parameter error

		case 8:		// Get Drive Status
			m68k_areg(regs, 0) = info->status;
			m68k_areg(regs, 1) = pb + csParam;
			m68k_dreg(regs, 0) = 22;
			CallTrap(0xa02e);	// BlockMove()
			return 0;

		default:
			return -18;	// Unimplemented status call
	}
}
