// TEST build: the switchable TB-303 timing set (voice_params_t.timing = 1).
// Checks the measured time constants against Yummy's own (timing = 0).
#include "../voice/src/voice303.c"
#include <assert.h>
#include <stdio.h>
static voice_t v;
static void setup(int timing, float decay) {
  voice_init(&v); voice_params_t p = v.p; p.timing = (uint8_t)timing; p.decay = decay; p.delay_amount = 0;
  voice_set_params(&v, &p);
}
static void ms(float t) { int16_t b[64]; int n = (int)(t * VOICE_FS / 1000.0f / 64.0f + 0.5f); for (int i = 0; i < n; i++) voice_render(&v, b, 64); }
static float glide_after(int timing, float t) {
  setup(timing, 0.4f); voice_note_on(&v, 45, false, false); ms(100);
  voice_note_on(&v, 57, false, true); ms(t);
  return (v.osc[0].pitch - 45.0f) / 12.0f;                 // share of the interval covered
}
static float meg_after(int timing, float decay, bool accent, float t) {
  setup(timing, decay); for (int i = 0; i < 40; i++) ms(1);   // knobs settle
  voice_note_on(&v, 45, accent, false); ms(t); return v.meg;
}
int main(void) {
  float g0 = glide_after(0, 50), g1 = glide_after(1, 50);
  printf("glide after 50 ms: Yummy %.0f%%, TB-303 set %.0f%%\n", g0 * 100, g1 * 100);
  assert(g0 > 0.50f && g0 < 0.62f);                         // tau 60 ms: ~57%
  assert(g1 > 0.85f && g1 < 0.92f);                         // tau 23 ms: ~89%
  float a1 = meg_after(1, 0.9f, true, 68);                  // accent: 68 ms whatever the Decay knob
  printf("TB-303 set: accent decay after 68 ms %.2f\n", a1);
  assert(a1 > 0.32f && a1 < 0.42f);
  float lo = meg_after(1, 0.0f, false, 68), hi = meg_after(1, 1.0f, false, 828), mid = meg_after(1, 0.5f, false, 190);
  printf("TB-303 set: Decay min after 68 ms %.2f, max after 828 ms %.2f, middle after 190 ms %.2f\n", lo, hi, mid);
  assert(lo > 0.32f && lo < 0.42f && hi > 0.32f && hi < 0.42f && mid > 0.30f && mid < 0.45f);
  setup(1, 0.4f); voice_note_on(&v, 45, false, false); ms(20); float v0 = v.vca; ms(1500);
  printf("TB-303 set: held-note level after 1.5 s %.2f of its start\n", v.vca / v0);
  assert(v.vca / v0 > 0.32f && v.vca / v0 < 0.42f);
  setup(0, 0.4f); voice_note_on(&v, 45, false, false); ms(20); v0 = v.vca; ms(1500);
  assert(v.vca / v0 > 0.55f && v.vca / v0 < 0.66f);         // Yummy: tau 3 s
  printf("PASS TB-303 timing set: glide, accent decay, Decay range, held-note fade; Yummy's set unchanged\n");
  return 0;
}
