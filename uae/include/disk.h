 /* 
  * UAE - The Un*x Amiga Emulator
  * 
  * disk support
  *
  * (c) 1995 Bernd Schmidt
  */

extern void DISK_init(void);
extern void DISK_select(UBYTE data);
extern UBYTE DISK_status(void);
extern int DISK_GetData(UWORD *mfm,UWORD *byt);
extern void DISK_InitWrite(void);
extern void DISK_WriteData(int);
extern void disk_eject(int num);
extern int disk_empty(int num);
extern void disk_insert(int num, const char *name);
extern int DISK_ReadMFM(CPTR);
extern int DISK_PrepareReadMFM(int, UWORD, int);

extern UWORD* mfmwrite;
