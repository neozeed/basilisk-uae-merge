 /*
  * UAE - The Un*x Amiga Emulator
  *
  * OS emulation prototypes
  *
  * Copyright 1996 Bernd Schmidt
  */

static __inline__ char *raddr(CPTR p)
{
    return p == 0 ? NULL : (char *)get_real_address(p);
}

extern void timerdev_init(void);
extern void gfxlib_install(void);
extern void execlib_install(void);
extern void execlib_sysinit(void);

/* exec.library */

extern CPTR EXEC_sysbase;

extern void EXEC_NewList(CPTR list);
extern void EXEC_Insert(CPTR list, CPTR node, CPTR pred);
extern void EXEC_AddTail(CPTR list, CPTR node);
extern void EXEC_Enqueue(CPTR list, CPTR node);
extern void EXEC_Remove(CPTR node);
extern ULONG EXEC_RemHead(CPTR list);
extern ULONG EXEC_RemTail(CPTR list);
extern CPTR EXEC_FindName(CPTR start, char *name);

extern CPTR EXEC_Allocate(CPTR memheader, unsigned long size);
extern CPTR EXEC_AllocMem(unsigned long size, ULONG requirements);
extern void EXEC_FreeMem(CPTR, unsigned long);
extern void EXEC_Deallocate(CPTR, CPTR, unsigned long);
extern void EXEC_FreeMem(CPTR, unsigned long);
extern unsigned long EXEC_AvailMem(ULONG requirements);
extern void EXEC_AddMemList(unsigned long, ULONG, int, CPTR, CPTR);
extern void EXEC_InitStruct(CPTR inittable, CPTR memory, unsigned long size);

extern CPTR EXEC_SetIntVector(int number, CPTR interrupt);
extern void EXEC_RemIntServer(ULONG nr, CPTR interrupt);
extern void EXEC_AddIntServer(ULONG nr, CPTR interrupt);

extern int EXEC_AllocSignal(int signum);
extern void EXEC_FreeSignal(int signum);
extern void EXEC_InitSemaphore(CPTR sigsem);
extern CPTR EXEC_GetMsg(CPTR port);
extern void EXEC_SumLibrary(CPTR lib);
extern CPTR EXEC_SetFunction(CPTR lib, int funcOffset, CPTR function);
extern void EXEC_MakeFunctions(CPTR target, CPTR funcarray, CPTR funcdispb);
extern CPTR EXEC_MakeLibrary(CPTR, CPTR, CPTR, unsigned long, ULONG);
extern void EXEC_AddLibrary(CPTR lib);
extern void EXEC_AddDevice(CPTR lib);
extern void EXEC_AddResource(CPTR lib);
extern void EXEC_AddPort(CPTR port);
extern void EXEC_RemPort(CPTR port);

extern CPTR EXEC_FindTask(char *name);
extern CPTR EXEC_FindResident(char *name);
extern CPTR EXEC_RawDoFormat(UBYTE *fstr, CPTR data, CPTR pcp, ULONG pcd);

/* These require a multi-tasking EXEC emulation */
extern ULONG EXEC_Wait(ULONG exec_sigmask);
extern void EXEC_ObtainSemaphoreList(CPTR l);
extern void EXEC_ReleaseSemaphoreList(CPTR l);

extern ULONG EXEC_Permit(void);
extern ULONG EXEC_Forbid(void);
extern ULONG EXEC_Disable(void);
extern ULONG EXEC_Enable(void);

extern LONG EXEC_WaitIO(CPTR ioreq);
extern LONG EXEC_DoIO(CPTR ioreq);
extern void EXEC_SendIO(CPTR ioreq);
extern CPTR EXEC_CheckIO(CPTR ioreq);
extern void EXEC_AbortIO(CPTR ioRequest);

extern void EXEC_InitSemaphore(CPTR exec_sigsem);
extern ULONG EXEC_AttemptSemaphore(CPTR exec_sigsem);
extern void EXEC_ObtainSemaphore(CPTR exec_sigsem);
extern void EXEC_ReleaseSemaphore(CPTR exec_sigsem);

extern CPTR EXEC_OpenLibrary(char *name, int version);
extern CPTR EXEC_OpenResource(char *name);
extern CPTR EXEC_OpenDevice(char *name, ULONG unit, CPTR ioRequest, ULONG flags);
extern ULONG EXEC_CloseLibrary(CPTR lib);
extern ULONG EXEC_CloseDevice(CPTR ioRequest);

extern CPTR EXEC_AddTask(CPTR task, CPTR initPC, CPTR finalPC);
extern void EXEC_RemTask(CPTR task);
extern int EXEC_SetTaskPri(CPTR task, int pri);

extern ULONG EXEC_SetSignal(ULONG newsig, ULONG exec_sigmask);
extern void EXEC_Signal(CPTR task, ULONG exec_sigmask);

extern void EXEC_PutMsg(CPTR port, CPTR msg);
extern void EXEC_ReplyMsg(CPTR msg);
extern CPTR EXEC_WaitPort(CPTR port);

extern void EXEC_QuantumElapsed(void);

extern CPTR EXEC_InitResident(CPTR resident, ULONG segList);

/* timer.device */

extern void TIMER_block(void);
extern void TIMER_unblock(void);
extern void TIMER_Interrupt(void);

/* graphics.library */

extern int GFX_WritePixel(CPTR rp, int x, int y);

