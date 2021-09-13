 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Stuff
  *
  * Copyright 1995, 1996 Bernd Schmidt, Ed Hanway
  */

#ifndef BASILISK
#define UAEMAJOR 0
#define UAEMINOR 6
#define UAEURSAMINOR 8
#else
#define BASILISK_VERSION 0
#define BASILISK_REVISION 6
#endif

typedef enum { KBD_LANG_US, KBD_LANG_DE, KBD_LANG_SE, KBD_LANG_FR, KBD_LANG_IT, KBD_LANG_ES } KbdLang;

#ifndef BASILISK
extern int version;
extern int framerate;
extern int produce_sound;
extern int correct_aspect;
extern int use_fast_draw;
extern int use_debugger;
extern int use_serial;
extern int illegal_mem;
extern int use_fast_mem;
extern int use_gfxlib;
extern int no_xhair;
extern int automount_uaedev;
extern int fake_joystick;
extern KbdLang keyboard_lang;
extern int color_mode;
extern int sound_desired_bits;
extern int sound_desired_freq;
extern int sound_desired_bsiz;
extern int allow_save;
extern int no_gui;
extern int test_drawing_speed;
extern int emul_accuracy;
extern long hardfile_size;
extern int gfx_requested_width, gfx_requested_height, gfx_requested_lores;
extern int gfx_requested_linedbl, gfx_requested_correct_aspect;
extern int gfx_requested_xcenter, gfx_requested_ycenter;
extern int blits_32bit_enabled, immediate_blits;
#else
extern int framerate;
extern int use_debugger;
extern int illegal_mem;
extern int boot_edisk;
extern char pramfile[];
#endif

/* AIX doesn't think it is Unix. Neither do I. */
#if defined(_ALL_SOURCE) || defined(_AIX)
#undef __unix
#define __unix
#endif

extern char df0[], df1[], df2[], df3[], romfile[], prtname[], sername[];

#define MAX_COLOR_MODES 5

extern int fast_memcmp(const void *foo, const void *bar, int len);
extern int memcmpy(void *foo, const void *bar, int len);

/* strdup() may be non-portable if you have a weird system */
static char *my_strdup(const char*s)
{
    /* The casts to char * are there to shut up the compiler on HPUX */
    char *x = (char*)malloc(strlen((char *)s) + 1);
    strcpy(x, (char *)s);
    return x;
}

/*
 * You can specify numbers from 0 to 5 here. It is possible that higher
 * numbers will make the CPU emulation slightly faster, but if the setting
 * is too high, you will run out of memory while compiling.
 * Best to leave this as it is.
 */
#define CPU_EMU_SIZE 0

#ifdef DONT_WANT_SOUND
#undef LINUX_SOUND
#undef AF_SOUND
#undef SOLARIS_SOUND
#undef SGI_SOUND
#endif

/* #define NEED_TO_DEBUG_BADLY */

#undef USE_EXECLIB

#if !defined(USER_PROGRAMS_BEHAVE)
#define USER_PROGRAMS_BEHAVE 0
#endif

#define QUADRUPLIFY(c) (((c) | ((c) << 8)) | (((c) | ((c) << 8)) << 16))

/* When you call this routine, bear in mind that it rounds the bounds and
 * may need some padding for the array. */

#define fuzzy_memset(p, c, o, l) fuzzy_memset_1 ((p), QUADRUPLIFY (c), (o) & ~3, ((l) + 4) >> 2)
static __inline__ void fuzzy_memset_1 (void *p, ULONG c, int offset, int len)
{
    ULONG *p2 = (ULONG *)((char *)p + offset);
    int a = len & 7;
    len >>= 3;
    switch (a) {
     case 7: p2--; goto l1;
     case 6: p2-=2; goto l2;
     case 5: p2-=3; goto l3;
     case 4: p2-=4; goto l4;
     case 3: p2-=5; goto l5;
     case 2: p2-=6; goto l6;
     case 1: p2-=7; goto l7;
     case 0: goto l8;
    }

    for (;;) {
	p2[0] = c;
	l1:
	p2[1] = c;
	l2:
	p2[2] = c;
	l3:
	p2[3] = c;
	l4:
	p2[4] = c;
	l5:
	p2[5] = c;
	l6:
	p2[6] = c;
	l7:
	p2[7] = c;
	l8:
	if (!len)
	    break;
	len--;
	p2 += 8;
    }
}

#define fuzzy_memset_le32(p, c, o, l) fuzzy_memset_le32_1 ((p), QUADRUPLIFY (c), (o) & ~3, ((l) + 4) >> 2)
static __inline__ void fuzzy_memset_le32_1 (void *p, ULONG c, int offset, int len)
{
    ULONG *p2 = (ULONG *)((char *)p + offset);

    switch (len) {
     case 9: p2[0] = c; p2[1] = c; p2[2] = c; p2[3] = c; p2[4] = c; p2[5] = c; p2[6] = c; p2[7] = c; p2[8] = c; break;
     case 8: p2[0] = c; p2[1] = c; p2[2] = c; p2[3] = c; p2[4] = c; p2[5] = c; p2[6] = c; p2[7] = c; break;
     case 7: p2[0] = c; p2[1] = c; p2[2] = c; p2[3] = c; p2[4] = c; p2[5] = c; p2[6] = c; break;
     case 6: p2[0] = c; p2[1] = c; p2[2] = c; p2[3] = c; p2[4] = c; p2[5] = c; break;
     case 5: p2[0] = c; p2[1] = c; p2[2] = c; p2[3] = c; p2[4] = c; break;
     case 4: p2[0] = c; p2[1] = c; p2[2] = c; p2[3] = c; break;
     case 3: p2[0] = c; p2[1] = c; p2[2] = c; break;
     case 2: p2[0] = c; p2[1] = c; break;
     case 1: p2[0] = c; break;
     case 0: break;
     default: printf("Hit the programmer.\n"); break;
    }
}

