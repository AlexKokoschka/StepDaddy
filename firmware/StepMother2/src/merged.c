#include "merged.h"
#include "persist.h"
#include "panel.h"
#include "ui.h"
#include "pico/stdlib.h"
#include <stdlib.h>

static bool yummy_visible,wait_release;
static uint32_t physical,visible_keys,y_pressed,y_released;
static uint32_t prefix_since;
static bool prefix_wait;
#define SHIFT_BIT (1u<<KEY_GLIDE)
#define BANK_BIT (1u<<KEY_WAVE)
#define PLAY_BIT (1u<<KEY_PLAY)
#define LEVEL_CHORD (SHIFT_BIT|BANK_BIT|PLAY_BIT)
#define CHORD_GRACE_MS 120
#define INPUT_CAP 128
struct edge { uint8_t key; bool down; };
static struct edge input[INPUT_CAP];
static uint32_t iw,ir;
static const int8_t bro_keymap[25]={0,1,2,3,-1,10,11,12,13,-1,14,15,16,17,30,18,19,20,21,31,22,23,24,25,32};

bool merged_panel_ready(void) {return !wait_release && !(prefix_wait && merged_millis()-prefix_since<CHORD_GRACE_MS);}
bool merged_bro_visible(void) {return !yummy_visible && merged_panel_ready();}
uint16_t merged_knob(void) {return panel_knob();}
void merged_led(int led,uint8_t level) {leds_set(led,level);}
void merged_midi_send(uint8_t status,uint8_t d1,uint8_t d2) {midi_send(status,d1,d2);}
uint32_t merged_millis(void) {return to_ms_since_boot(get_absolute_time());}
uint32_t merged_micros(void) {return time_us_32();}
uint32_t merged_yummy_keys(void) {return yummy_visible?visible_keys:0;}
uint32_t merged_yummy_pressed(void) {uint32_t p=y_pressed;y_pressed=0;return p;}
uint32_t merged_yummy_released(void) {uint32_t p=y_released;y_released=0;return p;}

void merged_init(void) {
  yummy_visible=wait_release=prefix_wait=false;
  physical=visible_keys=y_pressed=y_released=iw=ir=0;
  bro_init();
  ui_set_active(false);
  bro_show();
}
static void cancel_panel(void) {
  // Cancelling both does not stop either sequencer.
  ui_set_active(false);bro_suspend_panel();
  y_pressed=y_released=visible_keys=0;
  ir=iw;prefix_wait=false;
}
static void switch_panel(void) {
  cancel_panel();yummy_visible=!yummy_visible;wait_release=true;
  for(int i=0;i<N_LEDS;i++) leds_set(i,0);
}
static void enqueue(uint32_t bits,bool down) {
  for(int k=0;k<25;k++) if(bits&(1u<<k)) {
    if(bro_keymap[k]<0) continue;
    if(iw-ir>=INPUT_CAP) {cancel_panel();wait_release=true;return;}
    input[iw%INPUT_CAP]=(struct edge){(uint8_t)k,down};iw++;
  }
}
void merged_task_1ms(void) {
  uint32_t pressed=panel_pressed(),released=panel_released();
  physical=panel_keys();
  static uint16_t stored_knob;
  uint16_t new_knob=panel_knob();
  if(pressed||released||abs((int)new_knob-(int)stored_knob)>24){persist_activity();stored_knob=new_knob;}
  if(wait_release) {
    // Gesture is consumed until every held key is released, on either level.
    if(!physical) {
      wait_release=false;
      ui_set_active(yummy_visible);
      if(!yummy_visible) bro_show();
    }
  } else if((physical&LEVEL_CHORD)==LEVEL_CHORD) {
    switch_panel();
  } else {
    enqueue(pressed,true);enqueue(released,false);
    bool prefix=(physical&(BANK_BIT|PLAY_BIT))!=0;
    if(prefix && !prefix_wait) {prefix_wait=true;prefix_since=merged_millis();}
    bool delay=prefix_wait && merged_millis()-prefix_since<CHORD_GRACE_MS;
    if(!delay) {
      uint32_t seen=0;
      while(ir!=iw) {
        struct edge e=input[ir%INPUT_CAP];uint32_t b=1u<<e.key;
        // Keep separate taps of the same key on separate UI ticks.
        if(seen&b) break;
        seen|=b;ir++;
        if(e.down)visible_keys|=b;else visible_keys&=~b;
        if(yummy_visible) {if(e.down)y_pressed|=b;else y_released|=b;}
        else bro_key(bro_keymap[e.key],e.down);
        break; // One edge per UI tick preserves modifier ordering after grace.
      }
      if(!prefix && ir==iw) prefix_wait=false;
    }
  }
  ui_task_1ms(); // audio automation and Yummy transport continue when hidden
  if(yummy_visible && !wait_release) {
    // One slow Wave/Bank blink distinguishes the synth editing level.
    leds_set(LED_WAVE,(merged_millis()/500)%2?220:0);
  }
  persist_task();
  // Stopped-only save progress: Play blinks; three-bank errors remain visible
  // through telemetry. Engine transport displays resume immediately on Start.
  if(persist_busy())leds_set(LED_PLAY,(merged_millis()/100)%2?220:0);
  leds_task();
}
void merged_task_fast(void) {bro_task();}
