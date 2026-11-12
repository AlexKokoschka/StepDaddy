// Pass 2 chord-level regression: reduced nonlinear-stage drive and bounded
// level difference in sine mode. Filename retained for build compatibility.
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "voice303.h"

#define N 24000
static int16_t buf[N];

static double render(const uint8_t *notes, int n, float drive, double *rms) {
  voice_t v; voice_init(&v);
  voice_params_t p = v.p; p.distortion = drive; p.dist_type = 1;
  voice_set_params(&v, &p);
  voice_chord_on(&v, notes, n, false, VOICE_RETRIGGER);
  voice_render(&v, buf, N);
  double s = 0; for (int i = 1200; i < N; i++) s += (double)buf[i] * buf[i];
  *rms = 10 * log10(s / (N - 1200) + 1e-9);
  return 0;
}

int main(void) {
  const uint8_t chord[] = {49, 54, 56, 57}, single[] = {45};
  int fails = 0;
  for (float d = 0.05f; d <= 1.001f; d += 0.05f) {
    double lc, ls;
    render(chord, 4, d, &lc);
    render(single, 1, d, &ls);
    if (fabs(lc - ls) > 6.0) { printf("drive %.2f: chord %.1f dB vs single %.1f dB\n", d, lc, ls); fails++; }
  }
  // the chord path must not equal the single-note path (it is driven less)
  voice_t a, b; voice_init(&a); voice_init(&b);
  voice_params_t p = a.p; p.distortion = 0.3f; p.dist_type = 1;
  voice_set_params(&a, &p); voice_set_params(&b, &p);
  if (a.od_gain_n[4] >= a.od_gain_n[1] || a.od_gain_n[1] != a.od_gain) { printf("chord gain not reduced\n"); fails++; }
  if (a.od_makeup_n[1] != (1 << 28)) { printf("single-note makeup is not 1\n"); fails++; }
  (void)b;
  if (fails) { printf("overdrive chord tests FAILED\n"); return 1; }
  printf("Overdrive chord tests passed\n");
  return 0;
}
