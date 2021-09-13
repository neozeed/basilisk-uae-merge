/*
 *  sony.h - Replacement .Sony driver (floppy drives)
 *
 *  Basilisk (C) 1996-1997 Christian Bauer
 */

extern LONG SonyOpen(CPTR pb, CPTR dce);
extern LONG SonyPrime(CPTR pb, CPTR dce);
extern LONG SonyControl(CPTR pb, CPTR dce);
extern LONG SonyStatus(CPTR pb, CPTR dce);

extern void sony_init(void);
extern void sony_exit(void);

extern void disk_inserted(int num);
