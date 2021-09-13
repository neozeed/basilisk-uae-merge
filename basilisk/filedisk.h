/*
 *  filedisk.h - Filedisk driver
 *
 *  Basilisk (C) 1996-1997 Christian Bauer
 */

extern LONG FileDiskOpen(CPTR pb, CPTR dce);
extern LONG FileDiskPrime(CPTR pb, CPTR dce);
extern LONG FileDiskControl(CPTR pb, CPTR dce);
extern LONG FileDiskStatus(CPTR pb, CPTR dce);

extern void filedisk_init(void);
extern void filedisk_exit(void);
