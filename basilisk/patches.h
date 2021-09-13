/*
 *  patches.h - Mac ROM patches
 *
 *  Basilisk (C) 1996-1997 Christian Bauer
 */

extern void patch_rom(UBYTE *rom);
extern void load_pram(void);
extern void save_pram(void);
extern void ersatz_perform(UWORD op);
extern void customreset(void);
extern ULONG CallTrap(UWORD trap);
extern ULONG CallRoutine(CPTR addr);

extern int vbl_interrupt;							// Flag: VBL interrupt occurred
extern int mousex, mousey;							// Current mouse position
extern int mousebutton;								// Current mouse button state
extern UBYTE key_states[16], old_key_states[16];	// Current/previous key states
extern int mount_floppy;
