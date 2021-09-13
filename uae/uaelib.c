/*
 * UAE - The U*nix Amiga Emulator
 * 
 * UAE Library v0.1
 * 
 * (c) 1996 Tauno Taipaleenmaki <tataipal@raita.oulu.fi>
 *
 * Change UAE parameters and other stuff from inside the emulation. Creates
 * a library called "uae.library" before the boot-up. To check if emulation
 * is running we simply try to open the library. 
 * 
 * NOTE: The VERY FIRST attempt to open the library will actually create
 *       it, so you should always try to open it twice.
 * 
 * Functions:            Offset  Purpose                      Arguments
 * ----------
 * D0 = GetVersion()      -0x1E Returns UAE version               -
 * D0 = GetUaeConfig(A0)  -0x24 Get UAE Configuration       Pointer to memory
 *                                                          block with the size
 *                                                          of sizeof(UaeCfg)
 *!D0 = SetUaeConfig(A0)  -0x2A Sets UAE Configuration      Pointer to struct
 *                                                          UAE_CONFIG
 *!D0 = HardReset()       -0x30 Resets the Amiga                  - 
 *!D0 = Reset()           -0x36  -- "" --                         -
 * D0 = EjectDisk(D0)     -0x3C Ejects a disk               D0 = Drive Number
 * D0 = InsertDisk(A0,D0) -0x42 Inserts a disk              D0 = drive number,
 *                                                          A0 = ptr to name
 * D0 = EnableSound()     -0x48 Enables SOUND (if it was          -
 *                              compiled in)	   
 * D0 = DisableSound()    -0x4E Disables SOUND                    -
 * D0 = EnableJoystick()  -0x54 Enables FAKE_JOYSTICK             -
 * D0 = DisableJoystick() -0x5A Disables FAKE_JOYSTICK            -
 * D0 = SetFrameRate(D0)  -0x60 Changes frame rate          D0 = Framerate
 *!D0 = ChgCMemSize(D0)   -0x66 Changes ChipMemSize         D0 = New Mem Size
 *                              (DOES A HARD RESET!)
 *!D0 = ChgSMemSize(D0)   -0x6C Changes SlowMemSize         D0 = New Mem Size
 *                              (DOES A HARD RESET!)
 *!D0 = ChgFMemSize(D0)   -0x72 Changes FastmemSize         D0 = New Mem Size
 *                              (DOES A HARD RESET!)
 * D0 = ChangeLanguage(D0)-0x78 Changes kbd lang.           D0 = Language 
 * D0 = ExitEmu()         -0x7E Exits the emulator                -
 * D0 = GetDisk(D0,A0)    -0x84 Gets the disks name         D0 = Drive
 *                                                          A0 = Space for name
 * D0 = Debug()           -0x8A Enters internal debugger          -
 * ! means not implemented yet 
 */

#include "sysconfig.h"
#include "sysdeps.h"

#include <assert.h>
#include <string.h>

#include "config.h"
#include "options.h"

#include "memory.h"
#include "custom.h"
#include "readcpu.h"
#include "newcpu.h"
#include "xwin.h"
#include "autoconf.h"
#include "disk.h"
#include "os.h"

extern int quit_program;

static ULONG LibBase;

static ULONG opencount=0;
static ULONG emulibname, functable,resname,resid, datatable;
static CPTR GetVersion,HardReset,Reset,EnableSound,DisableSound,
            EnableJoystick,DisableJoystick,SetFrameRate,ChangeLanguage,
            ChgCMemSize,ChgSMemSize,ChgFMemSize,EjectDisk,InsertDisk,
            ExitEmu, GetUaeConfig, SetUaeConfig,Open,Close,Expunge,Null,
            GetDisk,FakeInit,segList,DebugFunc;

/*
 * Library "open"
 */
static ULONG emulib_Open(void)
{
       opencount++;
       put_word( LibBase + 32, get_word( LibBase + 32) + 1 );
       return LibBase;
}

static ULONG emulib_Close(void)
{
       if (opencount <= 1)
	 return 0;
       else {
	      opencount--;
	      put_word( LibBase + 32, get_word( LibBase + 32) - 1 );
	      return 0;
       }
}
static ULONG emulib_Expunge(void)
{
       return 0;
}

static ULONG emulib_Null(void)
{
       return 0;
}

/*
 * Returns UAE Version
 */
static ULONG emulib_GetVersion(void)
{
       return version;
}

/*
 * Resets your amiga
 * 
 * DOES NOT WORK YET ! It obviously crashes when this routine returns().. ?
 */
static ULONG emulib_HardReset(void)
{
       m68k_reset();
       return 0;
}

static ULONG emulib_Reset(void)
{
       m68k_reset();
       return 0;
}

/*
 * Enables SOUND
 */
static ULONG emulib_EnableSound(void)
{
    if (!sound_available || produce_sound == 2)
	return 0;

    produce_sound = 2;
    return 1;
}

/*
 * Disables SOUND
 */
static ULONG emulib_DisableSound(void)
{
    produce_sound = 1;
    return 1;
}

/*
 * Enables FAKE JOYSTICK
 */
static ULONG emulib_EnableJoystick(void)
{
    fake_joystick = 1;
    return 1;
}

/*
 * Disables FAKE JOYSTICK
 */
static ULONG emulib_DisableJoystick(void)
{
       fake_joystick = 0;
       return 1;
}

/*
 * Sets the framerate
 */
static ULONG emulib_SetFrameRate(void)
{
    if (m68k_dreg(regs, 0) == 0)
	return 0;
    else if (m68k_dreg(regs, 0) > 20)
	return 0;
    else {
	framerate = m68k_dreg(regs, 0);
	return 1;
    }
}

/*
 * Changes keyboard language settings
 */
static ULONG emulib_ChangeLanguage(void)
{
    if (m68k_dreg(regs, 0) > 5)
	return 0;
    else {
	switch( m68k_dreg(regs, 0) ) {
	 case 0:
	    keyboard_lang = KBD_LANG_US;
	    break;
	 case 1:
	    keyboard_lang = KBD_LANG_DE;
	    break;
	 case 2:
	    keyboard_lang = KBD_LANG_SE;
	    break;
	 case 3:
	    keyboard_lang = KBD_LANG_FR;
	    break;
	 case 4:
	    keyboard_lang = KBD_LANG_IT;
	    break;
	 case 5:
	    keyboard_lang = KBD_LANG_ES;
	    break;
	 default:
	    break;
	}
	return 1;
    }
}

/*
 * Changes CHIPMEMORY Size
 *  (reboots)
 * 
 * DOES NOT WORK YET !  - The m68k_reset does not work from here
 */
static ULONG emulib_ChgCMemSize(void)
{
    ULONG memsize = m68k_dreg(regs, 0);
       
    if (memsize != 0x80000 && memsize != 0x100000 && 
	memsize != 0x200000) {
	memsize = 0x200000;
	fprintf(stderr, "Unsupported chipmem size!\n");
    }
    m68k_dreg(regs, 0) = 0;
    
    chipmem_size = memsize;
    m68k_reset();
    return 1;
}

/*
 * Changes SLOWMEMORY Size
 *  (reboots)
 * 
 * DOES NOT WORK YET! - the m68k_reset does not work from here
 */
static ULONG emulib_ChgSMemSize(void)
{
       ULONG memsize = m68k_dreg(regs, 0);
       
       if (memsize != 0x80000 && memsize != 0x100000 &&
	   memsize != 0x180000 && memsize != 0x1C0000) {
	      memsize = 0;
	      fprintf(stderr, "Unsupported bogomem size!\n");
       }
       
       m68k_dreg(regs, 0) = 0;
       bogomem_size = memsize;
       m68k_reset();
       return 1;
}

/* 
 * Changes FASTMEMORY Size
 *  (reboots)
 * 
 * DOES NOT WORK YET! - the m68k_reset() does not work from here
 */
static ULONG emulib_ChgFMemSize(void)
{
       ULONG memsize = m68k_dreg(regs, 0);
       
       if (memsize != 0x100000 && memsize != 0x200000 &&
	   memsize != 0x400000 && memsize != 0x800000) {
	      memsize = 0;
	      fprintf(stderr, "Unsupported fastmem size!\n");
       }
       m68k_dreg(regs, 0) = 0;
       fastmem_size = memsize;
       m68k_reset();
       return 0;
}

/*
 * Ejects a disk
 */
static ULONG emulib_EjectDisk(void)
{
    ULONG drive = m68k_dreg(regs, 0);
       
    if (drive > 3)
	return 0;
    
    disk_eject( drive );
    return 1;
}

/*
 * Inserts a disk
 */
static ULONG emulib_InsertDisk(void)
{
    int    quit = 0,i=0,j;
    char   real_name[256];
    UBYTE  abyte;
    ULONG drive = m68k_dreg(regs, 0);
    CPTR  name = m68k_areg(regs, 0);
    FILE   *file;
       
    if (drive > 3)
	return 0;

    if  (!disk_empty( drive )) {
	disk_eject( drive );
    }
       
    while( quit == 0 ) {
	abyte = get_byte(name + i);
	if (abyte == '\0') {
	    real_name[i++] = '\0';
	    quit = 1;
	} else {
	    real_name[i++] = abyte;
	}
    }
    j = strlen( real_name );
    if (j <= 0 || j > 255)
	return 0;

    if (!(file = fopen(real_name, "r")))
	return 0;

    fclose(file);
    disk_insert(drive, real_name);
    return 1;

}

/*
 * Exits the emulator
 */
static ULONG emulib_ExitEmu(void)
{
       broken_in = 1;
       regs.spcflags |= SPCFLAG_BRK;
       quit_program = 1;
       return 1;
}

/*
 * Gets UAE Configuration
 */
static ULONG emulib_GetUaeConfig(void)
{
       int i,j;
       CPTR place = m68k_areg(regs, 0);

       put_long(place     , version);
       put_long(place +  4, chipmem_size);
       put_long(place +  8, bogomem_size);
       put_long(place + 12, fastmem_size);
       put_long(place + 16, framerate);
       put_long(place + 20, produce_sound);
       put_long(place + 24, fake_joystick);
       put_long(place + 28, keyboard_lang);
       if (disk_empty(0))
	 put_byte(place + 32, 0);
       else
	 put_byte(place + 32, 1);
       if (disk_empty(1))
	 put_byte(place + 33, 0);
       else
	 put_byte(place + 33, 1);
       if (disk_empty(2))
	 put_byte(place + 34, 0);
       else
	 put_byte(place + 34, 1);
       if (disk_empty(3))
	 put_byte(place + 35, 0);
       else
	 put_byte(place + 35, 1);
       
       for(i=0;i<256;i++) {
	      put_byte((place + 36 + i), df0[i]);
	      put_byte((place + 36 + i + 256), df1[i]);
	      put_byte((place + 36 + i + 512), df2[i]);
	      put_byte((place + 36 + i + 768), df3[i]);
       }
       return 1;
}

/*
 * Sets UAE Configuration
 * 
 * NOT IMPLEMENTED YET
 */
static ULONG emulib_SetUaeConfig(void)
{
       return 1;
}

/*
 * Gets the name of the disk in the given drive
 */
static ULONG emulib_GetDisk(void)
{
    int i;
    if (m68k_dreg(regs, 0) > 3) 
	return 0;

    switch( m68k_dreg(regs, 0) ) {
     case 0:
	for(i=0;i<256;i++) {
	    put_byte( m68k_areg(regs, 0) + i, df0[i] );
	}
	break;
     case 1:
	for(i=0;i<256;i++) {
	    put_byte( m68k_areg(regs, 0) + i, df1[i] );
	}
	break;
     case 2:
	for(i=0;i<256;i++) {
	    put_byte( m68k_areg(regs, 0) + i, df2[i] );
	}
	break;
     case 3:
	for(i=0;i<256;i++) {
	    put_byte( m68k_areg(regs, 0) + i, df3[i] );
	}
	break;
     default:
	break;
    }
    return 1;
}

/*
 * Enter debugging state
 */
static ULONG emulib_Debug(void)
{
    broken_in = 1;
    regs.spcflags |= SPCFLAG_BRK;
    return 1;
}

static ULONG emulib_FakeInit(void)
{
    segList = m68k_areg(regs, 0);
    return m68k_dreg(regs, 0);
}

/*
 * Creates the UAE.library in memory
 */
static ULONG emulib_Init(void)
{
    ULONG tmp1, quit,i;
    UBYTE bitti;
    ULONG dosbase;

    if (LibBase != 0)
	return 0;
       
    m68k_areg(regs, 0) = functable;
    m68k_areg(regs, 1) = datatable;
    m68k_areg(regs, 2) = FakeInit;
    m68k_dreg(regs, 0) = 1024;
    m68k_dreg(regs, 1) = 0;
    tmp1 = CallLib(m68k_areg(regs, 6), -0x54);
    if (tmp1 == 0) {
	fprintf(stderr, "Cannot create UAE.library! ");
	return 0;
    }
    m68k_areg(regs, 1) = tmp1;
    CallLib(m68k_areg(regs, 6), -0x18c);
    LibBase = tmp1;
#if 0
    m68k_areg(regs, 1) = ds("dos.library");
    m68k_dreg(regs, 0) = 0;
    dosbase = CallLib(m68k_areg(regs, 6), -552);
    printf("%08lx\n", dosbase);
#endif
    m68k_dreg(regs, 0) = 1;
    return 0;
}


/*
 * Installs the UAE LIBRARY
 */
void emulib_install(void)
{
    ULONG begin, end, inittable, initroutine;
    ULONG jotain, func_place, data_place, init_place;
       
    resname = ds("uae.library");
    resid = ds("UAE library 0.1");
       
    begin = here();
    dw(0x4AFC);
    dl(begin);
    dl(0);
    dw(0x8001);
    dw(0x0988);
    dl(resname);
    dl(resid);
    dl(here() + 4);

    dl(1024);
    func_place = here();
    dl(0);
    data_place = here();
    dl(0);
    init_place = here();
    dl(0);
 
/* Code to set up our library */

    initroutine = here();
    calltrap(deftrap(emulib_Init));
    dw(RTS);
       
    /* Function table */
    Open = here();
    calltrap(deftrap(emulib_Open));
    dw(RTS);
       
    Close = here();
    calltrap(deftrap(emulib_Close));
    dw(RTS);
       
    Expunge = here();
    calltrap(deftrap(emulib_Expunge));
    dw(RTS);
       
    Null = here();
    dw(0x203c); dl(1);
    dw(RTS);

    GetVersion = here();
    calltrap(deftrap(emulib_GetVersion));
    dw(RTS);
       
    GetUaeConfig = here();
    calltrap(deftrap(emulib_GetUaeConfig));
    dw(RTS);
       
    SetUaeConfig = here();
    calltrap(deftrap(emulib_SetUaeConfig));
    dw(RTS);

    HardReset = here();
    calltrap(deftrap(emulib_HardReset));
    dw(RTS);
       
    Reset = here();
    calltrap(deftrap(emulib_Reset));
    dw(RTS);
       
    EjectDisk = here();
    calltrap(deftrap(emulib_EjectDisk));
    dw(RTS);
       
    InsertDisk = here();
    calltrap(deftrap(emulib_InsertDisk));
    dw(RTS);
       
    EnableSound = here();
    calltrap(deftrap(emulib_EnableSound));
    dw(RTS);
       
    DisableSound = here();
    calltrap(deftrap(emulib_DisableSound));
    dw(RTS);
       
    EnableJoystick = here();
    calltrap(deftrap(emulib_EnableJoystick));
    dw(RTS);

    DisableJoystick = here();
    calltrap(deftrap(emulib_DisableJoystick));
    dw(RTS);
       
    SetFrameRate = here();
    calltrap(deftrap(emulib_SetFrameRate));
    dw(RTS);
       
    ChgCMemSize = here();
    calltrap(deftrap(emulib_ChgCMemSize));
    dw(RTS);
       
    ChgSMemSize = here();
    calltrap(deftrap(emulib_ChgSMemSize));
    dw(RTS);
       
    ChgFMemSize = here();
    calltrap(deftrap(emulib_ChgFMemSize));
    dw(RTS);
       
    ChangeLanguage = here();
    calltrap(deftrap(emulib_ChangeLanguage));
    dw(RTS);

    ExitEmu = here();
    calltrap(deftrap(emulib_ExitEmu));
    dw(RTS);

    GetDisk = here();
    calltrap(deftrap(emulib_GetDisk));
    dw(RTS);

    DebugFunc = here();
    calltrap(deftrap(emulib_Debug));
    dw(RTS);
       
    FakeInit = here();
    calltrap(deftrap(emulib_FakeInit));
    dw(RTS);
       
    functable = here();
    dl(Open);
    dl(Close);
    dl(Expunge);
    dl(Null);
    dl(GetVersion);
    dl(GetUaeConfig);
    dl(SetUaeConfig);
    dl(HardReset);
    dl(Reset);
    dl(EjectDisk);
    dl(InsertDisk);
    dl(EnableSound);
    dl(DisableSound);
    dl(EnableJoystick);
    dl(DisableJoystick);
    dl(SetFrameRate);
    dl(ChgCMemSize);
    dl(ChgSMemSize);
    dl(ChgFMemSize);
    dl(ChangeLanguage);
    dl(ExitEmu);
    dl(GetDisk);
    dl(DebugFunc);
    dl(0xFFFFFFFF);

    datatable = here();
    dw(0xE000);
    dw(0x0008);
    dw(0x0900);
    dw(0xC000);
    dw(0x000A);
    dl(resname);
    dw(0xE000);
    dw(0x000E);
    dw(0x0600);
    dw(0xD000);
    dw(0x0014);
    dw(0x0001);
    dw(0xD000);
    dw(0x0000);
    dw(0x0000);
    dw(0xC000);
    dw(0x0018);
    dl(resid);
    dl(0x00000000);
    
    end = here();

    org(begin + 6);           /* Load END value */
    dl(end);

    org(data_place);
    dl(datatable);
       
    org(func_place);
    dl(functable);

    org(init_place);
    dl(initroutine);
       
    org(end);
}
