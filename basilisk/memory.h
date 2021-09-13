/*
 *  memory.h - Memory management
 *
 *  Basilisk (C) 1996-1997 Christian Bauer
 */
#define REGPARAM
typedef ULONG (*mem_get_func)(CPTR) REGPARAM;
typedef void (*mem_put_func)(CPTR, ULONG) REGPARAM;
typedef UBYTE *(*xlate_func)(CPTR) REGPARAM;
typedef int (*check_func)(CPTR, ULONG) REGPARAM;

extern char *address_space, *good_address_map;

#undef DIRECT_MEMFUNCS_SUCCESSFUL
#include "machdep/memory.h"

#ifndef CAN_MAP_MEMORY
#undef USE_COMPILER
#endif

#if defined(USE_COMPILER) && !defined(USE_MAPPED_MEMORY)
#define USE_MAPPED_MEMORY
#endif

//1MB?
#define mainmem_size 0x400000
#define rommem_size 0x080000
#define emulmem_size 0x010000

#define mainmem_start 0x000000
#define rommem_start 0x400000
#define emulmem_start 0x600000

#define mainmem_mask (mainmem_size-1)
#define rommem_mask (rommem_size-1)
#define emulmem_mask (emulmem_size-1)

typedef struct {
    /* These ones should be self-explanatory... */
    mem_get_func alget, awget, lget, wget, bget;
    mem_put_func lput, wput, bput;
    /* Use xlateaddr to translate an Amiga address to a UBYTE * that can
     * be used to address memory without calling the wget/wput functions. 
     * This doesn't work for all memory banks, so this function may call
     * abort(). */
    xlate_func xlateaddr;
    /* To prevent calls to abort(), use check before calling xlateaddr.
     * It checks not only that the memory bank can do xlateaddr, but also
     * that the pointer points to an area of at least the specified size. 
     * This is used for example to translate bitplane pointers in custom.c */
    check_func check;
} addrbank;

extern addrbank mainmem_bank;
extern addrbank rommem_bank;
extern addrbank emulmem_bank;
extern addrbank via_bank;

extern UBYTE *mainmemory;


/* Default memory access functions */

extern int default_check(CPTR addr, ULONG size) REGPARAM;
extern UBYTE *default_xlate(CPTR addr) REGPARAM;
extern ULONG default_awget(CPTR addr) REGPARAM;
extern ULONG default_alget(CPTR addr) REGPARAM;

extern addrbank membanks[65536];
static __inline__ unsigned int bankindex(CPTR a)
{
    return a>>16;
}

static __inline__ int check_addr(CPTR a)
{
#if defined(NO_EXCEPTION_3) || CPU_LEVEL > 1
    return 1;
#else
    return (a & 1) == 0;
#endif
}
extern int buserr;
    
extern void memory_init(void);    
extern void map_banks(addrbank *bank, int first, int count);

#ifndef NO_INLINE_MEMORY_ACCESS

#define alongget(addr) (call_mem_get_func(membanks[bankindex(addr)].alget, addr))
#define awordget(addr) (call_mem_get_func(membanks[bankindex(addr)].awget, addr))
#define longget(addr) (call_mem_get_func(membanks[bankindex(addr)].lget, addr))
#define wordget(addr) (call_mem_get_func(membanks[bankindex(addr)].wget, addr))
#define byteget(addr) (call_mem_get_func(membanks[bankindex(addr)].bget, addr))
#define longput(addr,l) (call_mem_put_func(membanks[bankindex(addr)].lput, addr, l))
#define wordput(addr,w) (call_mem_put_func(membanks[bankindex(addr)].wput, addr, w))
#define byteput(addr,b) (call_mem_put_func(membanks[bankindex(addr)].bput, addr, b))

#else

extern ULONG alongget(CPTR addr);
extern ULONG awordget(CPTR addr);
extern ULONG longget(CPTR addr);
extern ULONG wordget(CPTR addr);
extern ULONG byteget(CPTR addr);
extern void longput(CPTR addr, ULONG l);
extern void wordput(CPTR addr, ULONG w);
extern void byteput(CPTR addr, ULONG b);

#endif

#ifndef MD_HAVE_MEM_1_FUNCS

#define alongget_1 alongget
#define awordget_1 awordget
#define longget_1 longget
#define wordget_1 wordget
#define byteget_1 byteget
#define longput_1 longput
#define wordput_1 wordput
#define byteput_1 byteput

#endif

static __inline__ ULONG get_along(CPTR addr) 
{
    if (check_addr(addr))
	return longget_1(addr);
    buserr = 1;
    return 0;
}
static __inline__ ULONG get_aword(CPTR addr)
{
    if (check_addr(addr))
	return wordget_1(addr);
    buserr = 1;
    return 0;
}
static __inline__ ULONG get_long(CPTR addr) 
{
    if (check_addr(addr))
	return longget_1(addr);
    buserr = 1;
    return 0;
}
static __inline__ ULONG get_word(CPTR addr) 
{
    if (check_addr(addr))
	return wordget_1(addr);
    buserr = 1;
    return 0;
}
static __inline__ ULONG get_byte(CPTR addr) 
{
    return byteget_1(addr); 
}
static __inline__ void put_long(CPTR addr, ULONG l) 
{
    if (!check_addr(addr))
	buserr = 1;
    longput_1(addr, l);
}
static __inline__ void put_word(CPTR addr, ULONG w)
{
    if (!check_addr(addr))
	buserr = 1;
    wordput_1(addr, w);
}
static __inline__ void put_byte(CPTR addr, ULONG b) 
{
    byteput_1(addr, b);
}

static __inline__ UBYTE *get_real_address(CPTR addr)
{
    return membanks[bankindex(addr)].xlateaddr(addr);
}

static __inline__ int valid_address(CPTR addr, ULONG size)
{
    return membanks[bankindex(addr)].check(addr, size);
}
