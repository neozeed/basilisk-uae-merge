 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Unix file system handler for AmigaDOS
  *
  * Copyright 1996 Ed Hanway, Bernd Schmidt
  *
  * Version 0.2: 960730
  *
  * Based on example code (c) 1988 The Software Distillery
  * and published in Transactor for the Amiga, Volume 2, Issues 2-5.
  * (May - August 1989)
  *
  * Known limitations:
  * Does not support ACTION_INHIBIT (big deal).
  * Does not support any 2.0+ packet types (except ACTION_SAME_LOCK)
  * Does not actually enforce exclusive locks.
  * Does not support removable volumes.
  * May not return the correct error code in some cases.
  * Does not check for sane values passed by AmigaDOS.  May crash the emulation
  * if passed garbage values.
  *
  * TODO someday:
  * Implement real locking using flock.  Needs test cases.
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"
#include "memory.h"
#include "custom.h"
#include "events.h"
#include "readcpu.h"
#include "newcpu.h"
#include "xwin.h"
#include "autoconf.h"
#include "compiler.h"

#ifdef __BEOS__
#undef access
// This is just a temporary hack.
// (Will be obsolete in future versions of the BeOS) [ So I hope -- Bernd ]

int access(const char *name, int mode)
{
    struct stat statbuf;
//	printf("%s\n",name);

    if(-1 == stat(name, &statbuf)) return(-1);
    else{
//		printf("OK\n");
//		printf("%i\n",S_ISDIR(statbuf.st_mode)?1:0);
		if(mode&R_OK) if(statbuf.st_mode&S_IRUSR) return(0);
		if(mode&W_OK) if(statbuf.st_mode&S_IWUSR) return(0);
		return(-1);
	}
}
#endif

#define MAKE_CASE_INSENSITIVE

typedef struct {
    char *devname; /* device name, e.g. UAE0: */
    CPTR devname_amiga;
    CPTR startup;
    char *volname; /* volume name, e.g. CDROM, WORK, etc. */
    char *rootdir; /* root unix directory */
    int readonly; /* disallow write access? */
    int devno;
} UnitInfo;

#define MAX_UNITS 20
static int num_units = 0, num_filesys_units = 0;
static UnitInfo ui[MAX_UNITS];

static ULONG fsdevname, filesysseglist, hardfileseglist, filesys_configdev;
static CPTR filesys_parampacket;

void add_filesys_unit(char *volname, char *rootdir, int readonly)
{
    if (num_units >= MAX_UNITS) {
	fprintf(stderr, "Maximum number of file systems mounted.\n");
	return;
    }

    if (volname != NULL) {
	num_filesys_units++;
	ui[num_units].volname = my_strdup(volname);
    } else
	ui[num_units].volname = NULL;
    ui[num_units].rootdir = my_strdup(rootdir);
    ui[num_units].readonly = readonly;

    num_units++;
}

int kill_filesys_unit(char *volname)
{
    size_t l = strlen(volname);
    int i;

    if (l == 0)
	return -1;

    if (volname[l-1] == ':')
	l--;
    
    for (i = 0; i < num_units; i++) {
	if (ui[i].volname != NULL
	    && strlen(ui[i].volname) == l
	    && strncmp(ui[i].volname, volname, l) == 0)
	    break;
    }
    if (i == num_units)
	return -1;

    num_units--;
    for (; i < num_units; i++) {
	ui[i] = ui[i+1];
    }
    return 0;
}

int sprintf_filesys_unit(char *buffer, int num)
{
    if (num >= num_units)
	return -1;
    if (ui[num].volname != NULL)
	sprintf(buffer, "%s: %s %s", ui[num].volname, ui[num].rootdir,
		ui[num].readonly ? "ro" : "");
    else
	sprintf(buffer, "Hardfile \"UAE0:\", size %d bytes",
		hardfile_size);
    return 0;
}

void write_filesys_config(FILE *f)
{
    int i;
    for (i = 0; i < num_units; i++) {
	if (ui[i].volname != NULL) {
	    fprintf(f, "-%c %s:%s\n", ui[i].readonly ? 'M' : 'm',
		    ui[i].volname, ui[i].rootdir);
	}
    }
}
/*#define TRACING_ENABLED*/
#ifdef TRACING_ENABLED
#define TRACE(x)	printf x;
#define DUMPLOCK(x)	dumplock(x)
#else
#define TRACE(x)
#define DUMPLOCK(x)
#endif

/* minimal AmigaDOS definitions */

/* field offsets in DosPacket */
#define dp_Type (8)
#define dp_Res1	(12)
#define dp_Res2 (16)
#define dp_Arg1 (20)
#define dp_Arg2 (24)
#define dp_Arg3 (28)
#define dp_Arg4 (32)

/* result codes */
#define DOS_TRUE (-1L)
#define DOS_FALSE (0L)

/* packet types */
#define ACTION_CURRENT_VOLUME	7
#define ACTION_LOCATE_OBJECT	8
#define ACTION_RENAME_DISK	9
#define ACTION_FREE_LOCK	15
#define ACTION_DELETE_OBJECT	16
#define ACTION_RENAME_OBJECT	17
#define ACTION_COPY_DIR		19
#define ACTION_SET_PROTECT	21
#define ACTION_CREATE_DIR	22
#define ACTION_EXAMINE_OBJECT	23
#define ACTION_EXAMINE_NEXT	24
#define ACTION_DISK_INFO	25
#define ACTION_INFO		26
#define ACTION_FLUSH		27
#define ACTION_SET_COMMENT	28
#define ACTION_PARENT		29
#define ACTION_SET_DATE		34
#define ACTION_SAME_LOCK	40
#define ACTION_FIND_WRITE	1004
#define ACTION_FIND_INPUT	1005
#define ACTION_FIND_OUTPUT	1006
#define ACTION_END		1007
#define ACTION_SEEK		1008
#define ACTION_IS_FILESYSTEM	1027
#define ACTION_READ		'R'
#define ACTION_WRITE		'W'

#define DISK_TYPE		(0x444f5301) /* DOS\1 */

/* errors */
#define ERROR_NO_FREE_STORE     103
#define ERROR_OBJECT_IN_USE	202
#define ERROR_OBJECT_EXISTS     203
#define ERROR_DIR_NOT_FOUND     204
#define ERROR_OBJECT_NOT_FOUND  205
#define ERROR_ACTION_NOT_KNOWN  209
#define ERROR_OBJECT_WRONG_TYPE 212
#define ERROR_DISK_WRITE_PROTECTED 214
#define ERROR_DIRECTORY_NOT_EMPTY 216
#define ERROR_DEVICE_NOT_MOUNTED 218
#define ERROR_SEEK_ERROR	219
#define ERROR_DISK_FULL		221
#define ERROR_WRITE_PROTECTED 223
#define ERROR_NO_MORE_ENTRIES  232
#define ERROR_NOT_IMPLEMENTED	236

static long dos_errno(void)
{
    int e = errno;

    switch(e) {
     case ENOMEM:	return ERROR_NO_FREE_STORE;
     case EEXIST:	return ERROR_OBJECT_EXISTS;
     case EACCES:	return ERROR_WRITE_PROTECTED;
     case ENOENT:	return ERROR_OBJECT_NOT_FOUND;
     case ENOTDIR:	return ERROR_OBJECT_WRONG_TYPE;
     case ENOSPC:	return ERROR_DISK_FULL;
     case EBUSY:       	return ERROR_OBJECT_IN_USE;
     case EISDIR:	return ERROR_OBJECT_WRONG_TYPE;
#if defined(ETXTBSY)
     case ETXTBSY:	return ERROR_OBJECT_IN_USE;
#endif
#if defined(EROFS)
     case EROFS:       	return ERROR_DISK_WRITE_PROTECTED;
#endif
#if defined(ENOTEMPTY)
#if ENOTEMPTY != EEXIST
     case ENOTEMPTY:	return ERROR_DIRECTORY_NOT_EMPTY;
#endif
#endif

     default:
	TRACE(("Unimplemented error %s\n", strerror(e)));
	return ERROR_NOT_IMPLEMENTED;
    }
}

/* handler state info */

typedef struct _unit {
    struct _unit *next;

    /* Amiga stuff */
    CPTR	dosbase;
    CPTR	volume;
    CPTR	port;	/* Our port */

    /* Native stuff */
    LONG	unit;	/* unit number */
    UnitInfo	ui;	/* unit startup info */
} Unit;

typedef struct {
    CPTR addr; /* addr of real packet */
    LONG type;
    LONG res1;
    LONG res2;
    LONG arg1;
    LONG arg2;
    LONG arg3;
    LONG arg4;
} DosPacket;

static char *
bstr(CPTR addr)
{
    static char buf[256];
    int n = get_byte(addr++);
    int i;
    for(i = 0; i < n; i++)
	buf[i] = get_byte(addr++);
    buf[i] = 0;
    return buf;
}

static Unit *units = NULL;
static int unit_num = 0;

static Unit*
find_unit(CPTR port)
{
    Unit* u;
    for(u = units; u; u = u->next)
	if(u->port == port)
	    break;

    return u;
}

static CPTR DosAllocMem(ULONG len)
{
    ULONG i;
    CPTR addr;

    m68k_dreg(regs, 0) = len + 4;
    m68k_dreg(regs, 1) = 1; /* MEMF_PUBLIC */
    addr = CallLib(m68k_areg(regs, 6), -198); /* AllocMem */

    if(addr) {
	put_long(addr, len);
	addr += 4;

	/* faster to clear memory here rather than use MEMF_CLEAR */
	for(i = 0; i < len; i += 4)
	    put_long(addr + i, 0);
    }

    return addr;
}

static void DosFreeMem(CPTR addr)
{
    addr -= 4;
    m68k_dreg(regs, 0) = get_long(addr) + 4;
    m68k_areg(regs, 1) = addr;
    CallLib(m68k_areg(regs, 6), -210); /* FreeMem */
}

static void
startup(DosPacket* packet)
{
    int i, namelen;
    char* devname = bstr(packet->arg1 << 2);
    char* s;
    Unit* unit;

    /* find UnitInfo with correct device name */
    s = strchr(devname, ':');
    if(s) *s = '\0';

    for(i = 0; i < num_units; i++) {
	/* Hardfile volume name? */
	if (ui[i].volname == NULL)
	    continue;
	    
	if (ui[i].startup == packet->arg2)
	    break;	    
    }
    
    if(i == num_units || 0 != access(ui[i].rootdir, R_OK)) {
	fprintf(stderr, "Failed attempt to mount device\n", devname);
	packet->res1 = DOS_FALSE;
	packet->res2 = ERROR_DEVICE_NOT_MOUNTED;
	return;
    }

    unit = (Unit *) malloc(sizeof(Unit));
    unit->next = units;
    units = unit;

    unit->volume = 0;
    unit->port = m68k_areg(regs, 5);
    unit->unit = unit_num++;

    unit->ui.devname = ui[i].devname;
    unit->ui.volname = my_strdup(ui[i].volname); /* might free later for rename */
    unit->ui.rootdir = ui[i].rootdir;
    unit->ui.readonly = ui[i].readonly;

    TRACE(("**** STARTUP volume %s\n", unit->ui.volname));

    /* fill in our process in the device node */
    put_long((packet->arg3 << 2) + 8, unit->port);

    /* open dos.library */
    m68k_dreg(regs, 0) = 0;
    m68k_areg(regs, 1) = EXPANSION_doslibname;
    unit->dosbase = CallLib(m68k_areg(regs, 6), -552); /* OpenLibrary */

    {
	CPTR rootnode = get_long(unit->dosbase + 34);
	CPTR dos_info = get_long(rootnode + 24) << 2;
	/* make new volume */
	unit->volume = DosAllocMem(80 + 1 + 44);
	put_long(unit->volume + 4, 2); /* Type = dt_volume */
	put_long(unit->volume + 12, 0); /* Lock */
	put_long(unit->volume + 16, 3800); /* Creation Date */
	put_long(unit->volume + 20, 0);
	put_long(unit->volume + 24, 0);
	put_long(unit->volume + 28, 0); /* lock list */
	put_long(unit->volume + 40, (unit->volume + 44) >> 2); /* Name */
	namelen = strlen(unit->ui.volname);
	put_byte(unit->volume + 44, namelen);
	for(i = 0; i < namelen; i++)
	    put_byte(unit->volume + 45 + i, unit->ui.volname[i]);
	
	/* link into DOS list */
	put_long(unit->volume, get_long(dos_info + 4));
	put_long(dos_info + 4, unit->volume >> 2);
    }
    
    put_long(unit->volume + 8, unit->port);
    put_long(unit->volume + 32, DISK_TYPE);

    packet->res1 = DOS_TRUE;
}

#ifdef HAVE_STATFS
static void
do_info(Unit* unit, DosPacket* packet, CPTR info)
{
    struct statfs statbuf;
#if STATFS_NO_ARGS == 2
    if(-1 == statfs(unit->ui.rootdir, &statbuf))
#else
    if(-1 == statfs(unit->ui.rootdir, &statbuf, sizeof(struct statfs), 0))
#endif
    {
	packet->res1 = DOS_FALSE;
	packet->res2 = dos_errno();
    }

    put_long(info, 0); /* errors */
    put_long(info + 4, unit->unit); /* unit number */
    put_long(info + 8, unit->ui.readonly ? 80 : 82); /* state  */
    put_long(info + 12, statbuf.f_blocks); /* numblocks */
    put_long(info + 16, statbuf.f_blocks - statbuf.STATBUF_BAVAIL); /* inuse */
    put_long(info + 20, statbuf.f_bsize); /* bytesperblock */
    put_long(info + 24, DISK_TYPE); /* disk type */
    put_long(info + 28, unit->volume >> 2); /* volume node */
    put_long(info + 32, 0); /* inuse */
    packet->res1 = DOS_TRUE;
}
#else
static void
do_info(Unit* unit, DosPacket* packet, CPTR info)
{
    put_long(info, 0); /* errors */
    put_long(info + 4, unit->unit); /* unit number */
    put_long(info + 8, unit->ui.readonly ? 80 : 82); /* state  */
    put_long(info + 12, 256); /* numblocks */
    put_long(info + 16, 128); /* inuse */
    put_long(info + 20, 512); /* bytesperblock */
    put_long(info + 24, DISK_TYPE); /* disk type */
    put_long(info + 28, unit->volume >> 2); /* volume node */
    put_long(info + 32, 0); /* inuse */
    packet->res1 = DOS_TRUE;
}
#endif

static void
action_disk_info(Unit* unit, DosPacket* packet)
{
    TRACE(("ACTION_DISK_INFO\n"));
    do_info(unit, packet, packet->arg1 << 2);
}

static void
action_info(Unit* unit, DosPacket* packet)
{
    TRACE(("ACTION_INFO\n"));
    do_info(unit, packet, packet->arg2 << 2);
}

typedef struct key {
    struct key *next;
    ULONG uniq;
    char *path;
    int fd;
    off_t file_pos;
} Key;

static struct key* keys = NULL;

static void
free_key(Key*k)
{
    Key *k1;
    Key *prev = NULL;
    for(k1 = keys; k1; k1 = k1->next) {
        if(k == k1) {
            if(prev)
                prev->next = k->next;
            else
                keys = k->next;
            break;
        }
        prev = k1;
    }

    if (k->fd >= 0)
	close(k->fd);
    free(k->path);
    free(k);
}

static Key*
lookup_key(ULONG uniq)
{
    Key *k;
    for(k = keys; k; k = k->next) {
        if(uniq == k->uniq)
            return k;
    }
    fprintf(stderr, "Error: couldn't find key %ld\n", uniq);
    exit(1);
    /* NOTREACHED */
    return NULL;
}

static Key*
new_key(void)
{
    static ULONG uniq = 0;
    Key *k = (Key*) malloc(sizeof(Key));
    k->uniq = ++uniq;
    k->fd = -1;
    k->file_pos = 0;
    k->next = keys;
    keys = k;

    return k;
}

static void
dumplock(CPTR lock)
{
    if(!lock) {
	fprintf(stderr, "LOCK: 0x0\n");
	return;
    }
    fprintf(stderr,
	    "LOCK: 0x%lx { next=0x%lx, key=%s, mode=%ld, handler=0x%lx, volume=0x%lx }\n",
	    lock,
	    get_long(lock)<<2, lookup_key(get_long(lock+4))->path, get_long(lock+8),
	    get_long(lock+12), get_long(lock+16));
}

static char*
get_path(Unit* unit, const char *base, const char *rel)
{
    static char buf[1024];
    char *s = buf;
    char *p;
    char *r;

    int i;

    TRACE(("get_path(%s,%s)\n", base, rel));

    /* root-relative path? */
    for(i = 0; rel[i] && rel[i] != '/' && rel[i] != ':'; i++);
    if(':' == rel[i]) {
	/*base = unit->ui.rootdir;*/ rel += i+1;
    }

    while(*base) {
	*s++ = *base++;
    }
    *s = 0;
    p = s; /* start of relative path */
    r = buf + strlen(unit->ui.rootdir); /* end of fixed path */

    while(*rel) {
	/* start with a slash? go up a level. */
	if('/' == *rel) {
	    while(s > r && '/' != *s)
		s--;
	    *s = 0;
	    rel++;
	} else {
	    *s++ = '/';
	    while(*rel && '/' != *rel) {
		*s++ = *rel++;
	    }
	    *s = 0;
	    if('/' == *rel)
		rel++;
	}
    }
    *s = 0;

#ifdef MAKE_CASE_INSENSITIVE
    TRACE(("path=\"%s\"\n", buf));
    /* go through each section of the path and if it does not exist,
     * scan its directory for any case insensitive matches
     */
    while(*p) {
	char *p2 = strchr(p+1, '/');
	char oldp2;
	if(!p2) {
	    p2 = p+1;
	    while(*p2) p2++;
	}
	oldp2 = *p2;
	*p2 = '\0';
	if(0 != access(buf, F_OK|R_OK)) {
	    DIR* dir;
	    struct dirent* de;
	    /* does not exist -- check dir for case insensitive match */
	    *p++ = '\0'; /* was '/' */
	    dir = opendir(buf);
	    if (dir) {
		while((de = readdir(dir))) {
#if 0
		    if(0 == stricmp(de->d_name, p))	/* OLSEN */
#endif
		    if(0 == strcasecmp(de->d_name, p))
			break;
		}
		if(de) {
		    strcpy(p, de->d_name);
		}
		closedir(dir);
	    }
	    *--p = '/';
	}
	*p2 = oldp2;
	p = p2;
    }
#endif
    TRACE(("path=\"%s\"\n", buf));

    return my_strdup(buf);
}

static Key*
make_key(Unit* unit, CPTR lock, const char *name)
{
    Key *k = new_key();

    if(!lock) {
	k->path = get_path(unit, unit->ui.rootdir, name);
    } else {
	Key*oldk = lookup_key(get_long(lock + 4));
#if 0
	const char *nm = strchr (name, ':');
	if (nm == NULL) {
	    nm = name;
	} else
	    nm++;
#else
	const char *nm = name;
#endif
	TRACE(("key: 0x%08lx", oldk->uniq));
	TRACE((" \"%s\"\n", oldk->path));
	k->path = get_path(unit, oldk->path, nm);
    }

    TRACE(("key=\"%s\"\n", k->path));
    return k;
}

static Key*
dup_key(Key*k)
{
    Key *newk = new_key();
    newk->path = my_strdup(k->path);
    return newk;
}

static CPTR
make_lock(Unit* unit, Key *key, long mode)
{
    /* allocate lock */
    CPTR lock = DosAllocMem(20);

    put_long(lock + 4, key->uniq);
    put_long(lock + 8, mode);
    put_long(lock + 12, unit->port);
    put_long(lock + 16, unit->volume >> 2);

    /* prepend to lock chain */
    put_long(lock, get_long(unit->volume + 28));
    put_long(unit->volume + 28, lock >> 2);

    DUMPLOCK(lock);
    return lock;
}

static void
free_lock(Unit* unit, CPTR lock)
{
    if(!lock)
	return;

    if(lock == get_long(unit->volume + 28) << 2) {
	put_long(unit->volume + 28, get_long(lock));
    } else {
	CPTR current = get_long(unit->volume + 28);
	CPTR next = 0;
	while(current) {
	    next = get_long(current << 2);
	    if(lock == next << 2)
		break;
	    current = next;
	}
	put_long(current << 2, get_long(lock));
    }
    free_key(lookup_key(get_long(lock + 4)));
    DosFreeMem(lock);
}

static void
action_lock(Unit* unit, DosPacket* packet)
{
    CPTR lock = packet->arg1 << 2;
    CPTR name = packet->arg2 << 2;
    long mode = packet->arg3;
    int access_mode = (mode == -2) ? R_OK : R_OK|W_OK;
    Key *k;

    TRACE(("ACTION_LOCK(0x%lx, \"%s\", %d)\n",lock, bstr(name), mode));
    DUMPLOCK(lock);

    k = make_key(unit, lock, bstr(name));

    if(k && 0 == access(k->path, access_mode)) {
	packet->res1 = make_lock(unit, k, mode) >> 2;
    } else {
	if(k)
	    free_key(k);
	packet->res1 = 0;
	packet->res2 = ERROR_OBJECT_NOT_FOUND;
    }
}

static void
action_free_lock(Unit* unit, DosPacket* packet)
{
    CPTR lock = packet->arg1 << 2;
    TRACE(("ACTION_FREE_LOCK(0x%lx)\n", lock));
    DUMPLOCK(lock);

    free_lock(unit, lock);

    packet->res1 = DOS_TRUE;
}

static void
action_dup_lock(Unit* unit, DosPacket* packet)
{
    CPTR lock = packet->arg1 << 2;

    TRACE(("ACTION_DUP_LOCK(0x%lx)\n", lock));
    DUMPLOCK(lock);

    if(!lock) {
	packet->res1 = 0;
	return;
    }

    {
	CPTR oldkey = get_long(lock + 4);
	CPTR oldmode = get_long(lock + 8);
	packet->res1 = make_lock(unit, dup_key(lookup_key(oldkey)), oldmode) >> 2;
    }
}

/* convert time_t to/from AmigaDOS time */
const int secs_per_day = 24 * 60 * 60;
const int diff = (8 * 365 + 2) * (24 * 60 * 60);

static void
get_time(time_t t, long* days, long* mins, long* ticks)
{
    /* time_t is secs since 1-1-1970 */
    /* days since 1-1-1978 */
    /* mins since midnight */
    /* ticks past minute @ 50Hz */

    t -= diff;
    *days = t / secs_per_day;
    t -= *days * secs_per_day;
    *mins = t / 60;
    t -= *mins * 60;
    *ticks = t * 50;
}

static time_t
put_time(long days, long mins, long ticks)
{
    time_t t;
    t = ticks / 50;
    t += mins * 60;
    t += days * secs_per_day;
    t += diff;

    return t;
}


typedef struct {
    ULONG uniq;
    char *path;
    DIR* dir;
} ExamineKey;

/* Since ACTION_EXAMINE_NEXT is so braindamaged, we have to keep
 * some of these around
 */

#define EXKEYS 100
static ExamineKey examine_keys[EXKEYS];
static int next_exkey = 0;

static void
free_exkey(ExamineKey* ek)
{
    free(ek->path);
    ek->path = 0;
    if(ek->dir)
	closedir(ek->dir);
}

static ExamineKey*
new_exkey(char *path)
{
    ULONG uniq = next_exkey;
    ExamineKey* ek= &examine_keys[next_exkey++];
    if(next_exkey==EXKEYS)
	next_exkey = 0;
    if(ek->path) {
	free_exkey(ek);
    }
    ek->path = my_strdup(path);
    ek->dir = 0;
    ek->uniq = uniq;
    return ek;
}

static void
get_fileinfo(Unit *unit, DosPacket* packet, CPTR info, char *buf)
{
    struct stat statbuf;
    long days, mins, ticks;

    if(-1 == stat(buf, &statbuf)) {
	packet->res1 = DOS_FALSE;
	packet->res2 = dos_errno();
    } else {
	put_long(info + 4, S_ISDIR(statbuf.st_mode) ? 2 : -3);
	{
	    /* file name */
	    int i = 8;
	    int n;
	    char *x;
	    if (strcmp (buf, unit->ui.rootdir) == 0) {
		x = unit->ui.volname;
	    } else {
		x = strrchr(buf,'/');
		if(x)
		    x++;
		else
		    x = buf;
	    }
	    TRACE(("name=\"%s\"\n", x));
	    n = strlen(x);
	    if(n > 106) n = 106;
	    put_byte(info + i++, n);
	    while(n--)
		put_byte(info + i++, *x++);
	    while(i < 108)
		put_byte(info + i++, 0);
	}
	put_long(info + 116,
		 (S_IRUSR & statbuf.st_mode ? 0 : (1<<3)) |
		 (S_IWUSR & statbuf.st_mode ? 0 : (1<<2)) |
#ifndef __DOS__
		 (S_IXUSR & statbuf.st_mode ? 0 : (1<<1)) |
#endif
		 (S_IWUSR & statbuf.st_mode ? 0 : (1<<0)));
	put_long(info + 120, S_ISDIR(statbuf.st_mode) ? 2 : -3);
	put_long(info + 124, statbuf.st_size);
#ifdef HAVE_ST_BLOCKS
	put_long(info + 128, statbuf.st_blocks);
#else
	put_long(info + 128, statbuf.st_size / 512 + 1);
#endif
	get_time(statbuf.st_mtime, &days, &mins, &ticks);
	put_long(info + 132, days);
	put_long(info + 136, mins);
	put_long(info + 140, ticks);
	put_long(info + 144, 0); /* no comment */
	packet->res1 = DOS_TRUE;
    }
}

static void
do_examine(Unit *unit, DosPacket* packet, ExamineKey* ek, CPTR info)
{
    static char buf[1024];
    struct dirent* de;

    if(!ek->dir) {
	ek->dir = opendir(ek->path);
    }
    if(!ek->dir) {
	free_exkey(ek);
	packet->res1 = DOS_FALSE;
	packet->res2 = ERROR_NO_MORE_ENTRIES;
	return;
    }

    de = readdir(ek->dir);

    while(de && (0 == strcmp(".", de->d_name) 
		 || 0 == strcmp("..", de->d_name)))
    {
	de = readdir(ek->dir);
    }

    if(!de) {
	TRACE(("no more entries\n"));
	free_exkey(ek);
	packet->res1 = DOS_FALSE;
	packet->res2 = ERROR_NO_MORE_ENTRIES;
	return;
    }

    TRACE(("entry=\"%s\"\n", de->d_name));

    sprintf(buf, "%s/%s", ek->path, de->d_name);

    get_fileinfo(unit, packet, info, buf);
}

static void
action_examine_object(Unit* unit, DosPacket* packet)
{
    CPTR lock = packet->arg1 << 2;
    CPTR info = packet->arg2 << 2;
    char *path;
    ExamineKey* ek;

    TRACE(("ACTION_EXAMINE_OBJECT(0x%lx,0x%lx)\n", lock, info));
    DUMPLOCK(lock);

    if(!lock) {
	path = unit->ui.rootdir;
    } else {
	Key*k = lookup_key(get_long(lock + 4));
	path = k->path;
    }

    get_fileinfo(unit, packet, info, path);
    ek = new_exkey(path);
    put_long(info, ek->uniq);
}

static void
action_examine_next(Unit* unit, DosPacket* packet)
{
    CPTR lock = packet->arg1 << 2;
    CPTR info = packet->arg2 << 2;

    TRACE(("ACTION_EXAMINE_NEXT(0x%lx,0x%lx)\n", lock, info));
    DUMPLOCK(lock);

    do_examine(unit, packet, &examine_keys[get_long(info)], info);
}

static void
do_find(Unit* unit, DosPacket* packet, mode_t mode, int fallback)
{
    CPTR fh = packet->arg1 << 2;
    CPTR lock = packet->arg2 << 2;
    CPTR name = packet->arg3 << 2;
    Key *k;
    struct stat st;

    TRACE(("ACTION_FIND_*(0x%lx,0x%lx,\"%s\",%d)\n",fh,lock,bstr(name),mode));
    DUMPLOCK(lock);

    k = make_key(unit, lock, bstr(name));
    if(!k) {
	packet->res1 = DOS_FALSE;
	packet->res2 = ERROR_NO_FREE_STORE;
	return;
    }

    /* Fixme: may not quite be right */
    if (0 == stat (k->path, &st)) {
	if (S_ISDIR (st.st_mode)) {
	    packet->res1 = DOS_FALSE;
	    packet->res2 = ERROR_OBJECT_WRONG_TYPE;
	    return;
	}
    }

    k->fd = open(k->path, mode | O_BINARY, 0777);

    if (k->fd < 0
	&& (errno == EACCES
#if defined(EROFS)
	    || errno == EROFS
#endif
	    ) 
	&& fallback) 
    {
	mode &= ~O_RDWR;
	mode |= O_RDONLY;
	k->fd = open(k->path, mode | O_BINARY, 0777);
    }
    
    if (k->fd < 0) {
	packet->res1 = DOS_FALSE;
	packet->res2 = dos_errno();
	free_key(k);
	return;
    }
    success:
    put_long(fh+36, k->uniq);

    packet->res1 = DOS_TRUE;
}

static void
action_find_input(Unit* unit, DosPacket* packet)
{
    if(unit->ui.readonly) {
	do_find(unit, packet, O_RDONLY, 0);
    } else {
	do_find(unit, packet, O_RDWR, 1);
    }
}

static void
action_find_output(Unit* unit, DosPacket* packet)
{
    if(unit->ui.readonly) {
	packet->res1 = DOS_FALSE;
	packet->res2 = ERROR_DISK_WRITE_PROTECTED;
	return;
    }
    do_find(unit, packet, O_RDWR|O_CREAT|O_TRUNC, 0);
}

static void
action_find_write(Unit* unit, DosPacket* packet)
{
    if(unit->ui.readonly) {
	packet->res1 = DOS_FALSE;
	packet->res2 = ERROR_DISK_WRITE_PROTECTED;
	return;
    }
    do_find(unit, packet, O_RDWR, 0);
}

static void
action_end(Unit* unit, DosPacket* packet)
{
    Key *k;
    TRACE(("ACTION_END(0x%lx)\n", packet->arg1));

    k = lookup_key(packet->arg1);
    free_key(k);
    packet->res1 = DOS_TRUE;
    packet->res2 = 0;
}

static void
action_read(Unit* unit, DosPacket* packet)
{
    Key *k = lookup_key(packet->arg1);
    CPTR addr = packet->arg2;
    long size = (LONG)packet->arg3;
    int actual;

    TRACE(("ACTION_READ(%s,0x%lx,%ld)\n",k->path,addr,size));
#ifdef RELY_ON_LOADSEG_DETECTION
    /* HACK HACK HACK HACK 
     * Try to detect a LoadSeg() */
    if (k->file_pos == 0 && size >= 4) {
	unsigned char buf[4];
	off_t currpos = lseek(k->fd, 0, SEEK_CUR);
	read(k->fd, buf, 4);
	lseek(k->fd, currpos, SEEK_SET);
	if (buf[0] == 0 && buf[1] == 0 && buf[2] == 3 && buf[3] == 0xF3)
	    possible_loadseg();
    }
#endif
    if (valid_address (addr, size)) {
	UBYTE *realpt;
	realpt = get_real_address (addr);
	actual = read(k->fd, realpt, size);

	if (actual == 0) {
	    packet->res1 = 0;
	    packet->res2 = 0;
	} else if (actual < 0) {
	    packet->res1 = 0;
	    packet->res2 = dos_errno();
	} else {
	    packet->res1 = actual;
	    k->file_pos += actual;
	}
    } else {
	char *buf;
	fprintf (stderr, "unixfs warning: Bad pointer passed for read: %08x\n", addr);
	/* ugh this is inefficient but easy */
	buf = (char *)malloc(size);
	if(!buf) {
	    packet->res1 = -1;
	    packet->res2 = ERROR_NO_FREE_STORE;
	    return;
	}
	actual = read(k->fd, buf, size);

	if (actual < 0) {
	    packet->res1 = 0;
	    packet->res2 = dos_errno();
	} else {
	    int i;
	    packet->res1 = actual;
	    for(i = 0; i < actual; i++)
		put_byte(addr + i, buf[i]);
	    k->file_pos += actual;
	}
	free(buf);
    }
}

static void
action_write(Unit* unit, DosPacket* packet)
{
    Key*k = lookup_key(packet->arg1);
    CPTR addr = packet->arg2;
    long size = packet->arg3;
    char *buf;
    int i;

    TRACE(("ACTION_WRITE(%s,0x%lx,%ld)\n",k->path,addr,size));

    if(unit->ui.readonly) {
	packet->res1 = DOS_FALSE;
	packet->res2 = ERROR_DISK_WRITE_PROTECTED;
	return;
    }

    /* ugh this is inefficient but easy */
    buf = (char *)malloc(size);
    if(!buf) {
	packet->res1 = -1;
	packet->res2 = ERROR_NO_FREE_STORE;
	return;
    }

    for(i = 0; i < size; i++)
	buf[i] = get_byte(addr + i);

    packet->res1 = write(k->fd, buf, size);
    if(packet->res1 != size)
	packet->res2 = dos_errno();
    if (packet->res1 >= 0)
	k->file_pos += packet->res1;

    free(buf);
}

static void
action_seek(Unit* unit, DosPacket* packet)
{
    Key* k = lookup_key(packet->arg1);
    long pos = (LONG)packet->arg2;
    long mode = (LONG)packet->arg3;
    off_t res;
    long old;
    int whence = SEEK_CUR;
    if(mode > 0) whence = SEEK_END;
    if(mode < 0) whence = SEEK_SET;

    TRACE(("ACTION_SEEK(%s,%d,%d)\n",k->path,pos,mode));

    old = lseek(k->fd, 0, SEEK_CUR);
    res = lseek(k->fd, pos, whence);

    if(-1 == res)
	packet->res1 = res;
    else
	packet->res1 = old;
    k->file_pos = res;
}

static void
action_set_protect(Unit* unit, DosPacket* packet)
{
    CPTR lock = packet->arg2 << 2;
    CPTR name = packet->arg3 << 2;
    ULONG mask = packet->arg4;
    struct stat statbuf;
    mode_t mode;
    Key *k;

    TRACE(("ACTION_SET_PROTECT(0x%lx,\"%s\",0x%lx)\n",lock,bstr(name),mask));

    if(unit->ui.readonly) {
	packet->res1 = DOS_FALSE;
	packet->res2 = ERROR_DISK_WRITE_PROTECTED;
	return;
    }

    k = make_key(unit, lock, bstr(name));
    if(!k) {
	packet->res1 = DOS_FALSE;
	packet->res2 = ERROR_NO_FREE_STORE;
	return;
    }

    if(-1 == stat(k->path, &statbuf)) {
	free_key(k);
	packet->res1 = DOS_FALSE;
	packet->res2 = ERROR_OBJECT_NOT_FOUND;
	return;
    }

    mode = statbuf.st_mode;
#ifdef __unix /* Unix dirs behave differently than AmigaOS ones. */
    if (S_ISDIR (mode)) {
	mask &= ~15;
    }
#endif
    if (mask & (1 << 3))
	mode &= ~S_IRUSR;
    else
	mode |= S_IRUSR;

    if (mask & (1 << 2) || mask & (1 << 0))
	mode &= ~S_IWUSR;
    else
	mode |= S_IWUSR;

    if (mask & (1 << 1))
	mode &= ~S_IXUSR;
    else
	mode |= S_IXUSR;

    if (-1 == chmod(k->path, mode)) {
	packet->res1 = DOS_FALSE;
	packet->res2 = dos_errno();
    } else {
	packet->res1 = DOS_TRUE;
    }
    free_key(k);
}

static void
action_same_lock(Unit* unit, DosPacket* packet)
{
    CPTR lock1 = packet->arg1 << 2;
    CPTR lock2 = packet->arg2 << 2;

    TRACE(("ACTION_SAME_LOCK(0x%lx,0x%lx)\n",lock1,lock2));
    DUMPLOCK(lock1); DUMPLOCK(lock2);

    if(!lock1 || !lock2) {
	packet->res1 = (lock1 == lock2) ? DOS_TRUE : DOS_FALSE;
    } else {
	Key* key1 = lookup_key(get_long(lock1 + 4));
	Key* key2 = lookup_key(get_long(lock2 + 4));
	packet->res1 = (0 == strcmp(key1->path, key2->path)) ? DOS_TRUE : DOS_FALSE;
    }
}

static void
action_parent(Unit* unit, DosPacket* packet)
{
    CPTR lock = packet->arg1 << 2;
    Key*k;

    TRACE(("ACTION_PARENT(0x%lx)\n",lock));

    if(!lock) {
	packet->res1 = 0;
	packet->res2 = 0;
	return;
    }

    k = dup_key(lookup_key(get_long(lock + 4)));
    if(0 == strcmp(k->path, unit->ui.rootdir)) {
	free_key(k);
	packet->res1 = 0;
	packet->res2 = 0;
	return;
    }
    {
	char *x = strrchr(k->path,'/');
	if(!x) { /* ??? This really shouldn't happen! */
	    free_key(k);
	    packet->res1 = 0;
	    packet->res2 = 0;
	    return;
	} else {
	    *x = '\0';
	}
    }
    packet->res1 = make_lock(unit, k, -2) >> 2;
}

static void
action_create_dir(Unit* unit, DosPacket* packet)
{
    CPTR lock = packet->arg1 << 2;
    CPTR name = packet->arg2 << 2;
    Key* k;

    TRACE(("ACTION_CREATE_DIR(0x%lx,\"%s\")\n",lock,bstr(name)));

    if(unit->ui.readonly) {
	packet->res1 = DOS_FALSE;
	packet->res2 = ERROR_DISK_WRITE_PROTECTED;
	return;
    }

    k = make_key(unit, lock, bstr(name));

    if(!k) {
	packet->res1 = DOS_FALSE;
	packet->res2 = ERROR_NO_FREE_STORE;
	return;
    }
//    if(-1 == mkdir(k->path, 0777)) {
    if(-1 == mkdir(k->path)) {
	packet->res1 = DOS_FALSE;
	packet->res2 = dos_errno();
	free_key(k);
	return;
    }
    packet->res1 = make_lock(unit, k, -2) >> 2;
}

static void
action_delete_object(Unit* unit, DosPacket* packet)
{
    CPTR lock = packet->arg1 << 2;
    CPTR name = packet->arg2 << 2;
    Key* k;
    struct stat statbuf;

    TRACE(("ACTION_DELETE_OBJECT(0x%lx,\"%s\")\n",lock,bstr(name)));

    if(unit->ui.readonly) {
	packet->res1 = DOS_FALSE;
	packet->res2 = ERROR_DISK_WRITE_PROTECTED;
	return;
    }

    k = make_key(unit, lock, bstr(name));

    if(!k) {
	packet->res1 = DOS_FALSE;
	packet->res2 = ERROR_NO_FREE_STORE;
	return;
    }
    if(-1 == stat(k->path, &statbuf)) {
	packet->res1 = DOS_FALSE;
	packet->res2 = dos_errno();
	free_key(k);
	return;
    }
    if(S_ISDIR(statbuf.st_mode)) {
	if(-1 == rmdir(k->path)) {
	    packet->res1 = DOS_FALSE;
	    packet->res2 = dos_errno();
	    free_key(k);
	    return;
	}
    } else {
	if(-1 == unlink(k->path)) {
	    packet->res1 = DOS_FALSE;
	    packet->res2 = dos_errno();
	    free_key(k);
	    return;
	}
    }
    free_key(k);
    packet->res1 = DOS_TRUE;
}

static void
action_set_date(Unit* unit, DosPacket* packet)
{
#if 0
    CPTR lock = packet->arg2 << 2;
    CPTR name = packet->arg3 << 2;
    CPTR date = packet->arg4;
    Key* k;
    struct utimbuf ut;

    TRACE(("ACTION_SET_DATE(0x%lx,\"%s\")\n",lock,bstr(name)));

    if(unit->ui.readonly) {
	packet->res1 = DOS_FALSE;
	packet->res2 = ERROR_DISK_WRITE_PROTECTED;
	return;
    }

    ut.actime = ut.modtime = put_time(get_long(date),get_long(date+4),get_long(date+8));
    k = make_key(unit, lock, bstr(name));

    if(!k) {
	packet->res1 = DOS_FALSE;
	packet->res2 = ERROR_NO_FREE_STORE;
	return;
    }
    if(-1 == utime(k->path, &ut)) {
	packet->res1 = DOS_FALSE;
	packet->res2 = dos_errno();
	free_key(k);
	return;
    }
    free_key(k);
    packet->res1 = DOS_TRUE;
#else
//JASON THIS IS SO WRONG!!!!!!!!!!
    packet->res1 = DOS_FALSE;
#endif
}

static void
action_rename_object(Unit* unit, DosPacket* packet)
{
    CPTR lock1 = packet->arg1 << 2;
    CPTR name1 = packet->arg2 << 2;
    Key* k1;
    CPTR lock2 = packet->arg3 << 2;
    CPTR name2 = packet->arg4 << 2;
    Key* k2;

    TRACE(("ACTION_RENAME_OBJECT(0x%lx,\"%s\",",lock1,bstr(name1)));
    TRACE(("0x%lx,\"%s\")\n",lock2,bstr(name2)));

    if(unit->ui.readonly) {
	packet->res1 = DOS_FALSE;
	packet->res2 = ERROR_DISK_WRITE_PROTECTED;
	return;
    }

    k1 = make_key(unit, lock1, bstr(name1));
    if(!k1) {
	packet->res1 = DOS_FALSE;
	packet->res2 = ERROR_NO_FREE_STORE;
	return;
    }
    k2 = make_key(unit, lock2, bstr(name2));
    if(!k2) {
	free_key(k1);
	packet->res1 = DOS_FALSE;
	packet->res2 = ERROR_NO_FREE_STORE;
	return;
    }

    if(-1 == rename(k1->path, k2->path)) {
	packet->res1 = DOS_FALSE;
	packet->res2 = dos_errno();
	free_key(k1);
	free_key(k2);
	return;
    }
    free_key(k1);
    free_key(k2);
    packet->res1 = DOS_TRUE;
}

static void
action_current_volume(Unit* unit, DosPacket* packet)
{
    packet->res1 = unit->volume >> 2;
}

static void
action_rename_disk(Unit* unit, DosPacket* packet)
{
    CPTR name = packet->arg1 << 2;
    int i;
    int namelen;

    TRACE(("ACTION_RENAME_DISK(\"%s\")\n", bstr(name)));

    if(unit->ui.readonly) {
	packet->res1 = DOS_FALSE;
	packet->res2 = ERROR_DISK_WRITE_PROTECTED;
	return;
    }

    /* get volume name */
    namelen = get_byte(name++);
    free(unit->ui.volname);
    unit->ui.volname = (char *) malloc(namelen + 1);
    for(i = 0; i < namelen; i++)
	unit->ui.volname[i] = get_byte(name++);
    unit->ui.volname[i] = 0;

    put_byte(unit->volume + 44, namelen);
    for(i = 0; i < namelen; i++)
	put_byte(unit->volume + 45 + i, unit->ui.volname[i]);

    packet->res1 = DOS_TRUE;
}

static void
action_is_filesystem(Unit* unit, DosPacket* packet)
{
    packet->res1 = DOS_TRUE;
}

static void
action_flush(Unit* unit, DosPacket* packet)
{
    /* sync(); */ /* pretty drastic, eh */
    packet->res1 = DOS_TRUE;
}

static ULONG
filesys_handler(void)
{
    DosPacket packet;
    Unit *unit = find_unit(m68k_areg(regs, 5));

    /* got DosPacket in A4 */
    packet.addr = m68k_areg(regs, 4);
    packet.type = get_long(packet.addr + dp_Type);
    packet.res1 = get_long(packet.addr + dp_Res1);
    packet.res2 = get_long(packet.addr + dp_Res2);
    packet.arg1 = get_long(packet.addr + dp_Arg1);
    packet.arg2 = get_long(packet.addr + dp_Arg2);
    packet.arg3 = get_long(packet.addr + dp_Arg3);
    packet.arg4 = get_long(packet.addr + dp_Arg4);

    if(!unit) {
	startup(&packet);
	put_long(packet.addr + dp_Res1, packet.res1);
	put_long(packet.addr + dp_Res2, packet.res2);
	return 0;
    }

    if(!unit->volume) {
	printf("no volume\n");
	return 0;
    }

    switch(packet.type) {
     case ACTION_LOCATE_OBJECT:
	action_lock(unit, &packet);
	break;

     case ACTION_FREE_LOCK:
	action_free_lock(unit, &packet);
	break;

     case ACTION_COPY_DIR:
	action_dup_lock(unit, &packet);
	break;

     case ACTION_DISK_INFO:
	action_disk_info(unit, &packet);
	break;

     case ACTION_INFO:
	action_info(unit, &packet);
	break;

     case ACTION_EXAMINE_OBJECT:
	action_examine_object(unit, &packet);
	break;

     case ACTION_EXAMINE_NEXT:
	action_examine_next(unit, &packet);
	break;

     case ACTION_FIND_INPUT:
	action_find_input(unit, &packet);
	break;

     case ACTION_FIND_WRITE:
	action_find_write(unit, &packet);
	break;

     case ACTION_FIND_OUTPUT:
	action_find_output(unit, &packet);
	break;

     case ACTION_END:
	action_end(unit, &packet);
	break;

     case ACTION_READ:
	action_read(unit, &packet);
	break;

     case ACTION_WRITE:
	action_write(unit, &packet);
	break;

     case ACTION_SEEK:
	action_seek(unit, &packet);
	break;

     case ACTION_SET_PROTECT:
	action_set_protect(unit, &packet);
	break;

     case ACTION_SAME_LOCK:
	action_same_lock(unit, &packet);
	break;

     case ACTION_PARENT:
	action_parent(unit, &packet);
	break;

     case ACTION_CREATE_DIR:
	action_create_dir(unit, &packet);
	break;

     case ACTION_DELETE_OBJECT:
	action_delete_object(unit, &packet);
	break;

     case ACTION_RENAME_OBJECT:
	action_rename_object(unit, &packet);
	break;

     case ACTION_SET_DATE:
	action_set_date(unit, &packet);
	break;

     case ACTION_CURRENT_VOLUME:
	action_current_volume(unit, &packet);
	break;

     case ACTION_RENAME_DISK:
	action_rename_disk(unit, &packet);
	break;

     case ACTION_IS_FILESYSTEM:
	action_is_filesystem(unit, &packet);
	break;

     case ACTION_FLUSH:
	action_flush(unit, &packet);
	break;

     default:
	TRACE(("*** UNSUPPORTED PACKET %ld\n", packet.type));
	packet.res1 = DOS_FALSE;
	packet.res2 = ERROR_ACTION_NOT_KNOWN;
	break;
    }

    put_long(packet.addr + dp_Res1, packet.res1);
    put_long(packet.addr + dp_Res2, packet.res2);
    TRACE(("reply: %8lx, %ld\n", packet.res1, packet.res2));

    return 0;
}

static ULONG filesys_diagentry(void)
{
    CPTR resaddr = m68k_areg(regs, 2) + 0x10;
    
    filesys_configdev = m68k_areg(regs, 3);
    
    if (ROM_hardfile_resid != 0) {
	/* Build a struct Resident. This will set up and initialize
	 * the uae.device */
	put_word(resaddr + 0x0, 0x4AFC);
	put_long(resaddr + 0x2, resaddr);
	put_long(resaddr + 0x6, resaddr + 0x1A); /* Continue scan here */
	put_word(resaddr + 0xA, 0x8101); /* RTF_AUTOINIT|RTF_COLDSTART; Version 1 */
	put_word(resaddr + 0xC, 0x0305); /* NT_DEVICE; pri 05 */
	put_long(resaddr + 0xE, ROM_hardfile_resname);
	put_long(resaddr + 0x12, ROM_hardfile_resid);
	put_long(resaddr + 0x16, ROM_hardfile_init);
    }
    resaddr += 0x1A;

    /* The good thing about this function is that it always gets called
     * when we boot. So we could put all sorts of stuff that wants to be done
     * here. */
    
    return 1;
}

static CPTR build_parmpacket(void)
{
    CPTR tmp1;

    m68k_dreg(regs, 0) = 88; m68k_dreg(regs, 1) = 1; /* MEMF_PUBLIC */
    tmp1 = CallLib (get_long(4), -198); /* AllocMem() */
    if (tmp1 == 0) {
	fprintf(stderr, "Not enough memory for filesystem!\n");
	return 0;
    }

    put_long (tmp1+12, 0); /* Device flags */
    put_long (tmp1+16, 16); /* Env. size */
    put_long (tmp1+20, 128); /* 512 bytes/block */
    put_long (tmp1+24, 0); /* unused */
    put_long (tmp1+28, 1); /* heads */
    put_long (tmp1+32, 1); /* unused */
    put_long (tmp1+36, 32); /* secs per track */
    put_long (tmp1+40, 1); /* reserved blocks */
    put_long (tmp1+44, 0); /* unused */
    put_long (tmp1+48, 0); /* interleave */
    put_long (tmp1+52, 0); /* lowCyl */
    {
    extern int numtracks;
    put_long (tmp1+56, numtracks-1); /* upperCyl */
    }
    put_long (tmp1+60, 0); /* Number of buffers */
    put_long (tmp1+64, 0); /* Buffer mem type */
    put_long (tmp1+68, 0x7FFFFFFF); /* largest transfer */
    put_long (tmp1+72, ~1); /* addMask (?) */
    put_long (tmp1+76, (ULONG)-1); /* bootPri */
#if 0
    if (have36)
	put_long (tmp1+80, 0x444f5301); /* DOS\1 */
    else
#endif
	put_long (tmp1+80, 0x444f5300); /* DOS\0 */

    put_long (tmp1+84, 0); /* pad */
    return tmp1;
}

static void make_dev(CPTR param_packet, int unit_no, int is_hardfile, int boot)
{
    CPTR devicenode, bootnode;

    put_long (param_packet, ui[unit_no].devname_amiga);
    put_long (param_packet + 4, is_hardfile ? ROM_hardfile_resname : fsdevname);
    put_long (param_packet + 8, ui[unit_no].devno);
    
    m68k_areg(regs, 0) = param_packet;
    devicenode = CallLib (EXPANSION_explibbase, -144); /* MakeDosNode() */
    ui[unit_no].startup = get_long(devicenode + 28);
    if (!is_hardfile) {
	put_long(devicenode+8, 0x0); /* dn_Task */
	put_long(devicenode+16, 0x0); /* dn_Handler */
	put_long(devicenode+20, 4000); /* dn_StackSize */
	put_long(devicenode+32, filesysseglist >> 2); /* dn_SegList */
	put_long(devicenode+36, (ULONG)-1); /* dn_GlobalVec */
    } else {
	/* ??? */
	put_long(devicenode+8, 0x0); /* dn_Task */
	put_long(devicenode+16, 0x0); /* dn_Handler */
	put_long(devicenode+32, 0); /* dn_SegList */
    }
    
    if (boot) {
	if (EXPANSION_haveV36) {
	    m68k_dreg(regs, 0) = -1; m68k_dreg(regs, 1) = 0;
	    m68k_areg(regs, 0) = devicenode;
	    m68k_areg(regs, 1) = filesys_configdev;
	    CallLib(EXPANSION_explibbase, -36);
	} else {
	    /* Construct a BootNode and Enqueue() it into eb_MountList */
	    m68k_dreg(regs, 0) = 20;
	    m68k_dreg(regs, 1) = 0;
	    bootnode = CallLib (get_long(4), -198); /* AllocMem() */

	    put_word (bootnode + 14, 0);              /* Flags (??) */
	    put_long (bootnode + 16, devicenode);
	    put_word (bootnode + 8, 0x10FF-unit_no);          /* Type/Pri */
	    put_long (bootnode + 10, filesys_configdev); /* Name */
	    put_long (bootnode + 0, 0);
	    put_long (bootnode + 4, 0);
	    m68k_areg(regs, 0) = EXPANSION_explibbase + 74; /* MountList */
	    m68k_areg(regs, 1) = bootnode;
	    CallLib (get_long(4), -270); /* Enqueue() */
	}
    } else {
	/* Call AddDosNode() for the constructed node */
	m68k_areg(regs, 0) = devicenode;
	m68k_dreg(regs, 0) = (ULONG)-1;
	m68k_areg(regs, 1) = 0;
	m68k_dreg(regs, 1) = 0;		/* Flags */
	CallLib (EXPANSION_explibbase, -150); /* AddDosNode() */
    }
}
    
void filesys_init(void)
{
    int i;
    int firstdev = 1;
    
    /* Open expansion.lib */
    
    EXPANSION_haveV36 = 0;
    m68k_dreg(regs, 0) = 36;
    m68k_areg(regs, 1) = EXPANSION_explibname;
    EXPANSION_explibbase = CallLib (get_long(4), -552); /* OpenLibrary() */
    if (EXPANSION_explibbase)
	EXPANSION_haveV36 = 1;
    else {
	m68k_dreg(regs, 0) = 0;
	m68k_areg(regs, 1) = EXPANSION_explibname;
	EXPANSION_explibbase = CallLib (get_long(4), -552); /* OpenLibrary() */
    }

    filesys_parampacket = build_parmpacket();

    /* re-use the same parameter packet to make each
     * dos node, which will then get tweaked
     */

    for(i = 0; i < num_units; i++) {
	int is_hardfile = ui[i].volname == NULL;
	if (is_hardfile && !EXPANSION_haveV36) {
	    fprintf(stderr, "Kickstart is older than 2.0, please mount hardfile manually.\n");
	    continue;
	}
	make_dev(filesys_parampacket, i, is_hardfile, 1);
    }

    m68k_areg(regs, 1) = EXPANSION_explibbase;
    CallLib (get_long(4), -414); /* CloseLibrary() */
    EXPANSION_explibbase = 0;
}

void filesys_install(void)
{
    int i;
    CPTR loop;

    ROM_filesys_resname = ds("UAEunixfs.resource");
    ROM_filesys_resid = ds("UAE unixfs 0.2");

    fsdevname = ds("uae.device"); /* does not really exist */

    for(i = 0; i < num_units; i++) {
	ui[i].devno = get_new_device(&ui[i].devname, &ui[i].devname_amiga);
    }

    ROM_filesys_diagentry = here();
    calltrap2(deftrap(filesys_diagentry)); dw(RTS);
    
    /* align */
    align(4);
    /* Fake seglist */
    dl(16);
    filesysseglist = here();
    dl(0); /* NextSeg */

    /* start of code */

    /* I don't trust calling functions that Wait() directly,
     * so here's a little bit of 68000 code to receive and send our
     * DosPackets
     */
    dw(0x2c79); dl(4);		/* move.l	$4,a6 */
    dw(0x2279); dl(0);		/* move.l	0,a1 */
    dw(0x4eae); dw(0xfeda);	/* jsr		FindTask(a6) */
    dw(0x2040);			/* move.l	d0,a0 */
    dw(0x4be8); dw(0x005c);	/* lea.l	pr_MsgPort(a0),a5 */
				/* loop: */
    loop = here();
    dw(0x204d);			/* move.l	a5,a0 */
    dw(0x4eae); dw(0xfe80);	/* jsr		WaitPort(a6) */
    dw(0x204d);			/* move.l	a5,a0 */
    dw(0x4eae); dw(0xfe8c);	/* jsr		GetMsg(a6) */
    dw(0x2840);			/* move.l	d0,a4 */
    dw(0x286c); dw(10);		/* move.l	LN_NAME(a4),a4 */
    calltrap2(deftrap(filesys_handler));
    dw(0x226c);	dw(0);		/* move.l	dp_Link(a4),a1 */
    dw(0x206c); dw(4);		/* move.l	dp_Port(a4),a0 */
    dw(0x294d); dw(4);		/* move.l	a5,dp_Port(a4) */
    dw(0x4eae); dw(0xfe92);	/* jsr		PutMsg(a6) */
    dw(0x4ef9); dl(loop);	/* jmp          loop */

}
