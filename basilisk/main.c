/*
 *  main.c - Settings/options etc.
 *
 *  Basilisk (C) 1996-1997 Christian Bauer
 */

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"


/*
 *  Emulation settings
 */

int framerate = 4;
int use_debugger = 0;
int illegal_mem = 0;
int boot_edisk = 0;
char df0[256] = "fd0.image", df1[256] = "fd1.image",
	 df2[256] = "fd2.image", df3[256] = "fd3.image";
char romfile[256] = "ClassicROM";
char pramfile[256] = "PRAM";


/*
 *  Parse command line options
 */

void usage(void)
{
	printf("Basilisk\n");
	printf("Summary of command-line options:\n");
	printf("  -h                 : Print help\n");
	printf("  -f n               : Draw every n-th frame\n");
	printf("  -D                 : Start up the built-in debugger\n");
	printf("  -E                 : Boot from ROM .EDisk\n");
	printf("  -[0123] filename   : Use file instead of fd[0123].image as filedisks\n");
	printf("  -r filename        : Use file as ROM image instead of 'ClassicROM'\n");
    printf("  -p filename        : Use filename to save PRAM contents\n");
}

void parse_cmdline(int argc, char **argv)
{
	int c;
	extern char *optarg;

	while(((c = getopt(argc, argv, "DEf:h0:1:2:3:r:p:")) != EOF)) switch(c) {
	 case 'h': usage(); exit(0);

	 case '0': strncpy(df0, optarg, 255); df0[255] = 0; break;
	 case '1': strncpy(df1, optarg, 255); df1[255] = 0; break;
	 case '2': strncpy(df2, optarg, 255); df2[255] = 0; break;
	 case '3': strncpy(df3, optarg, 255); df3[255] = 0; break;
	 case 'r': strncpy(romfile, optarg, 255); romfile[255] = 0; break;
	 case 'p': strncpy(pramfile, optarg, 255); pramfile[255] = 0; break;                  

	 case 'f': framerate = atoi(optarg); break;
	 case 'D': use_debugger = 1; break;
	 case 'E': boot_edisk = 1; break;
	}
}
