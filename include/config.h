 /* 
  * UAE - The Un*x Amiga Emulator
  * 
  * User configuration options
  *
  * Copyright 1995, 1996 Bernd Schmidt
  */

/*
 * Please note: Many things are configurable with command line parameters,
 * and you can put anything you can pass on the command line into a 
 * configuration file ~/.uaerc. Please read the documentation for more
 * information.
 * 
 * NOTE NOTE NOTE
 * Whenever you change something in this file, you have to "make clean"
 * afterwards.
 * Don't remove the '#' signs. If you want to enable something, move it out
 * of the C comment block, if you want to disable something, move it inside
 * the block.
 */

/*
 * CPU level: 0 = 68000, 1 = 68010, 2 = 68020, 3 = 68020/68881
 * If configured for 68020, the emulator will be a little slower.
 */
#define CPU_LEVEL 0

/*
 * Define this when you are compiling UAE for the first time. If it works, you
 * can try to undefine it to get (much) better performance. It does not seem
 * to work on all machines, though.
 */
#define DONT_WANT_SHM

/*
 * If you are running UAE over the network on a remote X server, this can
 * boost performance quite a bit. It can even boost performance on a 
 * non-networked system.
#define LOW_BANDWIDTH
 */

/*
 * When these two are enabled, a subset of the ECS features is emulated.
 * Actually, it's only the chip identification and big blits. This may be
 * enough to persuade some ECS programs to run.
 * DON'T enable SuperHires or Productivity modes. They are not emulated,
 * and will look very messy. NTSC doesn't work either.
 */
#define ECS_AGNUS
#define ECS_DENISE

/*
 * If you don't have any sound hardware, or if you don't want to use it, then
 * this option may make the emulator a little faster. I don't really know
 * whether it's worthwhile, so if you have any results with this one, tell
 * me about it.
 */
#define DONT_WANT_SOUND

/*
 * With this parameter, you can tune the CPU speed vs. graphics/sound hardware
 * speed. If you set this to 1, you'll get maximum CPU speed, but demos and
 * games will run very slowly. With large values, the CPU can't execute many
 * instructions per frame, but for many demos, it doesn't have to. A good
 * compromise is setting this to 4. Higher values may produce better sound
 * output, but can make some programs crash because they don't get enough CPU
 * time.
 */
#define M68K_SPEED 1

/*
 * When USE_COMPILER is defined, a m68k->i386 instruction compiler will be
 * used. This is experimental. It has only been tested on a Linux/i386 ELF
 * machine, although it might work on other i386 Unices.
 * This is supposed to speed up application programs. It will not work very
 * well for hardware bangers like games and demos, in fact it will be much
 * slower. It can also be slower for some applications and/or benchmarks.
 * It needs a lot of tuning. Please let me know your results with this.
 * The second define, RELY_ON_LOADSEG_DETECTION, decides how the compiler 
 * tries to detect self-modifying code. If it is not set, the first bytes
 * of every compiled routine are used as checksum before executing the
 * routine. If it is set, the UAE filesystem will perform some checks to 
 * detect whether an executable is being loaded. This is less reliable
 * (it won't work if you don't use the harddisk emulation, so don't try to
 * use floppies or even the RAM disk), but much faster.
#define USE_COMPILER
#define RELY_ON_LOADSEG_DETECTION
 */

/*
 * Set USER_PROGRAMS_BEHAVE to 1 or 2 to indicate that you are only running
 * non-hardware banging programs which leave all the dirty work to the
 * Kickstart. This affects the compiler, and on Linux systems it also
 * affects the normal CPU emulation. Any program that is _not_ in the ROM
 * (i.e. everything but the Kickstart) will use faster memory access 
 * functions.
 * There is of course the problem that the Amiga doesn't really distinguish
 * between user programs and the kernel. Not all of the OS is in the ROM,
 * e.g. the parallel.device is on the disk and gets loaded into RAM at least
 * with Kickstart 1.3 (don't know about newer Kickstarts). So you probably
 * can't print, and some other stuff may also fail to work.
 * A useless option, really, given the way lots of Amiga software is written.
#define USER_PROGRAMS_BEHAVE 0
 */

/***************************************************************************
 * Operating system specific options
 */

/*
 * Define this if you have the AF System and want sound in UAE.
 * You also have to set the right paths in the Makefile.
#define AF_SOUND
 */

/*
 * Define this if you have a Solaris box with sound hardware and the
 * right header files.
#define SOLARIS_SOUND
 */

/*
 * Define if you have installed the Linux sound driver and if you have read
 * the section about sound in the README.
 * Enable sound at run-time by passing the -S parameter with a value >= 2.
#define LINUX_SOUND
 */

/*
 * This option enables a different planar->chunky conversion routine. It may
 * work slightly faster on some machines, it will work a lot slower on others,
 * and it will crash most RISC machines.
 * It seems to be a win on the Pentium. No idea about other x86's.
 */
#define UNALIGNED_PROFITABLE

/*
 * SMP support - if this is defined, the graphics update code runs in a
 * separate thread. This works only for Linux right now. You can define this
 * even if you have a single processor machine, but that will give you a
 * performance loss. I can more or less guarantee that it will also give you 
 * a performance loss even on an SMP machine, because the code is written very
 * sloppily and is far too happy about making syscalls; but I want people with
 * the right hardware to test this out anyway.
#define SUPPORT_PENGUINS
 */

/***************************************************************************
 * Support for broken software. These options are set to default values
 * that are reasonable for most uses. You should not need to change these.
 */

/*
 * Some STUPID programs access a longword at an odd address and expect to
 * end up at the routine given in the vector for exception 3.
 * (For example, Katakis does this). And yes, I know it's legal, but it's dumb
 * anyway.
 * If you leave this commented in, memory accesses will be faster,
 * but some programs may fail for an obscure reason.
 */
#define NO_EXCEPTION_3

/*
 * If you want to see the "Hardwired" demo, you need to define this.
 * Otherwise, it will say "This demo don't like Axel" - apparently, Axel
 * has a 68040.
 * NEWS FLASH! My sources tell me that "Axel" stands for accelerator. Not
 * that it really matters...
#define WANT_SLOW_MULTIPLY
 */
#define WANT_SLOW_MULTIPLY

/*
 * This variable was introduced because a program could do a Bcc from
 * whithin chip memory to a location whitin expansion memory. With a
 * pointer variable the program counter would point to the wrong location.
 * With this variable unset the program counter is always correct, but
 * programs will run slower (about 4%).
 * Usually, you'll want to have this defined.
 */
#define USE_POINTER
