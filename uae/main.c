 /*
  * UAE - The Un*x Amiga Emulator
  * 
  * Main program 
  * 
  * (c) 1995 Bernd Schmidt, Ed Hanway
  */

#include "sysconfig.h"
#include "sysdeps.h"
#include <assert.h>

#include "config.h"
#include "options.h"
#include "uae.h"
#include "events.h"
#include "memory.h"
#include "custom.h"
#include "serial.h"
#include "readcpu.h"
#include "newcpu.h"
#include "disk.h"
#include "debug.h"
//#include "xwin.h"
#include "os.h"
#include "keybuf.h"
#include "gui.h"
#include "zfile.h"
#include "autoconf.h"
#include "osemu.h"
#include "compiler.h"

int version = 100*UAEMAJOR + 10*UAEMINOR + UAEURSAMINOR;
int framerate = 1;
int use_debugger = 0;
int illegal_mem = 0;
int use_gfxlib = 0;
int no_xhair = 0;
int use_serial = 0;
int automount_uaedev = 1;
int produce_sound = 0;
int fake_joystick = 0;
KbdLang keyboard_lang = KBD_LANG_US;
int correct_aspect = 0;
int color_mode = 0;
int sound_desired_bits = 16;
#ifdef AMIGA
int sound_desired_freq = 11025;
#else
int sound_desired_freq = 44100;
#endif
int sound_desired_bsiz = 8192;
int allow_save = 0;
int no_gui = 0;
int emul_accuracy = 2;
int test_drawing_speed = 0;
int gfx_requested_width = 800, gfx_requested_height = 600, gfx_requested_lores = 0;
int gfx_requested_linedbl = 0, gfx_requested_correct_aspect = 0;
int gfx_requested_xcenter = 0, gfx_requested_ycenter = 0;
int immediate_blits = 0, blits_32bit_enabled = 0;
long hardfile_size = 0;

ULONG fastmem_size = 0x000000;
ULONG chipmem_size = 0x200000;
ULONG bogomem_size = 0x000000;
char df0[256]="df0.adf", df1[256]="df1.adf", df2[256]="df2.adf", df3[256]="df3.adf";
char romfile[256] = "kick.rom";
#ifndef __DOS__
char prtname[256] = "lpr ";
#else
char prtname[256] = "PRN";
#endif

char optionsfile[256];

/* If you want to pipe Printer output to a file, put something like
 * "cat >>printerfile.tmp" above.
 * The printer support was only tested with the driver "PostScript" on
 * Amiga side, using apsfilter for linux to print ps-data.
 *
 * Under DOS it ought to be -p LPT1: or -p PRN: but you'll need a 
 * PostScript printer or ghostscript -=SR=-
 */

/* People must provide their own name for this */
char sername[256] = "";

static void fix_options(void)
{
}

void usage(void)
{
    printf("UAE - The Un*x Amiga emulator\n");
    printf("Summary of command-line options:\n");
    printf("  -h           : Print help\n");
    printf("  -m VOL:dir   : mount directory called <dir> as AmigaDOS volume VOL:\n");
    printf("  -M VOL:dir   : like -m, but mount read-only\n");
    printf("  -s n         : Emulate n*256 KB slow memory at 0xC00000\n");
    printf("  -c n         : Emulate n*512 KB chip memory at 0x000000\n");
    printf("  -F n         : Emulate n MB fast memory at 0x200000\n");
    printf("  -a           : Add no expansion devices (disables fastmem and\n"
	   "                 harddisk support\n");
    printf("  -J           : Fake joystick emulation with the numeric pad\n");
    printf("  -f n         : Set the frame rate to 1/n\n");
    printf("  -D           : Start up the built-in debugger\n");
    printf("  -i           : Print illegal memory accesses\n");
    printf("  -o           : Allow options to be saved\n");
    printf("  -G           : Disable user interface\n");
    printf("  -A n         : Set emulator accuracy to n (0, 1 or 2)\n");
    printf("  -[0123] file : Use file instead of df[0123].adf as disk image\n");
    printf("  -r file      : Use file as ROM image instead of kick.rom\n");
    target_specific_usage();
/*    printf("  -g           : Turn on gfx-lib replacement (EXPERIMENTAL).\n");*/
/*    printf("  -d mode      : OBSOLETE: Use \"-O\".\n");
    printf("  -C           : OBSOLETE: use \"-O\"\n"); */
    printf("  -n parms     : Set blitter parameters: 'i' enables immediate blits,\n"
	   "                 '3' enables 32 bit blits (may crash RISC machines)\n");
    printf("  -O modespec  : Define graphics mode (see below).\n");
    printf("  -H mode      : Set the color mode (see below).\n");
    printf("\n"
	   "Valid color modes: 0 (256 colors); 1 (32768 colors); 2 (65536 colors)\n"
	   "                   3 (256 colors, with dithering for better results)\n"
	   "                   4 (16 colors, dithered); 5 (16 million colors)\n"
	   "The format for the modespec parameter of \"-O\" is as follows:\n"
	   "  -O width:height:modifiers\n"
	   "  \"width\" and \"height\" specify the dimensions of the picture.\n"
	   "  \"modifiers\" is a string that contains zero or more of the following\n"
	   "  characters:\n"
           "   l:    Treat the display as lores, drawing only every second pixel\n"
	   "   x, y: Center the screen horizontally or vertically.\n"
	   "   d:    Draw every line twice unless in interlace: this doubles the height\n"
	   "         of the display (this is the old -C parameter).\n"
	   "   c:    Correct aspect ratio (this is _not_ the old -C parameter).\n"
	   "UAE may choose to ignore the color mode setting and/or adjust the\n"
           "video mode setting to reasonable values.\n");
}

static void parse_gfx_specs (char *spec)
{
    char *x0 = my_strdup (spec);
    char *x1, *x2;
    
    x1 = strchr (x0, ':');
    if (x1 == 0)
	goto argh;
    x2 = strchr (x1+1, ':');
    if (x2 == 0)
	goto argh;
    *x1++ = 0; *x2++ = 0;
    
    gfx_requested_width = atoi (x0);
    gfx_requested_height = atoi (x1);
    gfx_requested_lores = strchr (x2, 'l') != 0;
    gfx_requested_xcenter = strchr (x2, 'x') != 0 ? 1 : strchr (x2, 'X') != 0 ? 2 : 0;
    gfx_requested_ycenter = strchr (x2, 'y') != 0 ? 1 : strchr (x2, 'Y') != 0 ? 2 : 0;
    gfx_requested_linedbl = strchr (x2, 'd') != 0;
    gfx_requested_correct_aspect = strchr (x2, 'c') != 0;
    
    free (x0);
    return;
    
    argh:
    fprintf (stderr, "Bad display mode specification.\n");
    fprintf (stderr, "The format to use is: \"width:height:modifiers\"\n");
    free (x0);
}

#if defined(__unix)||defined(AMIGA)

void parse_cmdline(int argc, char **argv)
{
    int c;
    extern char *optarg;
#ifdef __BEOS__
    if (argc < 2)
	return;
#endif
    /* Help! We're running out of letters! */
    while(((c = getopt(argc, argv, "l:Dif:gd:hxF:as:c:S:Jm:M:0:1:2:3:r:H:p:C:I:b:R:B:oGA:tO:n:")) != EOF))
    switch(c) {
     case 'h': usage();	exit(0);

     case '0': strncpy(df0, optarg, 255); df0[255] = 0;	break;
     case '1': strncpy(df1, optarg, 255); df1[255] = 0; break;
     case '2': strncpy(df2, optarg, 255); df2[255] = 0; break;
     case '3': strncpy(df3, optarg, 255); df3[255] = 0; break;
     case 'r': strncpy(romfile, optarg, 255); romfile[255] = 0; break;
     case 'p': strncpy(prtname, optarg, 255); prtname[255] = 0; break;
     case 'I': strncpy(sername, optarg, 255); sername[255] = 0; use_serial = 1; break;
     case 'm':
     case 'M':
	{
	    char buf[256];
	    char *s2;
	    int readonly = (c == 'M');

	    strncpy(buf, optarg, 255); buf[255] = 0;
	    s2 = strchr(buf, ':');
	    if(s2) {
		*s2++ = '\0';
#ifdef __DOS__
		{
		    char *tmp;

		    while ((tmp = strchr(s2, '\\')))
			*tmp = '/';
		}
#endif
		add_filesys_unit(buf, s2, readonly);
	    } else {
		fprintf(stderr, "Usage: [-m | -M] VOLNAME:/mount_point\n");
	    }
	}
	break;
	
     case 'S': produce_sound = atoi(optarg); break;
     case 'f': framerate = atoi(optarg); break;
     case 'A': emul_accuracy = atoi(optarg); break;
     case 'x': no_xhair = 1; break;
     case 'D': use_debugger = 1; break;
     case 'i': illegal_mem = 1; break;
     case 'J': fake_joystick = 1; break;
     case 'a': automount_uaedev = 0; break;
     case 'g': use_gfxlib = 1; break;
     case 'o': allow_save = 1; break;
     case 'G': no_gui = 1; break;
     case 't': test_drawing_speed = 1; break;

     case 'n':
	if (strchr (optarg, '3') != 0)
	    blits_32bit_enabled = 1;
	if (strchr (optarg, 'i') != 0)
	    immediate_blits = 0;
	break;

     case 'C':
	fprintf (stderr, "The -C option is obsolete, please use -O.\n");
	correct_aspect = atoi(optarg);
	if (correct_aspect < 0 || correct_aspect > 2) {
	    fprintf(stderr, "Bad parameter for -C !\n");
	    correct_aspect = 0;
	}
	break;

     case 'F':
	fastmem_size = atoi(optarg) * 0x100000;
	if (fastmem_size != 0x100000 && fastmem_size != 0x200000 
	    && fastmem_size != 0x400000 && fastmem_size != 0x800000) 
	{
	    fastmem_size = 0;
	    fprintf(stderr, "Unsupported fastmem size!\n");
	}	
	break;

     case 's':
	bogomem_size = atoi(optarg) * 0x40000;
	if (bogomem_size != 0x80000 && bogomem_size != 0x100000
	    /* Braino && bogomem_size != 0x180000 && bogomem_size != 0x1C0000*/)
	{
	    bogomem_size = 0;
	    fprintf(stderr, "Unsupported bogomem size!\n");
	}
	break;

     case 'c':
	chipmem_size = atoi(optarg) * 0x80000;
	if (chipmem_size != 0x80000 && chipmem_size != 0x100000
	    && chipmem_size != 0x200000 && chipmem_size != 0x400000
	    && chipmem_size != 0x800000)
	{
	    chipmem_size = 0x200000;
	    fprintf(stderr, "Unsupported chipmem size!\n");
	}
	
	break;

     case 'l':
	if (0 == strcasecmp(optarg, "de"))
	    keyboard_lang = KBD_LANG_DE;
	else if (0 == strcasecmp(optarg, "us"))
	    keyboard_lang = KBD_LANG_US;
	else if (0 == strcasecmp(optarg, "se"))
	    keyboard_lang = KBD_LANG_SE;
	else if (0 == strcasecmp(optarg, "fr"))
	    keyboard_lang = KBD_LANG_FR;
	else if (0 == strcasecmp(optarg, "it"))
	    keyboard_lang = KBD_LANG_IT;
	else if (0 == strcasecmp(optarg, "es"))
	    keyboard_lang = KBD_LANG_ES;
	break;

     case 'O': parse_gfx_specs (optarg); break;
     case 'd':
	fprintf (stderr, "Note: The -d option is obsolete, please use the new -O format.\n");
	c = atoi(optarg);
	switch (c) {
	 case 0:
	    parse_gfx_specs("320:200:lx");
	    break;
	 case 1:
	    parse_gfx_specs("320:240:lx");
	    break;
	 case 2:
	    parse_gfx_specs("320:400:lx");
	    break;
	 case 3:
	    parse_gfx_specs("640:300:x");
	    break;
	 default:
	    fprintf(stderr, "Bad video mode selected. Using default.\n");
	    /* fall through */
	 case 4:
	    parse_gfx_specs("800:600:");
	    break;
	}
	break;

     case 'H':
	color_mode = atoi(optarg);
	if (color_mode < 0 || color_mode > MAX_COLOR_MODES) {
	    fprintf(stderr, "Bad color mode selected. Using default.\n");
	    color_mode = 0;
	}
	break;

     case 'b': sound_desired_bits = atoi(optarg); break;
     case 'B': sound_desired_bsiz = atoi(optarg); break;
     case 'R': sound_desired_freq = atoi(optarg); break;
    }
}
#endif

static void parse_cmdline_and_init_file(int argc, char **argv)
{
    FILE *f;
    char *home;
    char *buffer,*tmpbuf, *token;
    char smallbuf[256];
    int bufsiz, result;
    int n_args;
    char **new_argv;
    int new_argc;

    strcpy(optionsfile,"");

#if !defined(__DOS__) && !defined(__mac__)
    home = getenv("HOME");
    if (home != NULL && strlen(home) < 240)
    {
	strcpy(optionsfile, home);
	strcat(optionsfile, "/");
    }
#endif

#ifndef __DOS__
    strcat(optionsfile, ".uaerc");
#else
    strcat(optionsfile, "uae.rc");
#endif

    f = fopen(optionsfile,"rb");
#ifndef __DOS__
/* sam: if not found in $HOME then look in current directory */
    if (f == NULL) {
        f = fopen(".uaerc","rb");
    }
#endif

    if (f == NULL) {
	parse_cmdline(argc, argv);
	return;
    }
    fseek(f, 0, SEEK_END);
    bufsiz = ftell(f);
    fseek(f, 0, SEEK_SET);

    buffer = (char *)malloc(bufsiz+1);
    buffer[bufsiz] = 0;
    if (fread(buffer, 1, bufsiz, f) < bufsiz) {
	fprintf(stderr, "Error reading configuration file\n");
	fclose(f);
	parse_cmdline(argc, argv);
	return;
    }
    fclose(f);

#ifdef __DOS__
    {
	char *tmp;

	while ((tmp = strchr(buffer, 0x0d)))
	    *tmp = ' ';
	while ((tmp = strchr(buffer, 0x0a)))
	    *tmp = ' ';
	while (buffer[0] == ' ')
	    strcpy(buffer, buffer+1);
	while ((strlen(buffer) > 0) && (buffer[strlen(buffer) - 1] == ' '))
	    buffer[strlen(buffer) - 1] = '\0';
	while ((tmp = strstr(buffer, "  ")))
	    strcpy(tmp, tmp+1);
    }
#endif

    tmpbuf = my_strdup (buffer);

    n_args = 0;
    if (strtok(tmpbuf, "\n ") != NULL) {
	do {
	    n_args++;
	} while (strtok(NULL, "\n ") != NULL);
    }
    free (tmpbuf);

    new_argv = (char **)malloc ((1 + n_args + argc) * sizeof (char **));
    new_argv[0] = argv[0];
    new_argc = 1;

    token = strtok(buffer, "\n ");
    while (token != NULL) {
	new_argv[new_argc] = my_strdup (token);
	new_argc++;
	token = strtok(NULL, "\n ");
    }
    for (n_args = 1; n_args < argc; n_args++)
	new_argv[new_argc++] = argv[n_args];
    new_argv[new_argc] = NULL;
    parse_cmdline(new_argc, new_argv);
}

#define ARE_YOU_NUTS 0

/* Okay, this stuff looks strange, but it is here to encourage people who
 * port UAE to re-use as much of this code as possible. Functions that you
 * should be using are do_start_program() and do_leave_program(), as well
 * as real_main(). Some OSes don't call main() (which is braindamaged IMHO,
 * but unfortunately very common), so you need to call real_main() from
 * whatever entry point you have. You may want to write your own versions
 * of start_program() and leave_program() if you need to do anything special.
 * Add #ifdefs around these as appropriate.
 */

void do_start_program(void)
{
#ifdef USE_EXECLIB
    if (ARE_YOU_NUTS && use_gfxlib)
	execlib_sysinit();
    else
#endif
	m68k_go(1);
}

void do_leave_program(void)
{
    graphics_leave();
    close_joystick();
    dump_counts();
    serial_exit();
    zfile_exit();
    if (!no_gui)
	gui_exit();    
}

void start_program(void)
{
    do_start_program();
}

void leave_program(void)
{
    do_leave_program();
}

void real_main(int argc, char **argv)
{
    FILE *hf;

    hf = fopen("hardfile", "rb");
    if (hf == NULL)
	hardfile_size = 0;
    else {
	fseek(hf, 0, SEEK_END);
	hardfile_size = ftell(hf);
	fclose(hf);
    }

    rtarea_init ();
    hardfile_install ();

    parse_cmdline_and_init_file(argc, argv);

    if (!init_sound()) {
	fprintf(stderr, "Sound driver unavailable: Sound output disabled\n");
	produce_sound = 0;
    }
    init_joystick();

    if (!no_gui) {
	int err = gui_init();
	if (err == -1) {
	    fprintf(stderr, "Failed to initialize the GUI\n");
	} else if (err == -2) {
	    exit(0);
	}
    }

    fix_options();
    
    /* Install resident module to get 8MB chipmem, if requested */
    rtarea_setup();
    
    keybuf_init();
    
    expansion_init ();
    memory_init();

    filesys_install();
#ifdef USE_EXECLIB
    execlib_install();
#endif
    gfxlib_install();
    emulib_install();
    trackdisk_install();

    custom_init();
    serial_init();
    DISK_init();
    init_m68k();
    compiler_init();
    gui_update();
    
    if (graphics_init()) {
	customreset();
	m68k_reset();
	
	setup_brkhandler();
	if (use_debugger && debuggable())
	    activate_debugger();

	start_program();
    }
    leave_program();
}

#ifndef __BEOS__  /* BeOS needs its own startup code */
int main(int argc, char **argv)
{
    real_main(argc, argv);
    return 0;
}
#endif /* not __BEOS__ */
