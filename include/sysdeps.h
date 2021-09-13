 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Try to include the right system headers and get other system-specific
  * stuff right.
  *
  * Copyright 1996 Bernd Schmidt
  */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_VALUES_H
#include <values.h>
#endif

#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef HAVE_UTIME_H
#include <utime.h>
#endif

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#ifdef HAVE_SYS_VFS_H
#include <sys/vfs.h>
#endif

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#ifdef HAVE_SYS_MOUNT_H
#include <sys/mount.h>
#endif

#ifdef HAVE_SYS_STATFS_H
#include <sys/statfs.h>
#endif

#ifdef HAVE_SYS_STATVFS_H
#include <sys/statvfs.h>
#endif

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#if HAVE_DIRENT_H
# include <dirent.h>
#else
# define dirent direct
# if HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif
# if HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif
# if HAVE_NDIR_H
#  include <ndir.h>
# endif
#endif

#include <errno.h>
#include <assert.h>

#if EEXIST == ENOTEMPTY
#define BROKEN_OS_PROBABLY_AIX
#endif

#ifdef HAVE_LINUX_JOYSTICK_H
#if BROKEN_JOYSTICK_H == 1
#include "joystick.h"
#else
#include <linux/joystick.h>
#endif
#endif

#ifdef __NeXT__
#define S_IRUSR S_IREAD
#define S_IWUSR S_IWRITE
#define S_IXUSR S_IEXEC
#define S_ISDIR(val) (S_IFDIR & val)
struct utimbuf
{
    time_t actime;
    time_t modtime;
};
#endif

/* sam: SAS/C code removed */

#if defined(__GNUC__) && defined(AMIGA)
/* 
 * sam: GCC has a bad feature ("bug" would a better word) for uae in 
 * libamiga.a: bltsize, bltcon0, ... are defined as absolute address
 * in memory, causing real customchip registers being modified when
 * UAE is running. (I hate this feature... It makes  me lost lots of 
 * time looking for a bug on my side =:-( ).
 *
 * This cures the problem (I hope, since I don't exactly know which 
 * variables need to be redefined for UAE):
 */
/* 0.6.1 */
#define bltsize my_bltsize
#define bltcon0 my_bltcon0
#define bltcon1 my_bltcon1
#define bltapt  my_bltapt
#define bltbpt  my_bltbpt
#define bltcpt  my_bltcpt
#define bltdpt  my_bltdpt
#define adkcon  my_adkcon
/* 0.6.3 */
#define serdat  my_serdat
#define dmacon  my_dmacon
#define intena  my_intena
#define intreq  my_intreq

#define AMIGALIB_NEED_TO_BE_REALLY_SURE
/*
 * Those are here is case of doubt (they are all absolute address
 * in libamiga.a, defining CUSTOM_NEED_TO_BE_REALLY_SURE will prevent
 * uae from using any absolute variable.
 */
#ifdef AMIGALIB_NEED_TO_BE_REALLY_SURE 
#define bootrom my_bootrom
#define cartridge my_cartridge
#define romend my_romend
#define romstart my_romstart
#define adkcon my_adkcon
#define adkconr my_adkconr
#define aud my_aud
#define bltadat my_bltadat
#define bltafwm my_bltafwm
#define bltalwm my_bltalwm
#define bltamod my_bltamod
#define bltapt my_bltapt
#define bltbdat my_bltbdat
#define bltbmod my_bltbmod
#define bltbpt my_bltbpt
#define bltcdat my_bltcdat
#define bltcmod my_bltcmod
#define bltcon0 my_bltcon0
#define bltcon1 my_bltcon1
#define bltcpt my_bltcpt
#define bltddat my_bltddat
#define bltdmod my_bltdmod
#define bltdpt my_bltdpt
#define bltsize my_bltsize
#define bpl1mod my_bpl1mod
#define bpl2mod my_bpl2mod
#define bplcon0 my_bplcon0
#define bplcon1 my_bplcon1
#define bplcon2 my_bplcon2
#define bpldat my_bpldat
#define bplpt my_bplpt
#define clxcon my_clxcon
#define clxdat my_clxdat
/* Oh look ! libamiga.a does not allow you to define a shared
   variable caled color.... This is quite annoying :-/ */
#define color my_color
#define cop1lc my_cop1lc
#define cop2lc my_cop2lc
#define copcon my_copcon
#define copins my_copins
#define copjmp1 my_copjmp1
#define copjmp2 my_copjmp2
#define custom my_custom
#define ddfstop my_ddfstop
#define ddfstrt my_ddfstrt
#define diwstop my_diwstop
#define diwstrt my_diwstrt
#define dmacon my_dmacon
#define dmaconr my_dmaconr
#define dskbytr my_dskbytr
#define dskdat my_dskdat
#define dskdatr my_dskdatr
#define dsklen my_dsklen
#define dskpt my_dskpt
#define intena my_intena
#define intenar my_intenar
#define intreq my_intreq
#define intreqr my_intreqr
#define joy0dat my_joy0dat
#define joy1dat my_joy1dat
#define joytest my_joytest
#define pot0dat my_pot0dat
#define pot1dat my_pot1dat
#define potgo my_potgo
#define potinp my_potinp
#define refptr my_refptr
#define serdat my_serdat
#define serdatr my_serdatr
#define serper my_serper
#define spr my_spr
#define sprpt my_sprpt
#define vhposr my_vhposr
#define vhposw my_vhposw
#define vposr my_vposr
#define vposw my_vposw
#define ciaa my_ciaa
#define ciaacra my_ciaacra
#define ciaacrb my_ciaacrb
#define ciaaddra my_ciaaddra
#define ciaaddrb my_ciaaddrb
#define ciaaicr my_ciaaicr
#define ciaapra my_ciaapra
#define ciaaprb my_ciaaprb
#define ciaasdr my_ciaasdr
#define ciaatahi my_ciaatahi
#define ciaatalo my_ciaatalo
#define ciaatbhi my_ciaatbhi
#define ciaatblo my_ciaatblo
#define ciaatodhi my_ciaatodhi
#define ciaatodlow my_ciaatodlow
#define ciaatodmid my_ciaatodmid
#define ciab my_ciab
#define ciabcra my_ciabcra
#define ciabcrb my_ciabcrb
#define ciabddra my_ciabddra
#define ciabddrb my_ciabddrb
#define ciabicr my_ciabicr
#define ciabpra my_ciabpra
#define ciabprb my_ciabprb
#define ciabsdr my_ciabsdr
#define ciabtahi my_ciabtahi
#define ciabtalo my_ciabtalo
#define ciabtbhi my_ciabtbhi
#define ciabtblo my_ciabtblo
#define ciabtodhi my_ciabtodhi
#define ciabtodlow my_ciabtodlow
#define ciabtodmid my_ciabtodmid
#endif /* AMIGALIB_NEED_TO_BE_REALLY_SURE */

/* gcc on the amiga need that __attribute((regparm)) must */
/* be defined in function prototypes as well as in        */
/* function definitions !                                 */
#define REGPARAM2 REGPARAM
#else /* not(GCC & AMIGA) */
#define REGPARAM2
#endif

#ifdef __DOS__
#include <pc.h>
#include <io.h>
#else
#undef O_BINARY
#define O_BINARY 0
#endif

#ifndef EXEC_TYPES_H /* sam: to prevent re-definition in amiga.c! */
/* If char has more then 8 bits, good night. */
typedef unsigned char UBYTE;
typedef signed char BYTE;
typedef struct {UBYTE RGB[3];} RGB;

#if SIZEOF_SHORT == 2
typedef unsigned short UWORD;
typedef short WORD;
#elif SIZEOF_INT == 2
typedef unsigned int UWORD;
typedef int WORD;
#else
#error No 2 byte type, you lose.
#endif

#if SIZEOF_INT == 4
typedef unsigned int ULONG;
typedef int LONG;
#elif SIZEOF_LONG == 4
typedef unsigned long ULONG;
typedef long LONG;
#else
#error No 4 byte type, you lose.
#endif
typedef ULONG CPTR;

#undef INT_64BIT

#if SIZEOF_LONG_LONG == 8
#define INT_64BIT long long
#elif SIZEOF_LONG == 8
#define INT_64BIT long
#endif
#endif
