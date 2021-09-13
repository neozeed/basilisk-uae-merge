 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Serial Line Emulation
  *
  * Copyright 1996 Stefan Reinauer <stepan@matrix.kommune.schokola.de>
  */

extern void serial_init(void);
extern void serial_exit(void);

extern UWORD SERDATR(void);
extern void  SERPER(UWORD w);
extern void  SERDAT(UWORD w);

extern UWORD serdat;
