#include "telemetry.h"
#include "persist.h"
#include "audio.h"
#include "ui.h"
#include <stddef.h>
#include <stdbool.h>
#define PAINT 0xA55AA55Au
static volatile bool ready[2];
#ifndef MERGED_HOST_TEST
extern uint32_t __StackBottom[],__StackTop[],__StackOneBottom[],__StackOneTop[],__end__[],__HeapLimit[];
static uint32_t *bottom(unsigned core){return core?__StackOneBottom:__StackBottom;}
static uint32_t *top(unsigned core){return core?__StackOneTop:__StackTop;}
void telemetry_stack_init(unsigned core){
 uintptr_t sp;__asm volatile("mov %0, sp":"=r"(sp));
 // The active frame and an additional 128-byte margin are never painted.
 for(uint32_t *p=bottom(core);(uintptr_t)(p+1)+128<sp && p<top(core);p++)*p=PAINT;
 __asm volatile("dmb":::"memory");
 ready[core]=true;
}
uint32_t telemetry_stack_free(unsigned core){
 if(core>1||!ready[core])return 0;
 __asm volatile("dmb":::"memory");
 volatile uint32_t *p=bottom(core),*end=top(core);
 while(p<end&&*p==PAINT)p++;
 return (uint32_t)((uintptr_t)p-(uintptr_t)bottom(core));
}
uint32_t telemetry_static_headroom(void){return (uint32_t)((uintptr_t)__HeapLimit-(uintptr_t)__end__);}
#else
void telemetry_stack_init(unsigned core){ready[core]=true;}
uint32_t telemetry_stack_free(unsigned core){return core?2048:1536;}
uint32_t telemetry_static_headroom(void){return 16384;}
#endif
extern uint32_t merged_usb_late_packets(void);
extern uint32_t merged_midi_dropped(void);
static void cc14(uint8_t cc,uint32_t value){
 if(value>16383)value=16383;
 midi_send(0xBF,cc,(uint8_t)(value>>7));
 midi_send(0xBF,(uint8_t)(cc+32),(uint8_t)(value&127));
}
void telemetry_send(void){
 // Original CPU Note On/Off is sent first by the UI; these are additional CCs.
 cc14(20,telemetry_stack_free(0));cc14(21,telemetry_stack_free(1));
 cc14(22,audio_underruns());cc14(23,merged_usb_late_packets());
 cc14(24,merged_midi_dropped());cc14(25,audio_event_dropped());cc14(26,audio_midi_dropped());
 cc14(27,persist_successes());cc14(28,persist_errors());cc14(29,persist_status());
 cc14(30,telemetry_static_headroom()/16);cc14(31,audio_cpu_peak_max());
 cc14(19,persist_aborts());cc14(18,persist_loaded()?1:0);
 cc14(17,persist_sequence());
 cc14(83,save_blockers());            // 0.3.5: what holds saving back (0 = nothing)
 cc14(84,persist_quiet_ms()/100);     // time since the last key / knob / MIDI activity, 0.1 s
 // Reuse the last complete snapshot if a bounded seqlock read collides.
 static audio_profile_t profile;
 audio_profile_t next;
 if(audio_profile_get(&next))profile=next;
 cc14(64,profile.generation);cc14(65,profile_percent(profile.peak_us));
 cc14(66,profile.over_budget);
 for(unsigned i=0;i<PROFILE_STAGES;i++)cc14((uint8_t)(67+i),profile.stages[i]);
 cc14(72,profile.peak_seconds);cc14(73,profile.elapsed_seconds);
 cc14(74,profile_percent(profile.warmup_peak_us));
 cc14(75,profile_percent(profile.steady_peak_us));cc14(76,profile.peak_us);
 cc14(77,profile.lifetime_peak_seconds);cc14(78,profile.lifetime_peak_after_warmup);
 cc14(79,2);cc14(80,audio_profile_reset_pending()?1:0);
}
