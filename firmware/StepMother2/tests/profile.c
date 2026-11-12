#include "profile.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 profile_state_t p;profile_init(&p,1000000);
 const uint32_t normal[5]={10,20,30,180,10};
 const uint32_t boot[5]={10,300,20,150,10};
 const uint32_t burst[5]={190,20,20,250,10};
 profile_record(&p,boot,1100000);assert(p.value.peak_us==490);
 assert(profile_percent(p.value.peak_us)==147);assert(p.value.over_budget==1);
 assert(p.value.warmup_peak_us==490&&p.value.steady_peak_us==0);
 profile_record(&p,normal,7000000);assert(p.value.steady_peak_us==250);
 profile_reset(&p,1,8000000);assert(p.value.generation==1&&p.value.peak_us==0);
 assert(p.value.lifetime_peak_us==490&&p.value.warmup_peak_us==490);
 profile_record(&p,normal,9000000);assert(p.value.over_budget==0&&p.value.elapsed_seconds==1);
 profile_record(&p,burst,10000000);assert(p.value.peak_us==490&&p.value.stages[0]==190);
 assert(p.value.peak_seconds==9&&p.value.over_budget==1);
 // Equal peaks retain the first occurrence; lifetime boot attribution survives reset.
 assert(p.value.lifetime_peak_seconds==0&&p.value.lifetime_peak_after_warmup==0);
 const uint32_t larger[5]={200,20,20,250,10};profile_record(&p,larger,11000000);
 assert(p.value.lifetime_peak_us==500&&p.value.lifetime_peak_after_warmup==1);
 assert(p.value.steady_peak_us==500&&p.value.lifetime_peak_seconds==10);
 const uint32_t edge[5]={333,0,0,0,0};profile_reset(&p,2,12000000);
 profile_record(&p,edge,12000001);assert(p.value.over_budget==0);
 const uint32_t over[5]={334,0,0,0,0};profile_record(&p,over,12000002);
 assert(p.value.over_budget==1);p.value.over_budget=UINT32_MAX;
 profile_record(&p,over,12000003);assert(p.value.over_budget==UINT32_MAX);
 puts("PASS timing profiler: attribution, exact budget, warmup split, reset, counters and lifetime retention");
}
