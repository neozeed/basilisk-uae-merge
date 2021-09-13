 /* 
  * UAE - The Un*x Amiga Emulator
  * 
  * Autoconfig device support
  *
  * (c) 1996 Ed Hanway
  */

typedef ULONG (*TrapFunction)(void);

extern int lasttrap;
extern void do_emultrap(int nr);

extern ULONG addr(int);
extern void dw(UWORD);
extern void dl(ULONG);
extern ULONG ds(char *);
extern void calltrap(ULONG);
extern void calltrap2(ULONG);
extern void org(ULONG);
extern ULONG here(void);
extern int deftrap2(TrapFunction func, int mode, const char *str);
extern int deftrap(TrapFunction);
extern void align(int);
extern ULONG CallLib(CPTR base, WORD offset);
extern ULONG Call68k(CPTR address, int saveregs);
extern ULONG Call68k_retaddr(CPTR address, int saveregs, CPTR);

#define RTS 0x4e75
#define RTE 0x4e73

extern CPTR EXPANSION_explibname, EXPANSION_doslibname, EXPANSION_uaeversion;
extern CPTR EXPANSION_explibbase, EXPANSION_uaedevname, EXPANSION_haveV36;
extern CPTR EXPANSION_bootcode, EXPANSION_nullfunc;

extern CPTR ROM_filesys_resname, ROM_filesys_resid;
extern CPTR ROM_filesys_diagentry;
extern CPTR ROM_hardfile_resname, ROM_hardfile_resid;
extern CPTR ROM_hardfile_init;

extern void add_filesys_unit(char *volname, char *rootdir, int readonly);
extern int kill_filesys_unit(char *volname);
extern int sprintf_filesys_unit(char *buffer, int num);
extern void write_filesys_config(FILE *f);
extern int get_new_device(char **devname, CPTR *devname_amiga);

extern void filesys_install(void);
extern void filesys_init(void);
extern ULONG hardfile_init_late(void);
extern void hardfile_install(void);
extern void emulib_install(void);
extern void trackdisk_install(void);
extern void expansion_init(void);

#define TRAPFLAG_NOREGSAVE 1
#define TRAPFLAG_NORETVAL 2

extern CPTR libemu_InstallFunction(TrapFunction, CPTR, int, const char *);
extern CPTR libemu_InstallFunctionFlags(TrapFunction, CPTR, int, int, const char *);
