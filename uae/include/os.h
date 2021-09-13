 /* 
  * UAE - The Un*x Amiga Emulator
  * 
  * OS specific functions
  * 
  * (c) 1995 Bernd Schmidt
  */

extern void read_joystick(UWORD *dir, int *button);
extern void init_joystick(void);
extern void close_joystick(void);

extern struct audio_channel_data {
    CPTR lc, pt, dmaen;
    int data_written, snum, state, intreq2, wper, wlen;
    UWORD dat, nextdat, vol, per, len;
    int current_sample;
} audio_channel[4];

extern int sound_available;
extern int joystickpresent;

extern int init_sound (void);
extern void close_sound (void);

#ifndef DONT_WANT_SOUND
extern void aud0_handler(void);
extern void aud1_handler(void);
extern void aud2_handler(void);
extern void aud3_handler(void);
extern void sample16_handler (void);
extern void sample8_handler (void);
#endif
extern UWORD *sndbufpt;
extern int sndbufsize;
extern void init_sound_table16(void);
extern void init_sound_table8(void);

extern void AUDxDAT(int nr, UWORD value);

