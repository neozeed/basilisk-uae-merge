 /* 
  * UAE - The Un*x Amiga Emulator
  * 
  * OS specific functions
  * 
  * (c) 1995 Bernd Schmidt
  * (c) 1996 Marcus Sundberg
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"
#include "memory.h"
#include "custom.h"
#include "os.h"
#include "events.h"

#ifndef DONT_WANT_SOUND
static void sample_ulaw_handler(void);
#endif

int joystickpresent = 0;

#ifdef HAVE_LINUX_JOYSTICK_H

static int js0;

struct JS_DATA_TYPE jscal;

void read_joystick(UWORD *dir, int *button)
{
    static int minx = MAXINT, maxx = MININT,
               miny = MAXINT, maxy = MININT;
    int left = 0, right = 0, top = 0, bot = 0;
    struct JS_DATA_TYPE buffer;
    int len;
    
    *dir = 0;
    *button = 0;
    if (!joystickpresent)
    	return;
    
    len = read(js0, &buffer, sizeof(buffer));
    if (len != sizeof(buffer)) 
    	return;
    
    if (buffer.x < minx) minx = buffer.x;
    if (buffer.y < miny) miny = buffer.y;
    if (buffer.x > maxx) maxx = buffer.x;
    if (buffer.y > maxy) maxy = buffer.y;
    
    if (buffer.x < (minx + (maxx-minx)/3))
    	left = 1;
    else if (buffer.x > (minx + 2*(maxx-minx)/3))
    	right = 1;

    if (buffer.y < (miny + (maxy-miny)/3))
    	top = 1;
    else if (buffer.y > (miny + 2*(maxy-miny)/3))
    	bot = 1;
    	
    if (left) top = !top;
    if (right) bot = !bot;
    *dir = bot | (right << 1) | (top << 8) | (left << 9);
    *button = (buffer.buttons & 3) != 0;
}

void init_joystick(void)
{
    js0 = open("/dev/js0", O_RDONLY);
    if (js0 < 0)
    	return;
    joystickpresent = 1;
}

void close_joystick(void)
{
    if (joystickpresent)
	close(js0);
}

#elif defined(__DOS__)

void read_joystick(UWORD *dir, int *button)
{
    static int minx = MAXINT, maxx = MININT,
	       miny = MAXINT, maxy = MININT;
    int left = 0, right = 0, top = 0, bot = 0;
    char JoyPort;
    int laps, JoyX, JoyY;

    *dir = 0;
    *button = 0;
    if (!joystickpresent)
	return;

    JoyX = 0;
    JoyY = 0;
    laps = 0;
    __asm__ __volatile__("cli");
    outportb(0x201, 0xff);
    do {
	JoyPort = inportb(0x201);
	JoyX = JoyX + (JoyPort & 1);
	JoyY = JoyY + ((JoyPort & 2) >> 1);
	laps++;
    } while(((JoyPort & 3) != 0) && (laps != 65535));
    __asm__ __volatile__("sti");

    if (JoyX < minx) minx = JoyX;
    if (JoyY < miny) miny = JoyY;
    if (JoyX > maxx) maxx = JoyX;
    if (JoyY > maxy) maxy = JoyY;

    if (JoyX < (minx + (maxx-minx)/3))
	left = 1;
    else if (JoyX > (minx + 2*(maxx-minx)/3))
	right = 1;

    if (JoyY < (miny + (maxy-miny)/3))
	top = 1;
    else if (JoyY > (miny + 2*(maxy-miny)/3))
	bot = 1;

    if (left) top = !top;
    if (right) bot = !bot;
    *dir = bot | (right << 1) | (top << 8) | (left << 9);
    *button = ((~JoyPort) & 48) != 0;
}

void init_joystick(void)
{
    int laps = 0;
    char JoyPort;
    __asm__ __volatile__("cli");
    outportb(0x201, 0xff);
    do {
	JoyPort = inportb(0x201);
	laps++;
    } while(((JoyPort & 3) != 0) && (laps != 65535));
    __asm__ __volatile__("sti");
    if (laps != 65535)
	joystickpresent = 1;
}

void close_joystick(void)
{
}

#elif defined(AMIGA)

#define EXEC_TYPES_H /* exec/types.h has trouble as LONG, ULONG, etc. */
typedef void *APTR;  /* are already defined. This avoid the trouble.  */
typedef char *STRPTR;
typedef float *FLOAT;
typedef int BOOL;
#define VOID void

#include <hardware/custom.h>
#include <hardware/cia.h>

#define CIAAPRA 0xBFE001 
#define CUSTOM  0xDFF000

static struct Custom *custom= (struct Custom*) CUSTOM;
static struct CIA *cia = (struct CIA *) CIAAPRA;

void read_joystick(UWORD *dir, int *button)
{
    int bot, right, top, left, joy,fire;
    
    joy   = custom->joy1dat;
    fire  = !( cia->ciapra & 0x0080 ) ? 1 : 0;

    right = (joy & 0x0002) ? 1 : 0;
    left  = (joy & 0x0200) ? 1 : 0;
    bot   = (joy & 0x0001) ? 1 : 0;
    top   = (joy & 0x0100) ? 1 : 0;
    
    *button = fire;
    *dir = bot | (right << 1) | (top << 8) | (left << 9);
}

void init_joystick(void)
{
}

void close_joystick(void)
{
}

#elif !defined(__BEOS__)

void read_joystick(UWORD *dir, int *button)
{
    *dir = 0;
    *button = 0;
}

void init_joystick(void)
{
}

void close_joystick(void)
{
}

#endif

int sound_available = 0;

struct audio_channel_data audio_channel[4];

#ifdef __BEOS__
UWORD *sndbuffer;
#elif !defined(AMIGA)
/* The buffer is too large... */
static UWORD sndbuffer[44100];
#endif
UWORD *sndbufpt;

static ULONG data;

int sound_table[256][64];
static int dspbits = 16;
int sndbufsize;

void init_sound_table16(void)
{
    int i,j;
    
    for (i = 0; i < 256; i++)
	for (j = 0; j < 64; j++)
	    sound_table[i][j] = j * (BYTE)i;
}

void init_sound_table8 (void)
{
    int i,j;
    
    for (i = 0; i < 256; i++)
	for (j = 0; j < 64; j++)
	    sound_table[i][j] = (j * (BYTE)i) / 256;
}

static int exact_log2(int v)
{
    int l = 0;
    while ((v >>= 1) != 0)
	l++;
    return l;
}

#ifdef LINUX_SOUND

#include <sys/ioctl.h>
#include <sys/soundcard.h>

static int sfd;
static int have_sound;

void close_sound(void)
{
    if (have_sound)
	close(sfd);
}

int init_sound (void)
{
    int tmp;
    int rate;
    
    unsigned long formats;
    
    sfd = open ("/dev/dsp", O_WRONLY);
    have_sound = !(sfd < 0);
    if (!have_sound) {
	return 0;
    }
    
    ioctl (sfd, SNDCTL_DSP_GETFMTS, &formats);

    if (sound_desired_bsiz < 128 || sound_desired_bsiz > 16384) {
	fprintf(stderr, "Sound buffer size %d out of range.\n", sound_desired_bsiz);
	sound_desired_bsiz = 8192;
    }
    
    tmp = 0x00040000 + exact_log2(sound_desired_bsiz);
    ioctl (sfd, SNDCTL_DSP_SETFRAGMENT, &tmp);
    ioctl (sfd, SNDCTL_DSP_GETBLKSIZE, &sndbufsize);

    dspbits = sound_desired_bits;
    ioctl(sfd, SNDCTL_DSP_SAMPLESIZE, &dspbits);
    ioctl(sfd, SOUND_PCM_READ_BITS, &dspbits);
    if (dspbits != sound_desired_bits) {
	fprintf(stderr, "Can't use sound with %d bits\n", sound_desired_bits);
	return 0;
    }

    tmp = 0;
    ioctl(sfd, SNDCTL_DSP_STEREO, &tmp);
    
    rate = sound_desired_freq;
    ioctl(sfd, SNDCTL_DSP_SPEED, &rate);
    ioctl(sfd, SOUND_PCM_READ_RATE, &rate);
    /* Some soundcards have a bit of tolerance here. */
    if (rate < sound_desired_freq * 90 / 100 || rate > sound_desired_freq * 110 / 100) {
	fprintf(stderr, "Can't use sound with desired frequency %d\n", sound_desired_freq);
	return 0;
    }

    eventtab[ev_sample].evtime = (long)maxhpos * maxvpos * 50 / rate;

    if (dspbits == 16) {
	/* Will this break horribly on Linux/Alpha? Possible... */
	if (!(formats & AFMT_S16_LE))
	    return 0;
	init_sound_table16 ();
	eventtab[ev_sample].handler = sample16_handler;
    } else {
	if (!(formats & AFMT_U8))
	    return 0;
	init_sound_table8 ();
	eventtab[ev_sample].handler = sample8_handler;
    }
    sound_available = 1;
    printf ("Sound driver found and configured for %d bits at %d Hz, buffer is %d bytes\n",
	    dspbits, rate, sndbufsize);
    sndbufpt = sndbuffer;
    return 1;
}

static void flush_sound_buffer(void)
{
    write(sfd, sndbuffer, sndbufsize);
    sndbufpt = sndbuffer;
}

#elif defined(AF_SOUND)

#include <AF/AFlib.h>

static AFAudioConn  *aud;
static AC            ac;
static long          aftime;
static int           rate;

static int have_sound;

void close_sound(void)
{
}

int init_sound (void)
{
    AFSetACAttributes   attributes;
    AFDeviceDescriptor *aDev;
    int                 device;
    
    aud = AFOpenAudioConn(NULL);
    have_sound = !(aud == NULL);
    if (!have_sound) {
	return 0;
    }
    
    for(device = 0; device < ANumberOfAudioDevices(aud); device++) {
	aDev = AAudioDeviceDescriptor(aud, device);
	rate = aDev->playSampleFreq;
	sndbufsize = (rate / 8) * 4;
	if(aDev->inputsFromPhone == 0
	   && aDev->outputsToPhone == 0
	   && aDev->playNchannels == 1)
	    break;
    }
    if (device == ANumberOfAudioDevices(aud)) {
	return 0;
    }
    
    dspbits = 16;
    attributes.type = LIN16;
    ac = AFCreateAC(aud, device, ACEncodingType, &attributes);
    aftime = AFGetTime(ac);

    init_sound_table16 ();
    eventtab[ev_sample].handler = sample16_handler;
    eventtab[ev_sample].evtime = (long)maxhpos * maxvpos * 50 / rate;
    
    sndbufpt = sndbuffer;
    sound_available = 1;
    printf ("Sound driver found and configured for %d bits at %d Hz, buffer is %d bytes\n", dspbits, rate, sndbufsize);
    return 1;
}

static void flush_sound_buffer(void)
{
    long size = (char *)sndbufpt - (char *)sndbuffer;
    if (AFGetTime(ac) > aftime)
	aftime = AFGetTime(ac);
    AFPlaySamples(ac, aftime, size, (unsigned char*) sndbuffer);
    aftime += size / 2;
    sndbufpt = sndbuffer;
}

#elif defined(__mac__)

#include <Sound.h>

static SndChannelPtr newChannel;
static ExtSoundHeader theSndBuffer;
static SndCommand theCmd;

/* The buffer is too large... */
static UWORD buffer0[44100], buffer1[44100], *sndbufpt;

static int have_sound;
static int nextbuf=0;
static Boolean sFlag=true;

void close_sound(void)
{
}

int init_sound (void)
{	
    if (SndNewChannel(&newChannel, sampledSynth, initMono, NULL)) 
	return 0;
    sndbufsize = 44100;
    init_sound_table8 ();

    sndbufpt = buffer0;
    sound_available = 1;
    return 1;
}

static void flush_sound_buffer(void)
{
    sndbufpt = buffer0;
    
    theSndBuffer.samplePtr = (Ptr)buffer0;
    theSndBuffer.numChannels = 1;
    theSndBuffer.sampleRate = 0xac440000;
    theSndBuffer.encode = extSH;
    theSndBuffer.numFrames = sndbufsize;
    theSndBuffer.sampleSize = 8;
    theCmd.param1 = 0;
    theCmd.param2 = (long)&theSndBuffer;
    theCmd.cmd = bufferCmd;
    SndDoCommand(newChannel, &theCmd, false);    
}

#elif defined(__DOS__)

#include "sound/sb.h"
#include "sound/gus.h"

void (*SND_Write)(void *buf, unsigned long size);  // Pointer to function that plays data on card

void close_sound(void)
{
}

int init_sound (void)
{
    int rate;

    dspbits = sound_desired_bits;
    rate = sound_desired_freq;
    if ((rate != 22050) && (rate != 44100)) {
	fprintf(stderr, "Can't use sample rate %d!\n", rate);
	return 0;
    }
	
    if (GUS_Init(&dspbits, &rate, &sndbufsize));
    else if (SB_DetectInitSound(&dspbits, &rate, &sndbufsize));
    else if (0/*OTHER_CARD_DETECT_ROUTINE*/);
    else
	return 0;
    
    eventtab[ev_sample].evtime = (long)maxhpos * maxvpos * 50 / rate;

    if (dspbits == 16) {
	init_sound_table16 ();
	eventtab[ev_sample].handler = sample16_handler;
    } else {
	init_sound_table8 ();
	eventtab[ev_sample].handler = sample8_handler;
    }
    sound_available = 1;
    fprintf(stderr, "Sound driver found and configured for %d bits at %d Hz, buffer is %d bytes\n",
	    dspbits, rate, sndbufsize);
    sndbufpt = sndbuffer;
    return 1;
}

static void flush_sound_buffer(void)
{
    SND_Write(sndbuffer, sndbufsize);
    sndbufpt = sndbuffer;
}

#elif defined(SOLARIS_SOUND)

#include <sys/audioio.h>

static int sfd;
static int have_sound;

void close_sound(void)
{
    if (have_sound)
	close(sfd);
}

int init_sound (void)
{
    int rate;

    struct audio_info sfd_info;

    sfd = open("/dev/audio", O_WRONLY);
    have_sound = !(sfd <0);
    if (!have_sound) {
        return 0;
    }
    
    if (sound_desired_bsiz < 128 || sound_desired_bsiz > 44100) {
	fprintf(stderr, "Sound buffer size %d out of range.\n", sound_desired_bsiz);
	sound_desired_bsiz = 8192;
    }

    rate = sound_desired_freq;
    dspbits = sound_desired_bits;
    AUDIO_INITINFO(&sfd_info);
    sfd_info.play.sample_rate = rate;
    sfd_info.play.channels = 1;
    sfd_info.play.precision = dspbits;
    sfd_info.play.encoding = (dspbits == 8 ) ? AUDIO_ENCODING_ULAW : AUDIO_ENCODING_LINEAR;
    if (ioctl(sfd, AUDIO_SETINFO, &sfd_info)) {
	fprintf(stderr, "Can't use sample rate %d with %d bits, %s!\n", rate, dspbits, (dspbits ==8) ? "ulaw" : "linear");
	return 0;
    }
    eventtab[ev_sample].evtime = (long)maxhpos * maxvpos * 50 / rate;

    init_sound_table16 ();

    if (dspbits == 8) {
	eventtab[ev_sample].handler = sample_ulaw_handler;
    } else {
	eventtab[ev_sample].handler = sample16_handler;
    }

    sndbufpt = sndbuffer;
    smplcnt = 0;
    sound_available = 1;
    sndbufsize = sound_desired_bsiz;
    printf ("Sound driver found and configured for %d bits, %s at %d Hz, buffer is %d bytes\n", dspbits, (dspbits ==8) ? "ulaw" : "linear", rate, sndbufsize);
    return 1;
}

static void flush_sound_buffer(void)
{
    write(sfd, sndbuffer, sndbufsize);
    sndbufpt = sndbuffer;
}


#elif defined(AMIGA) && !defined(DONT_WANT_SOUND)
/*
 * Compared to Linux, AF_SOUND, and mac above, the AMIGA sound processing
 * with OS routines is awfull. (sam)
 */
#define DEVICES_TIMER_H /* as there is a conflict on timeval */
#include <proto/exec.h>
#include <proto/alib.h>
#include <proto/dos.h>

#include <exec/memory.h>
#include <exec/devices.h>
#include <exec/io.h>

#include <graphics/gfxbase.h>
#include <devices/timer.h>
#include <devices/audio.h>

static char whichchannel[]={1,2,4,8};
static struct IOAudio *AudioIO;
static struct MsgPort *AudioMP;
static struct Message *AudioMSG;

static unsigned char *buffers[2];
static UWORD *sndbuffer;
static int bufidx, devopen;

static int have_sound, clockval, oldledstate, period;

int init_sound (void) 
{ /* too complex ? No it is only the allocation of a single channel ! */
  /* it would have been far less painfull if AmigaOS provided a */
  /* SOUND: device handler */
    int rate;

    atexit(close_sound);

    if (sound_desired_bsiz < 2 || sound_desired_bsiz > (128*1024)) {
	fprintf(stderr, "Sound buffer size %d out of range.\n", sound_desired_bsiz);
	sound_desired_bsiz = 8192;
    } 
    sndbufsize = (sound_desired_bsiz + 1)&~1;

    /* get the buffers */
    buffers[0] = (void*)AllocMem(sndbufsize,MEMF_CHIP|MEMF_CLEAR);
    buffers[1] = (void*)AllocMem(sndbufsize,MEMF_CHIP|MEMF_CLEAR);
    if(!buffers[0] || !buffers[1]) goto fail;
    bufidx = 0;
    sndbuffer = sndbufpt = (UWORD*)buffers[bufidx];

    /* determine the clock */
    { 
	struct GfxBase *GB;
	GB = (void*)OpenLibrary("graphics.library",0L);
	if(!GB) goto fail;
	if (GB->DisplayFlags & PAL)
	    clockval = 3546895;        /* PAL clock */
	else
	    clockval = 3579545;        /* NTSC clock */
	CloseLibrary((void*)GB);
    }

    if (!sound_desired_freq) sound_desired_freq = 1;
    if (clockval/sound_desired_freq < 124 || clockval/sound_desired_freq > 65535) {
	fprintf(stderr, "Can't use sound with desired frequency %d Hz\n", sound_desired_freq);
        sound_desired_freq = 22000;
    }
    rate   = sound_desired_freq;
    period = (UWORD)(clockval/rate);

    /* setup the stuff */
    AudioMP = CreatePort(0,0);
    if(!AudioMP) goto fail;
    AudioIO = (struct IOAudio *)CreateExtIO(AudioMP, sizeof(struct IOAudio));
    if(!AudioIO) goto fail;

    AudioIO->ioa_Request.io_Message.mn_Node.ln_Pri /*pfew!!*/ = 85;
    AudioIO->ioa_Data = whichchannel;
    AudioIO->ioa_Length = sizeof(whichchannel);
    AudioIO->ioa_AllocKey = 0;
    if(OpenDevice(AUDIONAME, 0, (void*)AudioIO, 0)) goto fail;
    devopen = 1;

    oldledstate = cia->ciapra & (1<<CIAB_LED);
    cia->ciapra |= (1<<CIAB_LED);

    eventtab[ev_sample].evtime = (long)maxhpos * maxvpos * 50 / rate;
    init_sound_table8 ();
    eventtab[ev_sample].handler = sample8_handler;

    printf ("Sound driver found and configured for %d bits at %d Hz, buffer is %d bytes\n",
            8, rate, sndbufsize);

    sound_available = 1;
    return 1;
fail:
    sound_available = 0;
    return 0;
}

void close_sound(void)
{
    if(devopen) {CloseDevice((void*)AudioIO);devopen = 0;}
    if(AudioIO) {DeleteExtIO((void*)AudioIO);AudioIO = NULL;}
    if(AudioMP) {DeletePort((void*)AudioMP);AudioMP = NULL;}
    if(buffers[0]) {FreeMem((APTR)buffers[0],sndbufsize);buffers[0] = 0;}
    if(buffers[1]) {FreeMem((APTR)buffers[1],sndbufsize);buffers[1] = 0;}
    if(sound_available) {
    	cia->ciapra = (cia->ciapra & ~(1<<CIAB_LED)) | oldledstate;
	sound_available = 0;
    }
}

static void flush_sound_buffer(void)
{
    static char IOSent = 0;
    AudioIO->ioa_Request.io_Command = CMD_WRITE;
    AudioIO->ioa_Request.io_Flags   = ADIOF_PERVOL|IOF_QUICK;
    AudioIO->ioa_Data               = (BYTE *)buffers[bufidx];
    AudioIO->ioa_Length             = sndbufsize;
    AudioIO->ioa_Period             = period;
    AudioIO->ioa_Volume             = 64;
    AudioIO->ioa_Cycles             = 1;

    if(IOSent) WaitIO((void*)AudioIO); else IOSent=1;
    BeginIO((void*)AudioIO);

    /* double buffering */
    bufidx = 1 - bufidx;
    sndbuffer = sndbufpt = (UWORD*)buffers[bufidx];
}

#elif !defined(__BEOS__)

int init_sound (void)
{
    produce_sound = 0;
    return 1;
}

void close_sound(void)
{
}

static void flush_sound_buffer(void)
{
}

#endif

void AUDxDAT(int nr, UWORD v) 
{
#ifndef DONT_WANT_SOUND
    struct audio_channel_data *cdp = audio_channel + nr;
    cdp->dat = v;
    if (cdp->state == 0 && !(INTREQR() & (0x80 << nr))) {
	cdp->state = 2;
	INTREQ(0x8000 | (0x80 << nr));
	/* data_written = 2 ???? */
	eventtab[ev_aud0 + nr].evtime = cycles + cdp->per;
	eventtab[ev_aud0 + nr].oldcycles = cycles;
	eventtab[ev_aud0 + nr].active = 1;
	events_schedule();
    }
#endif
}

#ifndef DONT_WANT_SOUND
void sample16_handler(void)
{
    int nr;
    ULONG data = 0;

    eventtab[ev_sample].evtime += cycles - eventtab[ev_sample].oldcycles;
    eventtab[ev_sample].oldcycles = cycles;

    if (produce_sound < 2)
	return;

    for (nr = 0; nr < 4; nr++) {
	if (!(adkcon & (0x11 << nr)))
	    data += sound_table[audio_channel[nr].current_sample][audio_channel[nr].vol];
    }
    *sndbufpt++ = data;
    if ((char *)sndbufpt - (char *)sndbuffer >= sndbufsize) {
	flush_sound_buffer();
    }
}

void sample8_handler(void)
{
    int nr;
    ULONG data = 0;
    unsigned char *bp = (unsigned char *)sndbufpt;
    
    eventtab[ev_sample].evtime += cycles - eventtab[ev_sample].oldcycles;
    eventtab[ev_sample].oldcycles = cycles;
    
    if (produce_sound < 2)
	return;

    for (nr = 0; nr < 4; nr++) {
	if (!(adkcon & (0x11 << nr)))
	    data += sound_table[audio_channel[nr].current_sample][audio_channel[nr].vol];
    }
#ifdef AMIGA
    *bp++ = (unsigned char) data;
#else
    *bp++ = data + 128;
#endif
    sndbufpt = (UWORD *)bp;

    if ((char *)sndbufpt - (char *)sndbuffer >= sndbufsize) {
	flush_sound_buffer();
    }
}

static char int2ulaw(int ch)
{
    int mask;

    if (ch < 0) {
      ch = -ch;
      mask = 0x7f;
    }
    else {
      mask = 0xff;
    }

    if (ch < 32) {
	ch = 0xF0 | ( 15 - (ch/2) );
    } else if (ch < 96) {
        ch = 0xE0 | ( 15 - (ch-32)/4 );
    } else if (ch < 224) {
	ch = 0xD0 | ( 15 - (ch-96)/8 );
    } else if (ch < 480) {
	ch = 0xC0 | ( 15 - (ch-224)/16 );
    } else if (ch < 992 ) {
	ch = 0xB0 | ( 15 - (ch-480)/32 );
    } else if (ch < 2016) {
	ch = 0xA0 | ( 15 - (ch-992)/64 );
    } else if (ch < 4064) {
	ch = 0x90 | ( 15 - (ch-2016)/128 );
    } else if (ch < 8160) {
	ch = 0x80 | ( 15 - (ch-4064)/256 );
    } else {
	ch = 0x80;
    }
    return (char)(mask & ch);
}

static void sample_ulaw_handler(void)
{
    int nr;
    ULONG data = 0;
    char *bp = (char *)sndbufpt;

    eventtab[ev_sample].evtime += cycles - eventtab[ev_sample].oldcycles;
    eventtab[ev_sample].oldcycles = cycles;

    if (produce_sound < 2)
	return;

    for (nr = 0; nr < 4; nr++) {
	if (!(adkcon & (0x11 << nr)))
	    data += sound_table[audio_channel[nr].current_sample][audio_channel[nr].vol];
    }
    *bp++ = int2ulaw(data);
    sndbufpt = (UWORD *)bp;

    if ((char *)sndbufpt - (char *)sndbuffer >= sndbufsize) {
	flush_sound_buffer();
    }
}

static void audio_handler(int nr) 
{
    struct audio_channel_data *cdp = audio_channel + nr;

    switch (cdp->state) {
     case 0:
	fprintf(stderr, "Bug in sound code\n");
	break;

     case 1:
	/* We come here at the first hsync after DMA was turned on. */
	eventtab[ev_aud0 + nr].evtime += maxhpos;
	eventtab[ev_aud0 + nr].oldcycles += maxhpos;
	
	cdp->state = 5;
	INTREQ(0x8000 | (0x80 << nr));
	if (cdp->wlen != 1)
	    cdp->wlen--;
	cdp->nextdat = chipmem_bank.wget(cdp->pt);
	cdp->pt += 2;
	break;

     case 5:
	/* We come here at the second hsync after DMA was turned on. */
	if (produce_sound == 0)
	    cdp->per = 65535;

	eventtab[ev_aud0 + nr].evtime = cycles + cdp->per;
	eventtab[ev_aud0 + nr].oldcycles = cycles;
	cdp->dat = cdp->nextdat;
	cdp->current_sample = (UBYTE)(cdp->dat >> 8);
	cdp->state = 2;
	{
	    int audav = adkcon & (1 << nr);
	    int audap = adkcon & (16 << nr);
	    int napnav = (!audav && !audap) || audav;
	    if (napnav)
		cdp->data_written = 2;
	}
	break;
	
     case 2:
	/* We come here when a 2->3 transition occurs */
	if (produce_sound == 0)
	    cdp->per = 65535;

	cdp->current_sample = (UBYTE)(cdp->dat & 0xFF);
	eventtab[ev_aud0 + nr].evtime = cycles + cdp->per;
	eventtab[ev_aud0 + nr].oldcycles = cycles;

	cdp->state = 3;

	/* Period attachment? */
	if (adkcon & (0x10 << nr)) {
	    if (cdp->intreq2 && cdp->dmaen)
		INTREQ(0x8000 | (0x80 << nr));
	    cdp->intreq2 = 0;

	    cdp->dat = cdp->nextdat;
	    if (cdp->dmaen)
		cdp->data_written = 2;
	    if (nr < 3) {
		if (cdp->dat == 0)
		    (cdp+1)->per = 65535;

		else if (cdp->dat < maxhpos/2 && produce_sound < 3)
		    (cdp+1)->per = maxhpos/2;
		else
		    (cdp+1)->per = cdp->dat;
	    }
	}
	break;
	
     case 3:
	/* We come here when a 3->2 transition occurs */
	if (produce_sound == 0)
	    cdp->per = 65535;

	eventtab[ev_aud0 + nr].evtime = cycles + cdp->per;
	eventtab[ev_aud0 + nr].oldcycles = cycles;
	
	if ((INTREQR() & (0x80 << nr)) && !cdp->dmaen) {
	    cdp->state = 0;
	    cdp->current_sample = 0;
	    eventtab[ev_aud0 + nr].active = 0;
	    break;
	} else {
	    int audav = adkcon & (1 << nr);
	    int audap = adkcon & (16 << nr);
	    int napnav = (!audav && !audap) || audav;
	    cdp->state = 2;
	    
	    if ((cdp->intreq2 && cdp->dmaen && napnav)
		|| (napnav && !cdp->dmaen))
		INTREQ(0x8000 | (0x80 << nr));
	    cdp->intreq2 = 0;
	    
	    cdp->dat = cdp->nextdat;
	    cdp->current_sample = (UBYTE)(cdp->dat >> 8);

	    if (cdp->dmaen && napnav)
		cdp->data_written = 2;
	    
	    /* Volume attachment? */
	    if (audav) {
		if (nr < 3)
		    (cdp+1)->vol = cdp->dat;
	    }
	}
	break;
	    
     default:
	cdp->state = 0;
	eventtab[ev_aud0 + nr].active = 0;
	break;
    }
}

void aud0_handler(void)
{
    audio_handler(0);
}
void aud1_handler(void)
{
    audio_handler(1);
}
void aud2_handler(void)
{
    audio_handler(2);
}
void aud3_handler(void)
{
    audio_handler(3);
}
#endif
