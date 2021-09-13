 /*
  * UAE - The Un*x Amiga Emulator
  *
  * routines to handle compressed file automatically
  *
  * (c) 1996 Samuel Devulder, Tim Gunn
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"
#include "zfile.h"

#ifdef USE_ZFILE

#ifdef AMIGA
extern char *amiga_dev_path;   /* dev: */
extern char *ixemul_dev_path;  /* /dev/ */
extern int readdevice(const char *, char *);
#endif

#ifdef __BEOS__
#undef access
// This is just a temporary hack.
// (Will be obsolete in future versions of the BeOS) [ So I hope -- Bernd ]

static int access(const char *name, int mode)
{
    struct stat statbuf;

    if (-1 == stat(name, &statbuf))
    	return(-1);
    else{
		if (!mode)
			return 0;
		if ((mode & R_OK) && (statbuf.st_mode & S_IRUSR))
			return 0;
		if ((mode & W_OK) && (statbuf.st_mode & S_IWUSR))
			return 0;
		return -1;
	}
}
#endif


static struct zfile
{
  struct zfile *next;
  FILE *f;
  char name[L_tmpnam];
} *zlist;

/*
 * called on exit()
 */
void zfile_exit(void)
{
  struct zfile *l;

  while((l = zlist))
    {
      zlist = l->next;
      fclose(l->f);
      free(l);
    }
}

/*
 * fclose() but for a compressed file
 */
int zfile_close(FILE *f)
{
  struct zfile *pl = NULL,
               *l  = zlist;
  int ret;

  while(l && l->f!=f) {pl = l;l=l->next;}
  if(!l) return fclose(f);
  ret = fclose(l->f);

  if(!pl) zlist = l->next;
  else pl->next = l->next;
  free(l);

  return ret;
}

/*
 * gzip decompression
 */
static int gunzip(const char *src, const char *dst)
{
  char cmd[1024];
  if(!dst) return 1;
  sprintf(cmd,"gzip -d -c %s >%s",src,dst);
  return !system(cmd);
}

/*
 * lha decompression
 */
static int lha(const char *src, const char *dst)
{
  char cmd[1024];
  if(!dst) return 1;
#if defined(AMIGA)
  sprintf(cmd,"lha -q -N p %s >%s",src,dst);
#else
  sprintf(cmd,"lha pq %s >%s",src,dst);
#endif
  return !system(cmd);
}

/*
 * (pk)unzip decompression
 */
static int unzip(const char *src, const char *dst)
{
  char cmd[1024];
  if(!dst) return 1;
#if defined(AMIGA)
  sprintf(cmd,"unzip -p %s '*.adf' >%s",src,dst);
  return !system(cmd);
#else
  return gunzip(src,dst); /* I don't know for unix */
#endif
}

/*
 * decompresses the file (or check if dest is null)
 */
static int uncompress(const char *name, char *dest)
{
    char *ext = strrchr(name, '.');
    char nam[1024];

    if (ext != NULL && access(name,0) >= 0) {
	ext++;
	if(!strcasecmp(ext,"z")  ||
	   !strcasecmp(ext,"gz") ||
	   !strcasecmp(ext,"adz") ||
	   !strcasecmp(ext,"roz") ||
	   0) return gunzip(name,dest);
#ifndef __DOS__
	if(!strcasecmp(ext,"lha") ||
	   !strcasecmp(ext,"lzh") ||
	   0) return lha(name,dest);
	if(!strcasecmp(ext,"zip") ||
	   0) return unzip(name,dest);
#endif
    }

    if(access(strcat(strcpy(nam,name),".z"),0)>=0  ||
       access(strcat(strcpy(nam,name),".Z"),0)>=0  ||
       access(strcat(strcpy(nam,name),".gz"),0)>=0 ||
       access(strcat(strcpy(nam,name),".GZ"),0)>=0 ||
       access(strcat(strcpy(nam,name),".adz"),0)>=0 ||
       access(strcat(strcpy(nam,name),".roz"),0)>=0 ||
       0) return gunzip(nam,dest);

#ifndef __DOS__
    if(access(strcat(strcpy(nam,name),".lha"),0)>=0 ||
       access(strcat(strcpy(nam,name),".LHA"),0)>=0 ||
       access(strcat(strcpy(nam,name),".lzh"),0)>=0 ||
       access(strcat(strcpy(nam,name),".LZH"),0)>=0 ||
       0) return lha(nam,dest);

    if(access(strcat(strcpy(nam,name),".zip"),0)>=0 ||
       access(strcat(strcpy(nam,name),".ZIP"),0)>=0 ||
       0) return unzip(nam,dest);
#endif

#if defined(AMIGA)
    if(!strnicmp(nam,ixemul_dev_path,strlen(ixemul_dev_path)))
	return readdevice(name+strlen(ixemul_dev_path),dest);
    if(!strnicmp(nam,amiga_dev_path,strlen(amiga_dev_path)))
	return readdevice(name+strlen(amiga_dev_path),dest);
#endif

    return 0;
}

/*
 * fopen() for a compressed file
 */
FILE *zfile_open(const char *name, const char *mode)
{
    struct zfile *l;
    int fd = 0;
    
    if(!uncompress(name,NULL)) return fopen(name,mode);
    
    if(!(l = malloc(sizeof(*l)))) return NULL;

    tmpnam(l->name);
#if !defined(AMIGA)
    /* On the amiga this would make ixemul loose the break handler */
    fd = creat(l->name, 0666);
    if (fd < 0)
	return NULL;
#endif
    
    if(!uncompress(name,l->name)) 
    {
	free(l); close (fd); unlink(l->name); return NULL; 
    }

    l->f=fopen(l->name,mode);
    
    /* Deleting the file now will cause the space for it to be freed
     * as soon as we fclose() it. This saves bookkeeping work.
     */
#ifndef __DOS__
    unlink (l->name);
#endif

#if !defined(AMIGA)
    close (fd);
#endif
    
    if (l->f == NULL) {
	free(l);
	return NULL;
    }

    l->next = zlist;
    zlist   = l;

    return l->f;
}

#else

/*
 * Stubs for machines that this doesn't work on.
 */

void zfile_exit(void)
{
}

int zfile_close(FILE *f)
{
    return fclose(f);
}

FILE *zfile_open(const char *name, const char *mode)
{
    return fopen(name, mode);
}

#endif
