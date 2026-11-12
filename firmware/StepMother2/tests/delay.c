#include "voice303.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
void ref_voice_init(voice_t *v);
void ref_voice_set_params(voice_t *v,const voice_params_t *p);
void ref_voice_set_tempo(voice_t *v,float bpm);
void ref_voice_note_on(voice_t *v,int note,bool accent,bool legato);
void ref_voice_note_off(voice_t *v);
void ref_voice_render(voice_t *v,int16_t *out,int n);
static voice_t current,reference;
static unsigned compared;
static void render(int n){
 int16_t a[16],b[16];
 while(n>0){int k=n<16?n:16;
  voice_render(&current,a,k);ref_voice_render(&reference,b,k);
  assert(memcmp(a,b,(size_t)k*sizeof(*a))==0);
  assert(current.dl_w==reference.dl_w);
  assert(current.dl_lp==reference.dl_lp&&current.dl_hp==reference.dl_hp);
  compared+=(unsigned)k;n-=k;
 }
}
static void amount(float value){
 voice_params_t p=current.p;p.delay_amount=value;
 voice_set_params(&current,&p);ref_voice_set_params(&reference,&p);
}
static void tempo(float bpm){voice_set_tempo(&current,bpm);ref_voice_set_tempo(&reference,bpm);}
int main(void){
 for(int mode=0;mode<2;mode++){
  voice_init(&current);ref_voice_init(&reference);
  voice_note_on(&current,48,true,false);ref_voice_note_on(&reference,48,true,false);
  // Deliberately stale physical memory must stay invisible after activation.
  for(int i=0;i<VOICE_DELAY_LEN;i++)current.dl_buf[i]=(int16_t)(10000+i%10000);
  current.dl_w=reference.dl_w=VOICE_DELAY_LEN-7; // exercise wrap immediately
  voice_params_t p=current.p;p.dist_type=(uint8_t)mode;p.distortion=0.8f;
  p.delay_time=0.0f;
  voice_set_params(&current,&p);ref_voice_set_params(&reference,&p);
  amount(0.7f);assert(current.dl_valid==0);
  // Activation did not touch history: invalid memory is logically zero.
  assert(current.dl_buf[123]==10123);
  render(VOICE_DELAY_LEN*2+13);assert(current.dl_valid==VOICE_DELAY_LEN);
  // Brief off/on preserves the existing tail, exactly as before.
  amount(0);render(1703);amount(0.65f);render(6000);
  // Fully inactive, new activation, and fractional delay-time glide.
  amount(0);render(VOICE_DELAY_LEN+17);assert(!current.dl_on);
  for(int i=0;i<VOICE_DELAY_LEN;i++)current.dl_buf[i]=12345;
  amount(0.8f);assert(current.dl_valid==0);
  tempo(173.0f);render(VOICE_DELAY_LEN+9);
  // Repeated activations and automation while history is partially valid.
  for(int j=0;j<8;j++){
   amount(0);render(j%2?VOICE_DELAY_LEN+3:100);
   amount(0.2f+(float)j*0.08f);
   p=current.p;p.delay_time=(float)(j%6)/6.0f;
   voice_set_params(&current,&p);ref_voice_set_params(&reference,&p);
   tempo(60.0f+(float)j*19.0f);render(1137);
  }
  voice_note_off(&current);ref_voice_note_off(&reference);render(VOICE_DELAY_LEN*2);
  // Extreme supported API input to exercise di=0 and both fractional taps.
  amount(0);render(VOICE_DELAY_LEN+1);tempo(10000000.0f);
  p=current.p;p.delay_time=0;voice_set_params(&current,&p);ref_voice_set_params(&reference,&p);
  amount(0.5f);render(VOICE_DELAY_LEN*2+33);
 }
 printf("PASS delay reset: %u bit-identical samples vs supplied Pass 3, stale-history rejection, wrap, fractional reads, rapid toggles and full-off reactivation\n",compared);
}
