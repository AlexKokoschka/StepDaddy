#include "voice303.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
static voice_t v;
static uint32_t rng=0x12345678;
static uint32_t random_u32(void) {rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static float knob(void) {return (float)(random_u32()&65535)/65535.0f;}
int main(void) {
  int16_t out[79];uint64_t energy=0;
  voice_init(&v);
  // 80k irregular calls: roughly 66 seconds at 48k; automate 6 controls,
  // waveform/mode, notes/chords/slide/gates, volume and delay.
  for(int block=0;block<80000;block++) {
    if(block%19==0) {
      voice_params_t p=v.p; p.dist_drive=knob();
      p.cutoff=knob();p.resonance=knob();p.envmod=knob();p.decay=knob();
      p.accent=knob();p.distortion=block%57==0 ? 0 : knob();p.volume=1;
      p.wave=(voice_wave_t)(random_u32()&1);p.dist_type=random_u32()&1;
      p.delay_amount=block%171==0 ? 0 : knob();p.delay_time=knob();
      voice_set_params(&v,&p);
    }
    if(block%37==0) {
      uint8_t notes[4];int n=1+(int)(random_u32()%4);
      for(int i=0;i<n;i++)notes[i]=(uint8_t)(24+random_u32()%96);
      voice_chord_on(&v,notes,n,random_u32()&1,(voice_mode_t)(random_u32()%3));
    }
    if(block%83==0)voice_note_off(&v);
    int count=1+(int)(random_u32()%79);
    voice_render(&v,out,count);
    for(int i=0;i<count;i++)energy+=(uint64_t)((int32_t)out[i]*out[i]);
    if(!isfinite(v.last_cutoff_hz) || v.last_cutoff_hz<20 || v.last_cutoff_hz>16000) return 1;
  }
  if(!energy)return 1;
  puts("PASS deterministic long automation/note/chord/mode/delay stress under UBSan");
  return 0;
}
