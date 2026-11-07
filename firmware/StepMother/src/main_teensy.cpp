#include <Arduino.h>
#include "usb_audio.h"
extern "C" {
#include "panel.h"
#include "audio.h"
#include "ui.h"
#include "persist.h"
#include "telemetry.h"
#include "merged.h"
#include "firmware_version.h"
bool sm_storage_init(void);
bool sm_storage_available(void);
void sm_storage_report_error(void);
int sm_usb_midi_try_write(uint32_t packet);
}
static volatile bool ready;
static volatile uint32_t last_audio_us,late_packets;
static uint32_t midi_drops;
static uint8_t midi_fifo[256][3];static uint32_t midi_wr,midi_rd;
extern "C" uint32_t sm_millis(void){return millis();}
extern "C" uint32_t sm_micros(void){return micros();}
extern "C" uint64_t sm_micros64(void){static uint32_t prev,high;uint32_t now=micros();if(now<prev)high++;prev=now;return ((uint64_t)high<<32)|now;}
extern "C" uint32_t merged_usb_late_packets(void){return late_packets;}
extern "C" uint32_t merged_midi_dropped(void){return midi_drops;}
// Longest time between two passes of loop(), for the Function+Play report:
// anything over a few ms means the loop stalled (synth audio runs dry).
static uint32_t loop_last_us,loop_gap_max_us;
extern "C" uint32_t merged_loop_gap_max_us(void){return loop_gap_max_us;}
extern "C" void midi_send(uint8_t status,uint8_t d1,uint8_t d2){
 if(!usb_configuration)return;
 if(midi_wr-midi_rd>=256){midi_drops++;return;}
 uint8_t *p=midi_fifo[midi_wr++%256];p[0]=status;p[1]=d1&127;p[2]=d2&127;
}
static void midi_flush(void){
 if(!usb_configuration){midi_rd=midi_wr;return;}
 // Native packet API has bounded timeout; do not repeatedly block on a full pipe.
 unsigned count=0;
 while(midi_rd!=midi_wr&&count++<16){
  uint8_t *p=midi_fifo[midi_rd%256];
  uint32_t packet=(p[0]>>4)|((uint32_t)p[0]<<8)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<24);
  if(!sm_usb_midi_try_write(packet))break;
  midi_rd++;
 }
 // No usb_midi_flush_output() here. A part-filled packet is sent by the USB
 // start-of-frame interrupt within 125 us (sm_usb_midi_try_write turns it on).
 // Flushing from this loop as well raced that interrupt: both could queue the
 // same transfer, sending a packet twice or leaving the MIDI-out pipe stuck.
}
static void midi_read(void){
 unsigned count=0;uint32_t packet;
 while(count++<128&&(packet=usb_midi_read_message())!=0){
  uint8_t status=(uint8_t)(packet>>8),d1=(uint8_t)(packet>>16)&127,d2=(uint8_t)(packet>>24)&127;
  if(status>=0xF0){
   if(status==0xFA||status==0xFB||status==0xFC||status==0xF2)persist_activity();
   bro_system(status);ui_midi_system(status,d1,d2);
  }else if(status>=0x80){persist_activity();ui_midi_in(status&0xF0,status&15,d1,d2);}
 }
}
// Called only by the native USB TX interrupt. The main loop owns DSP state;
// this callback only consumes a prepared ring buffer and returns dual mono.
extern "C" void stepmother_audio_usb_tx(int16_t *dst,unsigned frames){
 if(!ready){memset(dst,0,frames*4);return;}
 if(!usb_audio_transmit_setting){memset(dst,0,frames*4);last_audio_us=0;return;}
 uint32_t now=micros(),previous=last_audio_us;last_audio_us=now;
 if(previous && now-previous>1500)late_packets++;
 if(!previous)audio_reset_stream();
 audio_read_stereo(dst,(int)frames);
}
void setup(){
 telemetry_stack_init(0);panel_init();
 (void)*(volatile const char *)firmware_tag;   // keep the version tag in the image
 // Hardware program button remains the recovery path for a failed test image.
 if((panel_scan_raw()&((1u<<KEY_GLIDE)|(1u<<KEY_FUNCTION)))==((1u<<KEY_GLIDE)|(1u<<KEY_FUNCTION))){
  delay(50);if((panel_scan_raw()&((1u<<KEY_GLIDE)|(1u<<KEY_FUNCTION)))==((1u<<KEY_GLIDE)|(1u<<KEY_FUNCTION)))_reboot_Teensyduino_();
 }
 leds_init();audio_start_core1();ui_init();merged_init();
 sm_storage_init();persist_init();sm_storage_report_error();
 audio_service();ready=true;
}
void loop(){
 {uint32_t t=micros();if(loop_last_us&&t-loop_last_us>loop_gap_max_us)loop_gap_max_us=t-loop_last_us;loop_last_us=t;}
 audio_service();midi_read();ui_midi_flush();merged_task_fast();midi_flush();audio_service();
 static uint32_t next;
 uint32_t now=millis();
 if((int32_t)(now-next)>=0){
  next=now+1;panel_task();
  ui_status(usb_configuration!=0,usb_audio_transmit_setting!=0,false,audio_underruns()+late_packets);
  merged_task_1ms();
 }
 audio_service();
}
