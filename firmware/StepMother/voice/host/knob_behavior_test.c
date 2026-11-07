// Behaviour checks, not an reference synthesizer similarity test. Run with UBSan.
#include "voice303.h"
#include "voice_profile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static voice_t a, b;
static int16_t x[9600], y[9600];
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); ++fails; } } while (0)
static void irregular(voice_t *v, int16_t *out, int n) {
  const int sizes[] = {1,7,31,3,64,127,5};
  int i=0;
  while(n) { int k=sizes[i++%7]; if(k>n) k=n; voice_render(v,out,k); out+=k; n-=k; }
}
static void partition_test(void) {
  voice_init(&a); voice_init(&b);
  for(int event=0;event<8;event++) {
    voice_params_t p=a.p;
    p.cutoff=0.1f+0.1f*event; p.resonance=0.9f; p.distortion=0.6f;
    p.dist_type=event%2;
    voice_set_params(&a,&p); voice_set_params(&b,&p);
    voice_note_on(&a,40+event,event%2,event==3);
    voice_note_on(&b,40+event,event%2,event==3);
    // Deliberately cross event and control boundaries.
    int n=1201+event;
    voice_render(&a,x,n); irregular(&b,y,n);
    CHECK(memcmp(x,y,(size_t)n*sizeof(*x))==0);
    CHECK(a.meg==b.meg && a.acc_sweep==b.acc_sweep);
  }
  voice_note_off(&a); voice_note_off(&b);
  voice_render(&a,x,9600); irregular(&b,y,9600);
  CHECK(memcmp(x,y,sizeof(x))==0);
  puts("PASS render partition invariance, including note/parameter events");
}
static void envelopes(void) {
  voice_init(&a); voice_init(&b);
  voice_params_t p=a.p; p.decay=0;
  voice_set_params(&a,&p); p.decay=1; voice_set_params(&b,&p);
  voice_note_on(&a,45,false,false); voice_note_on(&b,45,false,false);
  voice_render(&a,x,9600); voice_render(&b,y,9600);
  CHECK(a.meg < b.meg);
  voice_note_on(&a,45,true,false); voice_note_on(&b,45,true,false);
  voice_render(&a,x,9600); voice_render(&b,y,9600);
  CHECK(fabsf(a.meg-b.meg)<1e-7f); // current accented decay overrides Decay
  float before=a.meg;
  voice_note_on(&a,52,true,true);
  CHECK(a.meg==before); // slide must not retrigger
  voice_init(&a); voice_init(&b);
  p=a.p;p.envmod=0;voice_set_params(&a,&p);
  p.envmod=1;voice_set_params(&b,&p);
  // After settling, Env Mod must lower the resting cutoff at fixed C/R.
  voice_render(&a,x,9600);voice_render(&b,y,9600);
  CHECK(b.last_cutoff_hz<a.last_cutoff_hz);
  puts("PASS decay ordering, accent decay override, slide and envmod offset");
}
static void extremes(void) {
  const uint8_t chord[]={33,40,45,52};
  for(int drive=0;drive<3;drive++) for(int type=0;type<2;type++) for(unsigned bits=0;bits<64;bits++) {
    voice_init(&a); voice_params_t p=a.p; p.dist_drive=drive*0.5f;
    p.cutoff=!!(bits&1); p.resonance=!!(bits&2); p.envmod=!!(bits&4);
    p.decay=!!(bits&8); p.accent=!!(bits&16); p.distortion=!!(bits&32);
    p.dist_type=(uint8_t)type; p.delay_amount=0.5f;
    voice_set_params(&a,&p);
    voice_note_on(&a,33,true,false); voice_render(&a,x,9600);
    voice_chord_on(&a,chord,4,true,VOICE_RETRIGGER); voice_render(&a,x,9600);
    voice_note_off(&a); voice_render(&a,x,9600);
    CHECK(isfinite(a.last_cutoff_hz) && a.last_cutoff_hz>=20 && a.last_cutoff_hz<=16000);
  }
  puts("PASS Drive 0/50/100% at all 64 six-knob corners, both distortion types, mono/chord/release + delay");
}
static void reference_mappings(void) {
  voice_profile_mod_t m=voice_profile_mod(0,0);
  CHECK(fabsf(m.scale-.1544948f)<1e-6f && fabsf(m.depth-.142f)<1e-6f);
  m=voice_profile_mod(1,1);
  CHECK(fabsf(m.scale-.3784304f)<1e-6f && fabsf(m.depth-.4841f)<1e-6f);
  m=voice_profile_mod(.5f,.5f);
  CHECK(fabsf(m.depth-.2880125f)<1e-6f);
  CHECK(fabsf(voice_profile_decay_rate(.5f,0,0)-.000224f)<1e-9f);
  CHECK(fabsf(voice_profile_decay_rate(.5f,1,0)-.0000136f)<1e-9f);
  CHECK(fabsf(voice_profile_decay_rate(.5f,1,1)-.000224f)<1e-9f);
  puts("PASS modulation and decay mapping fixtures (1.0.0 values, matched to a 303 emulation)");
}
static void invalid_parameters(void) {
  voice_init(&a);voice_params_t p=a.p;
  p.cutoff=NAN;p.resonance=INFINITY;p.decay=-1;p.envmod=-INFINITY;
  p.accent=5;p.distortion=NAN;p.volume=5;p.tune=-1;p.delay_amount=NAN;
  p.delay_time=INFINITY;p.dist_type=255;p.dist_drive=NAN;
  voice_set_params(&a,&p);voice_note_on(&a,45,true,false);voice_render(&a,x,9600);
  CHECK(a.p.cutoff==0 && a.p.resonance==1 && a.p.decay==0 && a.p.envmod==0);
  CHECK(a.p.accent==1 && a.p.distortion==0 && a.p.volume==1 && a.p.tune==0);
  CHECK(a.p.delay_amount==0 && a.p.delay_time==1 && a.p.dist_type==1);
  CHECK(isfinite(a.last_cutoff_hz) && a.p.dist_drive==0);
  puts("PASS non-finite/out-of-range normalized parameter handling");
}
static void zero_distortion_bypass(void) {
  voice_init(&a);voice_init(&b);voice_params_t p=a.p;
  p.distortion=0;p.dist_drive=1;p.dist_type=1;voice_set_params(&b,&p);
  voice_note_on(&a,45,true,false);voice_note_on(&b,45,true,false);
  voice_render(&a,x,9600);voice_render(&b,y,9600);
  CHECK(memcmp(x,y,sizeof(x))==0);
  puts("PASS distortion=0 bypass is identical in both modes");
}
static void drive_control(void) {
  for (int type=0; type<2; type++) {
    voice_init(&a); voice_init(&b);
    voice_params_t p=a.p;
    p.distortion=.6f;p.dist_type=type;p.dist_drive=0;
    voice_set_params(&a,&p);p.dist_drive=1;voice_set_params(&b,&p);
    voice_note_on(&a,45,false,false);voice_note_on(&b,45,false,false);
    voice_render(&a,x,9600);voice_render(&b,y,9600);
    CHECK(memcmp(x,y,sizeof(x))!=0);
    CHECK(a.p.distortion==b.p.distortion);
  }
  puts("PASS Drive changes both distortion modes independently of Mix");
}
int main(void) { reference_mappings(); drive_control(); zero_distortion_bypass(); partition_test(); envelopes(); extremes(); invalid_parameters(); return fails ? EXIT_FAILURE : EXIT_SUCCESS; }
