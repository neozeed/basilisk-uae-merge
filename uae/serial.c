 /* 
  * UAE - The Un*x Amiga Emulator
  * 
  *  Serial Line Emulation
  *
  * (c) 1996 Stefan Reinauer <stepan@matrix.kommune.schokola.de>
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"
#include "memory.h"
#include "custom.h"
#include "readcpu.h"
#include "newcpu.h"

#ifndef O_NONBLOCK
//#define O_NONBLOCK O_NDELAY
#define O_NONBLOCK 0
//JASON this is wrong.!!
#endif

void serial_init(void);
void serial_exit(void);

UWORD SERDATR(void);
void  SERPER(UWORD w);
void  SERDAT(UWORD w);

int sersend;
int serrecv;
int sd = -1;

UWORD serdat;

void SERPER(UWORD w)
{
    if (!use_serial)
	return;

#ifndef __DOS__
    if (w&0x8000) fprintf (stdout, "SERPER: 9 Bit Transmission not implemented\n");
    fprintf (stdout, "serial port baudrate set to ");
    switch (w & 0x7fff)
    {
     case 0x2e9b: fprintf (stdout, "  300\n"); return;
     case 0x0ba6: fprintf (stdout, " 1200\n"); return;
     case 0x02e9: fprintf (stdout, " 4800\n"); return;
     case 0x0174: fprintf (stdout, " 9600\n"); return;
     case 0x00b9: fprintf (stdout, "19200\n"); return;
     case 0x005c: fprintf (stdout, "38400\n"); return;
     default:
	fprintf(stdout,"%d (approx.)\n",
		(unsigned int)(3579546.471/(double)((w&0x7fff)+1)));  return;
    }
#else
    if (w&0x8000) fprintf (stderr, "SERPER: 9 Bit Transmission not implemented\n");
    fprintf (stderr, "serial port baudrate set to ");
    switch (w & 0x7fff)
    {
     case 0x2e9b: fprintf (stderr, "  300\n"); return;
     case 0x0ba6: fprintf (stderr, " 1200\n"); return;
     case 0x02e9: fprintf (stderr, " 4800\n"); return;
     case 0x0174: fprintf (stderr, " 9600\n"); return;
     case 0x00b9: fprintf (stderr, "19200\n"); return;
     case 0x005c: fprintf (stderr, "38400\n"); return;
     default:
	fprintf(stderr,"%d (approx.)\n",
		(unsigned int)(3579546.471/(double)((w&0x7fff)+1)));  return;
    }
#endif
}

void SERDAT(UWORD w)
{
    char z;

    if (!use_serial)
	return;

    fprintf(stderr,"SERDAT: wrote 0x%04x\n",w);
    z=(char)(w&0xff);
    if (sd>0) {
	write (sd,&z,1);
    }   
    return;
}

UWORD SERDATR(void)
{
    char z;

    if (!use_serial)
	return 0;

    if ((read (sd, &z,1))==1) {
	serdat=0x6100;
	intreq|=0x0800;
	serdat+=(int)z;
#ifndef __DOS__
	fprintf (stdout,"SERDATR: received 0x%02x\n",(int)z);
#else
	fprintf (stderr,"SERDATR: received 0x%02x\n",(int)z);
#endif
    }
    return serdat;
}

void serial_init(void)
{
    if (!use_serial)
	return;

#ifndef __DOS__
    if ((sd=open(sername,O_RDWR|O_NONBLOCK))<0)
	fprintf (stderr,"Error: Could not open Device %s\n",sername);
#else
    if ((sd=open(sername,O_RDWR|O_NONBLOCK|O_BINARY))<0)
	fprintf (stderr,"Error: Could not open Device %s\n",sername);
#endif
    serdat=0x2000;
    return;
}

void serial_exit(void)
{
    if (sd >= 0)
	close(sd);
    return;
}
