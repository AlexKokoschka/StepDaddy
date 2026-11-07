#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "audio.h"
uint64_t test_now_us=1000000;
void telemetry_stack_init(unsigned n){(void)n;}
int main(void){
 audio_start_core1();int16_t out[96];uint8_t notes[4]={48,52,55,59};
 audio_chord_on(notes,4,true,VOICE_RETRIGGER);
 long long energy=0;
 for(int ms=0;ms<2000;ms++){
  test_now_us+=1000;audio_service();audio_read_stereo(out,48);
  for(int i=0;i<48;i++){assert(out[2*i]==out[2*i+1]);energy+=out[2*i]<0?-out[2*i]:out[2*i];}
 }
 assert(energy>0&&audio_underruns()==0&&audio_event_dropped()==0);
 audio_profile_reset();audio_service();assert(!audio_profile_reset_pending());
 audio_profile_t p;assert(audio_profile_get(&p)&&p.generation==1);
 // Deliberately starve service, check real counter, then recover.
 int16_t large[2048];audio_read_stereo(large,1024);assert(audio_underruns()>0);
 uint32_t missed=audio_underruns();audio_service();audio_read_stereo(out,48);
 assert(audio_underruns()==missed);
 puts("PASS Teensy audio scheduling: real DSP/service/ring, 48 kHz dual mono, four-note playback, reset acknowledgement, starvation detection and recovery");
}
