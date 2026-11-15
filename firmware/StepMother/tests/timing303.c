// 1.3.0: Yummy's time constants measured on a TB-303 (after DW-303,
// GPL-3.0-or-later, github.com/mo0kid/DW-303).
#include "../voice/src/voice303.c"
#include <assert.h>
#include <stdio.h>
static voice_t v;
static void setup(float decay) {
  voice_init(&v); voice_params_t p = v.p; p.decay = decay; p.delay_amount = 0;
  voice_set_params(&v, &p);
}
static void ms(float t) { int16_t b[64]; int n = (int)(t * VOICE_FS / 1000.0f / 64.0f + 0.5f); for (int i = 0; i < n; i++) voice_render(&v, b, 64); }
static float meg_after(float decay, bool accent, float t) {
  setup(decay); for (int i = 0; i < 40; i++) ms(1);   // knobs settle
  voice_note_on(&v, 45, accent, false); ms(t); return v.meg;
}
int main(void) {
  setup(0.4f); voice_note_on(&v, 45, false, false); ms(100);
  voice_note_on(&v, 57, false, true); ms(50);
  float g = (v.osc[0].pitch - 45.0f) / 12.0f;          // share of the slide covered
  printf("slide after 50 ms: %.0f%%\n", g * 100);
  assert(g > 0.85f && g < 0.92f);                       // tau 23 ms: ~89%
  float a = meg_after(0.9f, true, 68);                  // accent: 68 ms whatever the Decay knob
  printf("accent decay after 68 ms %.2f\n", a);
  assert(a > 0.32f && a < 0.42f);
  float lo = meg_after(0.0f, false, 68), hi = meg_after(1.0f, false, 828), mid = meg_after(0.5f, false, 190);
  printf("Decay min after 68 ms %.2f, max after 828 ms %.2f, middle after 190 ms %.2f\n", lo, hi, mid);
  assert(lo > 0.32f && lo < 0.42f && hi > 0.32f && hi < 0.42f && mid > 0.30f && mid < 0.45f);
  setup(0.4f); voice_note_on(&v, 45, false, false); ms(20); float v0 = v.vca; ms(1500);
  printf("held-note level after 1.5 s %.2f of its start\n", v.vca / v0);
  assert(v.vca / v0 > 0.32f && v.vca / v0 < 0.42f);
  printf("PASS TB-303 timings: slide, accent decay, Decay range, held-note fade\n");
  return 0;
}
