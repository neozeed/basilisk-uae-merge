 /*
  * UAE - The Un*x Amiga Emulator
  * 
  * Prototypes for main.c
  * 
  * Copyright 1996 Bernd Schmidt
  */

extern void do_start_program(void);
extern void do_leave_program(void);
extern void start_program(void);
extern void leave_program(void);
extern void real_main(int, char **);
extern void target_specific_usage(void);
extern void usage(void);
extern void parse_cmdline(int argc, char **argv);

/* Contains the filename of .uaerc */
extern char optionsfile[];

