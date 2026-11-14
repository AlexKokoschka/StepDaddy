#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "merged.h"
#include "persist.h"
#include "telemetry.h"
#include "ui.h"
#include "panel.h"
#include "audio.h"
#include <stddef.h>
#include "storage.h"
#include "seq303.h"
uint64_t test_now_us=1000000;
static uint32_t raw_keys,pr,rel;
static uint16_t knob;
static uint8_t led[20];
static voice_t v;
static int midi_note_count[16],midi_off_count[16],audio_note_count;
static long long energy;
static uint8_t last_cc[128];
static unsigned profile_resets;
extern float test_yummy_mix(void),test_yummy_drive(void);
extern int test_yummy_dist_type(void);
extern void test_yummy_drive_set(float);
extern int test_bro_step(int,int,int),test_bro_playhead(void),test_bro_local_stopped(void);
extern int test_bro_active_slot(void),test_bro_chain_on(void),test_bro_gate(int),test_bro_stream_muted(void),test_bro_resume_pending(void),test_bro_loop_len(void);
extern void test_bro_clear(int);
extern int test_bro_channel(void),test_bro_recording(void),test_bro_chain_editing(void),test_bro_chain_length(void);
extern void test_bro_seed(int,int,int,int);
extern int test_yummy_active(void),test_yummy_wave(void),test_yummy_page(void),test_yummy_playing(void);
extern int test_yummy_held(void),test_yummy_recording(void),test_yummy_poly(void);
extern void test_yummy_poly_set(void);
extern int test_yummy_auto_armed(void),test_yummy_auto_count(void);
extern void test_yummy_note_out(int,int);extern int test_yummy_midi_out(void);
uint32_t panel_keys(void) {return raw_keys;}
uint32_t panel_pressed(void) {uint32_t x=pr;pr=0;return x;}
uint32_t panel_released(void) {uint32_t x=rel;rel=0;return x;}
uint16_t panel_knob(void) {return knob;}
void leds_set(int i,uint8_t n) {assert(i>=0&&i<20);led[i]=n;}
void leds_task(void) {}
void midi_send(uint8_t status,uint8_t d1,uint8_t d2) {
 assert(d1<128&&d2<128);
 if(status==0xBF)last_cc[d1]=d2;
 if((status&0xf0)==0x90 && d2) midi_note_count[status&15]++;
 if((status&0xf0)==0x80 || ((status&0xf0)==0x90 && !d2)) midi_off_count[status&15]++;
}
void audio_note_on(int n,bool a,bool l) {audio_note_count++;seq_voice_muted=true;voice_note_on(&v,n,a,l);}
void audio_chord_on(const uint8_t *n,int count,bool a,voice_mode_t m) {audio_note_count++;seq_voice_muted=true;voice_chord_on(&v,n,count,a,m);}
void audio_note_off(void) {seq_voice_muted=false;voice_note_off(&v);}
static bool audio_muted_test;
void audio_mute(bool on) {audio_muted_test=on;}
void audio_set_params(const voice_params_t *p) {voice_set_params(&v,p);}
void audio_seq(bool run) {if(run)seq_start(&v);else seq_stop(&v);}
void audio_seq_ext_start(void) {seq_start_ext(&v);}
void audio_ext_step(uint32_t n) {seq_ext_step(&v,n);}
void audio_metronome(bool on) {(void)on;}
bool audio_midi_out_pop(uint8_t *n,uint8_t *vel) {(void)n;(void)vel;return false;}
uint32_t audio_underruns(void){return 0;}
uint32_t audio_event_dropped(void){return 0;}
uint32_t audio_midi_dropped(void){return 0;}
uint32_t merged_usb_late_packets(void){return 0;}
uint32_t merged_midi_dropped(void){return 0;}
uint32_t merged_loop_gap_max_us(void){return 1200;}
uint16_t audio_cpu_peak_max(void){return 65;}
uint8_t audio_cpu_avg(void) {return 60;}
uint8_t audio_cpu_peak(void) {return 65;}
void audio_profile_reset(void){profile_resets++;}
bool audio_profile_reset_pending(void){return false;}
bool audio_profile_get(audio_profile_t *p){
 memset(p,0,sizeof(*p));p->generation=profile_resets;p->peak_us=490;
 p->stages[PROFILE_PARAMS]=300;p->stages[PROFILE_RENDER]=190;
 p->warmup_peak_us=490;p->steady_peak_us=263;p->over_budget=1;
 return true;
}
static void tick(void) {
 test_now_us+=1000;merged_task_fast();merged_task_1ms();
 int16_t b[48];seq_tick(&v,48);voice_render(&v,b,48);
 for(int i=0;i<48;i++)energy+=b[i]<0?-b[i]:b[i];
}
static void wait_ms(int n) {for(int i=0;i<n;i++)tick();}
static void key(int k,int down) {
 uint32_t bit=1u<<k;if(down){raw_keys|=bit;pr|=bit;}else{raw_keys&=~bit;rel|=bit;}
 tick();
}
static void tap(int k) {key(k,1);wait_ms(30);key(k,0);wait_ms(10);}
static void system_msg(uint8_t status) {bro_system(status);ui_midi_system(status,0,0);}
static void clocks(int n) {for(int i=0;i<n;i++){system_msg(0xF8);wait_ms(21);}}
static void chord(int a,int b,int c) {
 key(a,1);wait_ms(10);key(b,1);wait_ms(10);key(c,1);wait_ms(10);
 key(c,0);key(b,0);key(a,0);wait_ms(150);
}
int main(void) {
 voice_init(&v);seq_init();ui_init();merged_init();persist_init();wait_ms(20);
 assert(!test_yummy_active());assert(test_bro_channel()==1);
 assert(bro_lead_get()==4&&!test_yummy_midi_out()); // 0.3.6 defaults: StepBro lead 4 ms, Yummy MIDI out off
 // Note audition sends channel 1, and pattern toggle survives press/release routing.
 tap(pad_key(0));assert(midi_note_count[0]>0);
 tap(KEY_PATTERN);tap(pad_key(0));assert(test_bro_step(0,0,0)==100);
 // Hold + turn creates an empty step, does not clear it on release.
 knob=0;key(pad_key(4),1);knob=4095;wait_ms(10);key(pad_key(4),0);
 assert(test_bro_step(0,0,4)==127);knob=0;
 // Both engines start from one host Start and clock stream.
 system_msg(0xFA);clocks(12);assert(test_yummy_playing());
 assert(test_bro_playhead()>0);assert(energy>0);
 int before=midi_note_count[0];
 // All six orders of the level chord must not alter local transport or wave.
 int order[6][3]={{KEY_GLIDE,KEY_WAVE,KEY_PLAY},{KEY_GLIDE,KEY_PLAY,KEY_WAVE},
 {KEY_WAVE,KEY_GLIDE,KEY_PLAY},{KEY_WAVE,KEY_PLAY,KEY_GLIDE},
 {KEY_PLAY,KEY_GLIDE,KEY_WAVE},{KEY_PLAY,KEY_WAVE,KEY_GLIDE}};
 int expected=0,wave=test_yummy_wave();
 for(int i=0;i<6;i++) {
  chord(order[i][0],order[i][1],order[i][2]);expected=!expected;
  assert(test_yummy_active()==expected);assert(!test_bro_local_stopped());
  assert(test_yummy_wave()==wave);clocks(6);
 }
 assert(midi_note_count[0]>before);
 // Switch into synth, audition four-note chord, switch away with keys still held.
 chord(KEY_GLIDE,KEY_WAVE,KEY_PLAY);assert(test_yummy_active());
 tap(KEY_FUNCTION);tap(pad_key(15));assert(test_yummy_poly());tap(KEY_NOTE);int heard=audio_note_count;
 key(pad_key(0),1);key(pad_key(4),1);key(pad_key(7),1);key(pad_key(11),1);
 assert(test_yummy_held()==4&&audio_note_count>heard);
 key(KEY_GLIDE,1);key(KEY_WAVE,1);key(KEY_PLAY,1);
 assert(!test_yummy_active());assert(test_yummy_held()==0);
 // Releasing old pads must not toggle StepBro steps or banks.
 key(KEY_PLAY,0);key(KEY_WAVE,0);key(KEY_GLIDE,0);
 key(pad_key(0),0);key(pad_key(4),0);key(pad_key(7),0);key(pad_key(11),0);wait_ms(150);
 assert(test_bro_step(0,0,0)==100&&test_bro_step(0,0,4)==127);
 assert(!test_bro_local_stopped());
 // Background StepBro and Yummy sequencing continues with synth panel hidden.
 before=midi_note_count[0];clocks(96);assert(midi_note_count[0]>before);assert(test_yummy_playing());
 // A regular Play tap still toggles the local StepBro transport after grace.
 tap(KEY_PLAY);wait_ms(150);assert(test_bro_local_stopped());
 // 0.3.3: the second tap arms the restart; it comes in on the next beat.
 tap(KEY_PLAY);wait_ms(150);assert(test_bro_local_stopped()&&test_bro_resume_pending());
 {int n=0;while(test_bro_local_stopped()&&n<30){clocks(1);n++;}
  assert(!test_bro_local_stopped()&&!test_bro_resume_pending()&&n<=24);
  assert((test_bro_playhead()+test_bro_loop_len()-1)%4==0);}
 system_msg(0xFC);assert(!test_yummy_playing());
 // Chain entry and playback continue to work with only stream A present.
 tap(KEY_PATTERN);wait_ms(450);tap(KEY_PATTERN);tap(KEY_PATTERN);
 assert(test_bro_chain_editing());tap(pad_key(0));tap(pad_key(1));tap(KEY_PATTERN);
 assert(test_bro_chain_on()&&test_bro_chain_length()==2);
 system_msg(0xFA);clocks(102);assert(test_bro_active_slot()==1);
 // 0.3.2: Pattern + knob (was the beat repeat) does nothing: the chain keeps
 // its place. 0.3.4: Mute + knob sets the gate length again, once moved.
 {int slot=test_bro_active_slot(),gate=test_bro_gate(slot),head=test_bro_playhead();
  knob=0;key(KEY_PATTERN,1);knob=4095;wait_ms(10);clocks(6);key(KEY_PATTERN,0);clocks(6);
  assert(test_bro_chain_on()&&test_bro_playhead()!=head);
  knob=2000;key(KEY_ACCENT,1);wait_ms(10);assert(test_bro_gate(slot)==gate);   // pressing alone: no change
  knob=4095;wait_ms(10);assert(test_bro_gate(slot)==100);
  knob=0;wait_ms(10);key(KEY_ACCENT,0);wait_ms(10);assert(test_bro_gate(slot)==5);
  knob=2000;key(KEY_ACCENT,1);knob=3300;wait_ms(10);key(KEY_ACCENT,0);wait_ms(10);knob=0;}
 system_msg(0xFC);
 // Live record arm and a recorded note roll.
 wait_ms(450);tap(KEY_NOTE);tap(KEY_NOTE);assert(test_bro_recording());
 system_msg(0xFA);clocks(6);key(KEY_GLIDE,1);key(pad_key(2),1);clocks(18);
 key(pad_key(2),0);key(KEY_GLIDE,0);
 int recorded=0;for(int k=0;k<32;k++)recorded+=test_bro_step(test_bro_active_slot(),2,k)>0;
 assert(recorded>=2);tap(KEY_PATTERN);assert(!test_bro_recording());
 // Held Distortion edits Drive only, keeps type selection and Mix pickup separate.
 chord(KEY_GLIDE,KEY_WAVE,KEY_PLAY);assert(test_yummy_active());
 tap(KEY_FUNCTION);wait_ms(500);knob=0;wait_ms(10);
 float drive0=test_yummy_drive();
 key(pad_key(7),1);knob=1000;wait_ms(10);
 assert(test_yummy_drive()==drive0); // pickup has not reached 50%
 knob=2100;wait_ms(10);knob=3200;wait_ms(10);
 assert(test_yummy_drive()>0.77f&&test_yummy_drive()<0.79f);
 assert(test_yummy_mix()==0); // turning while held must never write Mix
 tap(pad_key(1));assert(test_yummy_dist_type()==1);
 key(pad_key(7),0);wait_ms(10);
 float drive1=test_yummy_drive();knob=3600;wait_ms(10);
 assert(test_yummy_mix()==0); // release must reacquire Mix
 knob=0;wait_ms(10);knob=1800;wait_ms(10);
 assert(test_yummy_mix()>0.43f&&test_yummy_mix()<0.45f);
 assert(test_yummy_drive()==drive1);
 wait_ms(500);key(pad_key(7),1);tap(pad_key(0));key(pad_key(7),0);
 assert(test_yummy_dist_type()==0&&test_yummy_drive()==drive1);
 key(pad_key(7),1);tap(KEY_NOTE);key(pad_key(7),0);tap(KEY_FUNCTION);
 knob=0;wait_ms(10);knob=4095;wait_ms(10);
 assert(test_yummy_drive()==drive1); // leaving a page clears the held gesture
 knob=1600;wait_ms(10); // return cutoff to midrange for the recording test
 tap(KEY_NOTE);
 chord(KEY_GLIDE,KEY_WAVE,KEY_PLAY);assert(!test_yummy_active());
 // Yummy parameter recording is serviced while visible and resumes in background.
 chord(KEY_GLIDE,KEY_WAVE,KEY_PLAY);assert(test_yummy_active());
 tap(KEY_FUNCTION);knob=1200;tap(pad_key(0));tap(pad_key(0));assert(test_yummy_auto_armed()==0);
 for(int i=0;i<18;i++){knob=(uint16_t)(400+i*180);clocks(1);}
 assert(test_yummy_auto_count()>0);
 chord(KEY_GLIDE,KEY_WAVE,KEY_PLAY);assert(test_yummy_auto_armed()==-1);
 assert(test_yummy_playing());clocks(12);system_msg(0xFC);
 assert(!test_yummy_playing());

 // Stopped-only combined saving and actual engine round-trip.
 wait_ms(5000);assert(persist_successes()>0&&persist_errors()==0);
 uint8_t bro_data[20000],y_data[5000];uint32_t bs=bro_store_size(),ys=ui_store_size();
 assert(bs<sizeof(bro_data)&&ys<sizeof(y_data));bro_store_read(0,bro_data,bs);ui_store_read(0,y_data,ys);
 assert(bro_store_validate(bro_data,bs)&&ui_store_validate(y_data,ys));
 test_bro_seed(0,0,0,1);assert(bro_store_apply(bro_data,bs));
 assert(bro_store_apply(bro_data,bs)&&ui_store_apply(y_data,ys));
 uint8_t check[20000];bro_store_read(0,check,bs);assert(memcmp(check,bro_data,bs)==0);
 ui_store_read(0,check,ys);assert(memcmp(check,y_data,ys)==0);
 // Invalid payload is rejected without changing either engine.
 y_data[0]=0xFF;y_data[1]=0xFF;y_data[2]=0xFF;y_data[3]=0x7F;
 assert(!ui_store_apply(y_data,ys));
 // Restore BOTH real engines through the combined journal, not only adapters.
 float changed=0.123f;memcpy(y_data,&changed,sizeof(changed));assert(ui_store_apply(y_data,ys));
 test_bro_seed(0,0,0,3);persist_init();assert(persist_loaded());
 bro_store_read(0,check,bs);assert(memcmp(check,bro_data,bs)==0);
 // Original serialized Yummy data was invalidated above; export must be valid
 // again after restoration from the previously committed journal bank.
 ui_store_read(0,y_data,ys);assert(ui_store_validate(y_data,ys));
 float restored;memcpy(&restored,y_data,sizeof(restored));assert(restored!=changed);
 // Quick Function + Play retains its modifier through the chord guard.
 chord(KEY_GLIDE,KEY_WAVE,KEY_PLAY);assert(test_yummy_active());
 int diagnostic_before=midi_note_count[15];bool mute_before=seq_mute_request;
 key(KEY_FUNCTION,1);key(KEY_PLAY,1);wait_ms(30);key(KEY_PLAY,0);key(KEY_FUNCTION,0);wait_ms(200);
 assert(midi_note_count[15]>diagnostic_before&&seq_mute_request==mute_before);
 telemetry_send();assert(((last_cc[20]<<7)|last_cc[52])==1536);
 assert(((last_cc[21]<<7)|last_cc[53])==2048);
 assert(((last_cc[31]<<7)|last_cc[63])==65);
 assert(((last_cc[65]<<7)|last_cc[97])==147);
 assert(((last_cc[68]<<7)|last_cc[100])==300);
 assert(((last_cc[76]<<7)|last_cc[108])==490);
 assert(((last_cc[79]<<7)|last_cc[111])==3);
 diagnostic_before=midi_note_count[15];int reset_page=test_yummy_page();
 key(KEY_FUNCTION,1);key(KEY_ACCENT,1);tap(KEY_PLAY);
 key(KEY_ACCENT,0);key(KEY_FUNCTION,0);wait_ms(200);
 assert(profile_resets==1&&seq_mute_request==mute_before);
 assert(test_yummy_page()==reset_page&&midi_note_count[15]==diagnostic_before);
 // Drive uses reserved metadata and leaves the legacy record offsets unchanged.
 assert(ui_store_size()==4280&&bro_store_size()==16456);
 test_yummy_drive_set(0.812345f);persist_activity();wait_ms(5000);
 extern void test_export_current_flash(void);test_export_current_flash();
 uint16_t drive_code=ui_drive_store_code();test_yummy_drive_set(0.1f);
 persist_init();assert(persist_loaded());assert(ui_drive_store_code()==drive_code);
 // Every persisted 16-bit Drive code is stable across decode/encode.
 for(unsigned code=0;code<65536;code++){ui_drive_store_apply((uint16_t)code);assert(ui_drive_store_code()==code);}
 extern void test_import_legacy(void),test_retag_metadata(uint8_t);
 extern const uint8_t *test_legacy_payload(void);
 test_import_legacy();const uint8_t *legacy=test_legacy_payload();
 memcpy(bro_data,legacy,bs);memcpy(y_data,legacy+bs,ys);
 test_yummy_drive_set(0.99f);persist_init();assert(persist_loaded());
 assert(test_yummy_drive()==0.5f); // exact startup default for pre-Drive saves
 bro_store_read(0,check,bs);assert(memcmp(check,bro_data,bs)==0);
 ui_store_read(0,check,ys);
 {uint32_t fo=offsetof(store_data_t,clock_usb);uint8_t lo=(y_data[fo]>>3)&15,ln=(check[fo]>>3)&15;
  // 0.3.5: the old 4 ms lead units become whole ms, marked by flag bit 2.
  assert(!(y_data[fo]&4)&&(check[fo]&4)&&ln==(lo*4>15?15:lo*4));
  assert((check[fo]&~(7|(15<<3)))==(y_data[fo]&~(7|(15<<3))));
  // 0.3.6: a save from before 0.3.6 (bit 0 set) loads with MIDI out off, and
  // is written back with bit 0 clear and bit 1 ("MIDI out off") set.
  assert((y_data[fo]&1)&&!(check[fo]&1)&&(check[fo]&2));y_data[fo]=check[fo];}
 assert(memcmp(check,y_data,ys)==0);
 // First upgraded save persists the new metadata without changing the payload layout.
 wait_ms(5000);assert(persist_successes()>0&&persist_errors()==0);
 test_yummy_drive_set(0.12f);persist_init();assert(persist_loaded());
 assert(ui_drive_store_code()==32768);
 // Invalid extensions are rejected before either engine is changed.
 test_import_legacy();test_retag_metadata(0xE0);test_yummy_drive_set(0.37f);
 persist_init();assert(!persist_loaded()&&persist_errors()>0);
 assert(test_yummy_drive()==0.37f);
 // 0.3.3: locally stopped, StepBro keeps the DAW's place silently; the
 // armed restart lands on a beat of the DAW's bar, in place.
 if(test_yummy_active())chord(KEY_GLIDE,KEY_WAVE,KEY_PLAY);
 {system_msg(0xFC);wait_ms(450);
  if(test_bro_chain_on()){tap(KEY_PATTERN);tap(KEY_PATTERN);assert(test_bro_chain_editing());tap(KEY_PATTERN);wait_ms(450);}
  assert(!test_bro_chain_on()&&!test_bro_chain_editing());
  int slot=test_bro_active_slot();test_bro_clear(slot);
  for(int st=0;st<16;st+=4)test_bro_seed(slot,0,st,110);           // beats, strong
  for(int st=1;st<16;st+=2)test_bro_seed(slot,1,st,40);            // ghost 16ths
  for(int st=2;st<16;st+=4)test_bro_seed(slot,2,st,80);            // off-beat 8ths
  assert(test_bro_loop_len()==16);
  system_msg(0xFA);int ticks=0;
  for(;ticks<6*5;ticks++)clocks(1);
  tap(KEY_PLAY);wait_ms(150);assert(test_bro_local_stopped());
  int heard=midi_note_count[0];
  for(int i=0;i<6*6;i++,ticks++)clocks(1);
  assert(midi_note_count[0]==heard);                              // silent while stopped
  int steps=(ticks-1)/6+1;                                          // steps the DAW has started
  int ph=test_bro_playhead();assert(ph==steps%16||ph==(steps+1)%16);   // +1: sent ahead (MIDI lead)
  tap(KEY_PLAY);wait_ms(150);assert(test_bro_resume_pending());
  while(test_bro_local_stopped()){clocks(1);ticks++;}
  assert((test_bro_playhead()+15)%4==0&&midi_note_count[0]>heard);
  // 0.3.3: density on the knob alone. Full, then the strongest eighth (the
  // beats), then half (beats and off-beat eighths).
  knob=2000;wait_ms(20);knob=4095;wait_ms(20);
  while(test_bro_playhead()!=0){clocks(1);}
  int c0=midi_note_count[0];clocks(96);int full=midi_note_count[0]-c0;
  knob=0;wait_ms(20);while(test_bro_playhead()!=0){clocks(1);}
  c0=midi_note_count[0];clocks(96);int least=midi_note_count[0]-c0;
  knob=2048;wait_ms(20);while(test_bro_playhead()!=0){clocks(1);}
  c0=midi_note_count[0];clocks(96);int half=midi_note_count[0]-c0;
  assert(full==16&&least==4&&half==8);
  // Held pads and modifiers keep the knob for their own jobs.
  knob=4095;wait_ms(20);key(KEY_GLIDE,1);knob=0;wait_ms(20);key(KEY_GLIDE,0);wait_ms(20);
  while(test_bro_playhead()!=0){clocks(1);}
  c0=midi_note_count[0];clocks(96);assert(midi_note_count[0]-c0==16);
  system_msg(0xFC);}
 // 0.3.2: Shift+Mute on the StepBro level: pad 1 StepBro, pad 2 Yummy MIDI,
 // pad 3 Yummy audio. MIDI out is off by default since 0.3.6; switched on here.
 {extern void test_yummy_set_midi_out(int);test_yummy_set_midi_out(1);}
 assert(test_yummy_midi_out());
 if(test_yummy_active())chord(KEY_GLIDE,KEY_WAVE,KEY_PLAY);
 assert(!test_yummy_active()&&!ui_yummy_midi_muted()&&!ui_yummy_audio_muted());
 test_yummy_note_out(50,100);test_yummy_note_out(50,100);test_yummy_note_out(57,90);
 int on2=midi_note_count[1],off2=midi_off_count[1];
 key(KEY_GLIDE,1);key(KEY_ACCENT,1);tap(pad_key(1));key(KEY_ACCENT,0);key(KEY_GLIDE,0);wait_ms(20);
 assert(ui_yummy_midi_muted()&&!ui_yummy_audio_muted()&&!test_bro_stream_muted());
 assert(midi_off_count[1]==off2+3);                // everything sounding was closed
 test_yummy_note_out(60,100);test_yummy_note_out(60,0);test_yummy_note_out(50,0);
 assert(midi_note_count[1]==on2&&midi_off_count[1]==off2+3);   // muted: nothing sent
 key(KEY_GLIDE,1);key(KEY_ACCENT,1);tap(pad_key(2));key(KEY_ACCENT,0);key(KEY_GLIDE,0);wait_ms(20);
 assert(ui_yummy_audio_muted()&&audio_muted_test);
 key(KEY_GLIDE,1);key(KEY_ACCENT,1);tap(pad_key(1));tap(pad_key(2));key(KEY_ACCENT,0);key(KEY_GLIDE,0);wait_ms(20);
 assert(!ui_yummy_midi_muted()&&!ui_yummy_audio_muted()&&!audio_muted_test&&!test_bro_stream_muted());
 test_yummy_note_out(62,100);assert(midi_note_count[1]==on2+1);test_yummy_note_out(62,0);
 // A 0.3.6 save keeps the MIDI-out choice (on here); the same save with bit 0
 // set (as written before 0.3.6) loads with MIDI out off, the new default.
 {uint8_t y[5000];uint32_t n=ui_store_size();ui_store_read(0,y,n);assert(!(y[offsetof(store_data_t,clock_usb)]&1));
  assert(ui_store_apply(y,n)&&test_yummy_midi_out());
  y[offsetof(store_data_t,clock_usb)]|=1;assert(ui_store_apply(y,n)&&!test_yummy_midi_out());
  {extern void test_yummy_set_midi_out(int);test_yummy_set_midi_out(1);}}
 // 0.3.2: the melody follows scale changes, and flipping back restores it.
 {extern void test_yummy_set_scale(int);extern int test_yummy_scale(void),test_yummy_root(void);
  seq_pattern_t keep[SEQ_PATTERNS];memcpy(keep,seq_patterns,sizeof(keep));
  test_yummy_set_scale(1);                          // natural minor
  seq_pattern_t *q=&seq_patterns[3];q->poly=1;q->transpose=0;
  int r=test_yummy_root();
  uint8_t mel[4]={(uint8_t)(48+r),(uint8_t)(51+r),(uint8_t)(55+r),(uint8_t)(58+r)};   // 1, b3, 5, b7
  for(int i=0;i<4;i++){q->steps[i].on=1;seq_step_set(q,&q->steps[i],&mel[i],1);}
  uint8_t ch[3]={(uint8_t)(48+r),(uint8_t)(51+r),(uint8_t)(55+r)};q->steps[4].on=1;seq_step_set(q,&q->steps[4],ch,3);
  q->steps[5].on=0;q->steps[5].pitch=7;
  seq_pattern_t written=*q;
  test_yummy_set_scale(2);                          // major: same degrees
  uint8_t got[4];
  int want[4]={48,52,55,59};
  for(int i=0;i<4;i++){assert(seq_step_get(q,&q->steps[i],got)==1&&got[0]==want[i]+r);}
  assert(seq_step_get(q,&q->steps[4],got)==3&&got[0]==48+r&&got[1]==52+r&&got[2]==55+r);
  assert(q->steps[5].pitch==7);                     // rests untouched
  test_yummy_set_scale(3);                          // minor pentatonic (5 notes)
  for(int i=0;i<5;i++){int n=seq_step_get(q,&q->steps[i],got);for(int k=0;k<n;k++)assert(seq_in_scale(got[k],3,r));}
  test_yummy_set_scale(0);test_yummy_set_scale(5);test_yummy_set_scale(1);   // via chromatic and blues, back to minor
  assert(memcmp(q,&written,sizeof(written))==0);   // exactly as written
  // An edit makes the current notes the new original.
  test_yummy_set_scale(2);uint8_t e=(uint8_t)(50+r);seq_step_set(q,&q->steps[0],&e,1);
  test_yummy_set_scale(1);test_yummy_set_scale(2);
  assert(seq_step_get(q,&q->steps[0],got)==1&&got[0]==50+r);
  memcpy(seq_patterns,keep,sizeof(keep));}
 // 0.3.4: Yummy Function page: 13 = MIDI channel, 14 = MIDI out, 15 = MIDI
 // lead (tap, then turn); no internal clock: Play mutes / unmutes, also stopped.
 {extern int test_yummy_channel(void),test_yummy_lead(void);
  if(!test_yummy_active())chord(KEY_GLIDE,KEY_WAVE,KEY_PLAY);
  assert(test_yummy_active());system_msg(0xFC);
  tap(KEY_NOTE);tap(KEY_FUNCTION);assert(test_yummy_page()==2);
  knob=2000;tap(pad_key(12));assert(test_yummy_channel()==1);   // selecting alone: no change
  knob=4*256+128;wait_ms(20);assert(test_yummy_channel()==4);
  knob=1*256+128;wait_ms(20);assert(test_yummy_channel()==1);
  tap(pad_key(13));assert(!test_yummy_midi_out());tap(pad_key(13));assert(test_yummy_midi_out());
  knob=2000;tap(pad_key(14));assert(test_yummy_lead()==8);   // 0.3.5: 8 ms = 8 steps of 1 ms
  knob=3*256+100;wait_ms(20);assert(test_yummy_lead()==3);
  knob=2*256+100;wait_ms(20);assert(test_yummy_lead()==2);
  tap(KEY_FUNCTION);
  bool m=seq_mute_request;tap(KEY_PLAY);wait_ms(200);assert(seq_mute_request!=m&&!test_yummy_playing());
  tap(KEY_PLAY);wait_ms(200);assert(seq_mute_request==m);
  system_msg(0xFA);clocks(12);assert(test_yummy_playing());system_msg(0xFC);
  chord(KEY_GLIDE,KEY_WAVE,KEY_PLAY);assert(!test_yummy_active());}
 // 0.3.5: save blockers for diagnostics, all-notes-off, lead conversion.
 {extern int test_yummy_lead(void);system_msg(0xFC);wait_ms(50);
  assert((save_blockers()&(2|8|16))==0);
  ui_midi_in(0x90,1,60,100);assert(ui_save_blockers()&4);assert(save_blockers()&32);
  ui_midi_in(0xB0,1,123,0);assert(!(ui_save_blockers()&4));
  key(KEY_NOTE,1);wait_ms(30);assert(save_blockers()&128);key(KEY_NOTE,0);wait_ms(200);
  wait_ms(3000);assert(persist_quiet_ms()>=2000);
  static uint8_t yd[5000];uint32_t ys=ui_store_size();ui_store_read(0,yd,ys);
  uint32_t fo=offsetof(store_data_t,clock_usb);
  assert(yd[fo]&4);                                    // new saves: lead in whole ms
  yd[fo]=(uint8_t)((yd[fo]&~(4|(15<<3)))|(2<<3));       // an older save: 2 x 4 ms
  assert(ui_store_apply(yd,ys));assert(test_yummy_lead()==8);
  yd[fo]=(uint8_t)((yd[fo]&~(15<<3))|(3<<3));           // older, 3 x 4 ms = 12 ms
  assert(ui_store_apply(yd,ys));assert(test_yummy_lead()==12);
  yd[fo]=(uint8_t)((yd[fo]&~(15<<3))|(5<<3)|4);         // new: 5 ms
  assert(ui_store_apply(yd,ys));assert(test_yummy_lead()==5);
 }
 // 1.2.0: LEDs and playheads.
 {extern int test_bro_last_step(void),test_bro_selected_pad(void),test_bro_grid_note(void);
  extern int test_yummy_head(void),test_yummy_oct(void),test_yummy_fn_sel(void);
  extern void test_yummy_set_midi_out(int);
  system_msg(0xFC);wait_ms(50);
  if(test_yummy_active())chord(KEY_GLIDE,KEY_WAVE,KEY_PLAY);
  assert(!test_yummy_active());
  if(!test_bro_grid_note()){tap(KEY_NOTE);wait_ms(450);}
  assert(test_bro_grid_note()&&!test_bro_recording());
  // StepBro, not armed: no playhead on the dark pads.
  system_msg(0xFA);
  for(int i=0;i<40;i++){clocks(1);int ph=test_bro_last_step()%16;
   if(ph!=test_bro_selected_pad())assert(led[ph]==0);}
  // Armed: the playhead runs, brighter on pad 1.
  wait_ms(450);tap(KEY_NOTE);tap(KEY_NOTE);assert(test_bro_recording());
  {int seen0=0,seen=0;
   for(int i=0;i<6*20;i++){clocks(1);int ph=test_bro_last_step()%16;
    // LEDs refresh on their own schedule: the lit pad is this step or the one before.
    int lit=-1,n=0;for(int k=0;k<16;k++)if(k!=test_bro_selected_pad()&&led[k]){lit=k;n++;}
    if(n==0)continue;                                  // the step is on the selected pad
    assert(n==1&&(lit==ph||lit==(ph+15)%16));assert(led[lit]==(lit==0?90:30));
    if(lit==0)seen0=1;else seen=1;}
   assert(seen0&&seen);}
  tap(KEY_NOTE);assert(!test_bro_recording());system_msg(0xFC);wait_ms(50);
  // Yummy Note page: Scale and Oct dark at rest; Scale lit while held,
  // Oct - / + lit only when below / above octave 0.
  chord(KEY_GLIDE,KEY_WAVE,KEY_PLAY);assert(test_yummy_active());
  tap(KEY_NOTE);wait_ms(450);assert(test_yummy_page()==0&&test_yummy_oct()==0);
  wait_ms(20);assert(led[13]==0&&led[14]==0&&led[15]==0);
  key(pad_key(13),1);wait_ms(20);assert(led[13]==140);key(pad_key(13),0);wait_ms(20);assert(led[13]==0);
  tap(pad_key(14));wait_ms(20);assert(test_yummy_oct()==-1&&led[14]==140&&led[15]==0);
  tap(pad_key(15));tap(pad_key(15));wait_ms(20);assert(test_yummy_oct()==1&&led[14]==0&&led[15]==140);
  tap(pad_key(14));wait_ms(20);assert(test_yummy_oct()==0&&led[14]==0&&led[15]==0);
  // Recording armed and playing: the playhead runs, brighter on step 1.
  // (mono, one note selected away from pad 1, so pad 1 is free to show it)
  tap(KEY_FUNCTION);wait_ms(20);if(test_yummy_poly())tap(pad_key(15));
  tap(KEY_NOTE);wait_ms(450);tap(pad_key(5));wait_ms(20);
  wait_ms(450);tap(KEY_NOTE);tap(KEY_NOTE);assert(test_yummy_recording());
  system_msg(0xFA);
  {int seen0=0,seen=0;
   for(int i=0;i<6*20;i++){clocks(1);wait_ms(2);int h=test_yummy_head();
    int lit=-1,n=0;for(int k=0;k<16;k++)if(led[k]==30||led[k]==70){lit=k;n++;}
    if(h<0||n==0)continue;                             // on a selected note
    assert(n==1&&(lit==h||lit==(h+15)%16));assert(led[lit]==(lit==0?70:30));
    if(lit==0)seen0=1;else seen=1;}
   assert(seen0&&seen);}
  tap(KEY_NOTE);assert(!test_yummy_recording());system_msg(0xFC);wait_ms(50);
  // Function page: MIDI out and Mono/Poly dark when off / mono, bright when on / poly.
  tap(KEY_FUNCTION);wait_ms(20);assert(test_yummy_page()==2);
  assert(test_yummy_fn_sel()!=13&&test_yummy_fn_sel()!=15);
  test_yummy_set_midi_out(0);int poly=test_yummy_poly();if(poly){tap(pad_key(15));}
  wait_ms(20);assert(!test_yummy_poly()&&led[13]==0&&led[15]==0);
  tap(pad_key(13));wait_ms(20);assert(test_yummy_midi_out()&&led[13]==140);
  tap(pad_key(15));wait_ms(20);assert(test_yummy_poly()&&led[15]==140);
  tap(pad_key(15));tap(pad_key(13));wait_ms(20);assert(!test_yummy_poly()&&!test_yummy_midi_out()&&led[13]==0&&led[15]==0);
  tap(KEY_NOTE);chord(KEY_GLIDE,KEY_WAVE,KEY_PLAY);assert(!test_yummy_active());}
 // 1.2.0: Note mode: the knob alone is the selected sound's velocity, with
 // pickup, scaling its programmed steps; density only in Pattern mode. The
 // knob during a roll rides the repeats' velocity, also when recorded.
 {extern int test_bro_voice_vel(int),test_bro_density(void),test_bro_swing(void),test_bro_grid_note(void);
  system_msg(0xFC);wait_ms(50);
  if(test_yummy_active())chord(KEY_GLIDE,KEY_WAVE,KEY_PLAY);
  if(test_bro_chain_on()){tap(KEY_PATTERN);wait_ms(450);tap(KEY_PATTERN);tap(KEY_PATTERN);tap(KEY_PATTERN);wait_ms(450);}
  if(!test_bro_grid_note()){tap(KEY_NOTE);wait_ms(450);}
  assert(test_bro_grid_note());
  int slot=test_bro_active_slot();test_bro_clear(slot);
  test_bro_seed(slot,3,0,100);test_bro_seed(slot,3,4,50);test_bro_seed(slot,3,8,127);
  knob=400;wait_ms(20);tap(pad_key(3));wait_ms(20);
  assert(test_bro_voice_vel(3)==100);int dens=test_bro_density();
  knob=2000;wait_ms(20);assert(test_bro_voice_vel(3)==100);        // below 100: not picked up yet
  assert(test_bro_step(slot,3,4)==50&&test_bro_density()==dens);   // and density untouched
  knob=3400;wait_ms(20);int v=test_bro_voice_vel(3);assert(v>100&&v<=110);   // crossed: picked up
  knob=2048;wait_ms(20);v=test_bro_voice_vel(3);assert(v>=60&&v<=68);
  assert(test_bro_step(slot,3,0)==v);                              // scaled from the snapshot
  assert(test_bro_step(slot,3,4)==(50*v+50)/100);
  assert(test_bro_step(slot,3,8)==(127*v+50)/100);
  assert(test_bro_density()==dens);
  knob=0;wait_ms(20);assert(test_bro_voice_vel(3)==1&&test_bro_step(slot,3,4)==1);
  for(knob=0;test_bro_voice_vel(3)<100;knob+=4)wait_ms(2);          // back up to 100: restored exactly
  assert(test_bro_voice_vel(3)==100);
  assert(test_bro_step(slot,3,0)==100&&test_bro_step(slot,3,4)==50&&test_bro_step(slot,3,8)==127);
  // Another sound picks up afresh: a knob far from its velocity changes nothing.
  knob=200;wait_ms(20);tap(pad_key(5));wait_ms(20);knob=300;wait_ms(20);assert(test_bro_voice_vel(5)==100);
  // Pattern mode: the knob alone is still the density, and velocity is left alone.
  tap(KEY_PATTERN);wait_ms(450);assert(!test_bro_grid_note());
  knob=1000;wait_ms(20);knob=3000;wait_ms(20);assert(test_bro_density()!=dens);
  assert(test_bro_voice_vel(5)==100);
  knob=4095;wait_ms(20);tap(KEY_NOTE);wait_ms(450);assert(test_bro_grid_note());
  // Roll: the knob rides the velocity, no jump, both ends reachable.
  int swing=test_bro_swing();
  knob=2048;wait_ms(20);
  int v0=test_bro_voice_vel(3);
  key(KEY_GLIDE,1);key(pad_key(3),1);wait_ms(20);
  assert(test_bro_voice_vel(3)==v0&&test_bro_swing()==swing);      // pressing: no jump
  knob=4095;wait_ms(20);assert(test_bro_voice_vel(3)==127);
  knob=0;wait_ms(20);assert(test_bro_voice_vel(3)==1);
  knob=2048;wait_ms(20);v=test_bro_voice_vel(3);assert(v>=v0-2&&v<=v0+2);
  assert(test_bro_swing()==swing);                                 // never swing during a roll
  knob=0;wait_ms(20);key(pad_key(3),0);wait_ms(20);assert(test_bro_swing()==swing);   // Shift still held: no jump
  key(KEY_GLIDE,0);wait_ms(20);
  // Recorded roll: the hits carry the velocity ridden on the knob.
  test_bro_clear(slot);knob=2048;wait_ms(20);
  wait_ms(450);tap(KEY_NOTE);tap(KEY_NOTE);assert(test_bro_recording());
  system_msg(0xFA);clocks(6);
  key(KEY_GLIDE,1);key(pad_key(3),1);clocks(6*4);
  knob=4095;wait_ms(20);clocks(6*4);knob=0;wait_ms(20);clocks(6*4);
  key(pad_key(3),0);key(KEY_GLIDE,0);
  {int hi=0,lo=0;for(int k=0;k<32;k++){int x=test_bro_step(slot,3,k);if(x>=120)hi++;if(x&&x<=10)lo++;}
   assert(hi>=2&&lo>=2);}
  tap(KEY_NOTE);assert(!test_bro_recording());system_msg(0xFC);wait_ms(50);
  test_bro_clear(slot);knob=0;wait_ms(20);}
 // 1.2.0: Yummy chain entry as on StepBro: double-tap Pattern, tap slots,
 // finish with Pattern, Note or Function. Holding Pattern picks one slot.
 {extern int test_yummy_chain_len(void),test_yummy_chain_at(int),test_yummy_edit_slot(void),test_yummy_chain_entry(void);
  system_msg(0xFC);wait_ms(50);
  if(!test_yummy_active())chord(KEY_GLIDE,KEY_WAVE,KEY_PLAY);
  assert(test_yummy_active());
  tap(KEY_PATTERN);wait_ms(450);assert(test_yummy_page()==1&&!test_yummy_chain_entry());
  tap(KEY_PATTERN);tap(KEY_PATTERN);assert(test_yummy_chain_entry());
  wait_ms(20);{int a=led[17];wait_ms(260);assert(led[17]!=a);}     // Pattern blinks
  tap(pad_key(2));tap(pad_key(5));tap(pad_key(2));
  tap(KEY_PATTERN);assert(!test_yummy_chain_entry());
  assert(test_yummy_chain_len()==3&&test_yummy_chain_at(0)==2&&test_yummy_chain_at(1)==5&&test_yummy_chain_at(2)==2);
  assert(test_yummy_edit_slot()==2);
  // Finishing with Note, and one slot: plays on its own, the chain ends.
  wait_ms(450);tap(KEY_PATTERN);tap(KEY_PATTERN);assert(test_yummy_chain_entry());
  tap(pad_key(7));tap(KEY_NOTE);assert(!test_yummy_chain_entry()&&test_yummy_page()==0);
  assert(test_yummy_chain_len()==0&&test_yummy_edit_slot()==7);
  // Finishing with Function.
  tap(KEY_PATTERN);wait_ms(450);tap(KEY_PATTERN);tap(KEY_PATTERN);assert(test_yummy_chain_entry());
  tap(pad_key(1));tap(pad_key(3));tap(KEY_FUNCTION);assert(!test_yummy_chain_entry()&&test_yummy_page()==2);
  assert(test_yummy_chain_len()==2&&test_yummy_chain_at(0)==1&&test_yummy_chain_at(1)==3);
  // No slots tapped: nothing changes.
  tap(KEY_PATTERN);wait_ms(450);tap(KEY_PATTERN);tap(KEY_PATTERN);assert(test_yummy_chain_entry());
  tap(KEY_PATTERN);assert(!test_yummy_chain_entry()&&test_yummy_chain_len()==2);
  // Holding Pattern and tapping slots picks one (the last), and ends the chain.
  wait_ms(450);key(KEY_PATTERN,1);wait_ms(30);tap(pad_key(4));tap(pad_key(6));key(KEY_PATTERN,0);wait_ms(20);
  assert(!test_yummy_chain_entry()&&test_yummy_chain_len()==0&&test_yummy_edit_slot()==6);
  wait_ms(450);key(KEY_PATTERN,1);wait_ms(30);tap(pad_key(0));key(KEY_PATTERN,0);wait_ms(450);
  assert(test_yummy_edit_slot()==0);
  tap(KEY_NOTE);chord(KEY_GLIDE,KEY_WAVE,KEY_PLAY);assert(!test_yummy_active());}
 // 1.2.0: holding a step and turning reaches C1..C4 (the Note page range)
 // wherever the knob sits when the step is grabbed. Before: C1 from a knob
 // at three-quarters reached only five notes up.
 {extern int test_yummy_step_pitch(int),test_yummy_edit_slot(void),test_yummy_root(void);
  extern void test_yummy_set_scale(int);
  system_msg(0xFC);wait_ms(50);if(!test_yummy_active())chord(KEY_GLIDE,KEY_WAVE,KEY_PLAY);
  tap(KEY_FUNCTION);wait_ms(20);if(test_yummy_poly())tap(pad_key(15));
  test_yummy_set_scale(0);tap(KEY_PATTERN);wait_ms(450);
  seq_pattern_t keep=seq_patterns[test_yummy_edit_slot()];
  #define PUT(st,n) do{seq_pattern_t *q=&seq_patterns[test_yummy_edit_slot()];uint8_t x=(uint8_t)(n);q->steps[st].on=1;q->steps[st].tie=0;seq_step_set(q,&q->steps[st],&x,1);}while(0)
  #define TURN(a,b) do{int a_=(a),b_=(b),d_=b_>a_?16:-16;for(int k_=a_;(d_>0)?k_<b_:k_>b_;k_+=d_){knob=(uint16_t)k_;wait_ms(1);}knob=(uint16_t)b_;wait_ms(5);}while(0)
  int starts[5]={0,1024,2048,3300,4000};
  for(int i=0;i<5;i++){
   PUT(3,24);knob=starts[i];wait_ms(20);key(pad_key(3),1);wait_ms(10);
   TURN(starts[i],4095);assert(test_yummy_step_pitch(3)==60);         // C1 -> C4 from anywhere
   key(pad_key(3),0);wait_ms(20);}
  PUT(3,48);knob=2048;wait_ms(20);key(pad_key(3),1);wait_ms(10);
  TURN(2048,0);assert(test_yummy_step_pitch(3)==24);                  // down to C1
  TURN(0,4095);assert(test_yummy_step_pitch(3)==60);                  // up to C4
  TURN(4095,2048);assert(test_yummy_step_pitch(3)==48);               // back where it started
  key(pad_key(3),0);wait_ms(20);
  // In a scale: the ends are the lowest / highest scale notes in C1..C4.
  test_yummy_set_scale(1);
  {int r=test_yummy_root(),lo=24,hi=60;while(!seq_in_scale(lo,1,r))lo++;while(!seq_in_scale(hi,1,r))hi--;
   PUT(5,seq_scale_snap(40,1,r));knob=3000;wait_ms(20);key(pad_key(5),1);wait_ms(10);
   TURN(3000,4095);assert(test_yummy_step_pitch(5)==hi);
   TURN(4095,0);assert(test_yummy_step_pitch(5)==lo);
   key(pad_key(5),0);wait_ms(20);}
  test_yummy_set_scale(0);
  // A note above C4 (written before 1.2.0) does not go further up.
  PUT(6,66);knob=2048;wait_ms(20);key(pad_key(6),1);wait_ms(10);
  TURN(2048,4095);assert(test_yummy_step_pitch(6)==66);
  TURN(4095,0);assert(test_yummy_step_pitch(6)==24);
  key(pad_key(6),0);wait_ms(20);
  #undef PUT
  #undef TURN
  seq_patterns[test_yummy_edit_slot()]=keep;
  tap(KEY_NOTE);chord(KEY_GLIDE,KEY_WAVE,KEY_PLAY);assert(!test_yummy_active());}
 // 1.2.0: StepBro loop window in Pattern mode.
 {extern int test_bro_last_step(void),test_bro_grid_note(void),test_bro_selected_pad(void);
  system_msg(0xFC);wait_ms(50);
  if(test_yummy_active())chord(KEY_GLIDE,KEY_WAVE,KEY_PLAY);
  if(test_bro_grid_note()){tap(KEY_PATTERN);wait_ms(450);}
  int vo=test_bro_selected_pad();assert(vo>=0&&vo<16);
  assert(!test_bro_grid_note()&&!test_bro_chain_on());
  int slot=test_bro_active_slot();test_bro_clear(slot);
  for(int st=0;st<16;st++)test_bro_seed(slot,vo,st,20+st);
  assert(test_bro_loop_len()==16);
  #define STEP() (clocks(6),test_bro_last_step())
  #define ONTIME(x) (((test_bro_playhead()-(x)+32)%16)==1||((test_bro_playhead()-(x)+32)%16)==2)
  system_msg(0xFA);for(int i=0;i<5;i++)STEP();
  // One step held: a stutter on that step while it is held.
  key(KEY_GLIDE,1);key(pad_key(4),1);STEP();
  for(int i=0;i<6;i++)assert(STEP()==4);
  key(pad_key(4),0);STEP();
  for(int i=0;i<4;i++){int x=STEP();assert(ONTIME(x));}            // back on the timeline
  // A held step and a second: the range loops, also after letting go of both.
  key(pad_key(2),1);key(pad_key(5),1);STEP();
  {int seen=0;for(int i=0;i<4;i++){int x=STEP();assert(x>=2&&x<=5);seen|=1<<x;}assert(seen==0x3C);}
  key(pad_key(5),0);key(pad_key(2),0);
  {int seen=0;for(int i=0;i<8;i++){int x=STEP();assert(x>=2&&x<=5);seen|=1<<x;}assert(seen==0x3C);}
  key(KEY_GLIDE,0);STEP();
  for(int i=0;i<4;i++){int x=STEP();assert(ONTIME(x));}            // Shift let go: the full pattern again
  // The loop pads never toggled their steps.
  for(int st=0;st<16;st++)assert(test_bro_step(slot,vo,st)==20+st);
  // Without Shift, a tap still toggles.
  tap(pad_key(7));assert(test_bro_step(slot,vo,7)==0);tap(pad_key(7));assert(test_bro_step(slot,vo,7)!=0);
  #undef STEP
  #undef ONTIME
  system_msg(0xFC);wait_ms(50);test_bro_clear(slot);}
 printf("PASS integration: StepBro note/step/velocity, both transports, six chord orders, four-note audition, hold cleanup, background playback, local Play, chains, Pattern + knob inert, Mute + knob gate length, beat-quantised local restart in DAW time, knob density, recorded rolls, Yummy automation, Drive/Mix pickup, actual v0.2.3 save import, metadata upgrade and Drive retention, Yummy MIDI/audio mutes from Shift+Mute, MIDI out off by default (older saves too), scale-following melodies, Yummy MIDI channel / out / lead pads, USB-only clock, 1 ms Yummy lead with old-save conversion, save-blocker diagnostics, all-notes-off, 1.2.0 LEDs and record playheads, Note-mode velocity knob with pickup and step scaling, roll velocity on the knob (also recorded), Yummy chain entry by double-tapping Pattern, step note edits reach C1..C4 from any knob position, StepBro loop window.\n");
 return 0;
}
