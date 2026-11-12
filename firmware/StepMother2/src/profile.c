#include "profile.h"
#include "voice303.h"
#include <string.h>
#include <limits.h>
uint32_t profile_percent(uint32_t us) {
 return (uint32_t)((uint64_t)us * VOICE_FS / ((uint64_t)VOICE_BLOCK * 10000u));
}
void profile_init(profile_state_t *p, uint64_t now) {
 memset(p,0,sizeof(*p));p->boot_us=p->session_us=now;
}
void profile_reset(profile_state_t *p, uint32_t generation, uint64_t now) {
 p->value.generation=generation;p->value.peak_us=0;
 memset(p->value.stages,0,sizeof(p->value.stages));
 p->value.peak_seconds=0;p->value.over_budget=0;p->value.elapsed_seconds=0;
 p->session_us=now;
}
void profile_record(profile_state_t *p,const uint32_t stages[PROFILE_STAGES],uint64_t now) {
 uint32_t dt=0;for(unsigned i=0;i<PROFILE_STAGES;i++)dt+=stages[i];
 uint32_t sec=(uint32_t)((now-p->boot_us)/1000000u);
 if(dt>p->value.peak_us){
  p->value.peak_us=dt;p->value.peak_seconds=sec;
  memcpy(p->value.stages,stages,sizeof(p->value.stages));
 }
 // Exact rational deadline: 16 samples / 48 kHz. No rounded-333us comparison.
 if((uint64_t)dt*VOICE_FS>(uint64_t)VOICE_BLOCK*1000000u && p->value.over_budget<UINT32_MAX)
  p->value.over_budget++;
 uint32_t *phase=(now-p->boot_us<5000000u)?&p->value.warmup_peak_us:&p->value.steady_peak_us;
 if(dt>*phase)*phase=dt;
 if(dt>p->value.lifetime_peak_us){
  p->value.lifetime_peak_us=dt;p->value.lifetime_peak_seconds=sec;
  p->value.lifetime_peak_after_warmup=(now-p->boot_us>=5000000u);
 }
 p->value.elapsed_seconds=(uint32_t)((now-p->session_us)/1000000u);
}
