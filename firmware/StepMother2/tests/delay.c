// 1.2.0: the delay stores its echoes at 24 kHz. Checks it directly: echo
// time and level, what the half-band filter keeps out, fractional delay
// times, stale-history rejection after a cold activation, buffer wrap, and
// long runs through the voice with tempo and division changes (UBSan).
#include "../voice/src/voice303.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#define N 12000
static voice_t v;
static int32_t in[N], out[N];
static void setup(float d) {
  memset(&v, 0, sizeof(v));
  v.dl_on = true; v.dl_valid = VOICE_DELAY_BUF;
  v.dl_d = v.dl_target = (int32_t)(d * 256.0f);
  v.dl_wet = ONE_Q28; v.dl_fb = 0;
}
static void run(int n) { for (int i = 0; i < n; i++) out[i] = delay(&v, in[i]); }
static double rms_echo_error(float d, int from, int to) {   // echo vs the input d samples ago
  double e = 0, s = 0;
  for (int i = from; i < to; i++) {
    double t = i - d; int k = (int)t; double f = t - k;
    double want = in[k] * (1 - f) + in[k + 1] * f;
    double got = (double)out[i] - in[i];
    e += (got - want) * (got - want); s += want * want;
  }
  return sqrt(e / s);
}
static void sine(float hz, float amp) {
  for (int i = 0; i < N; i++) in[i] = Q28(amp * sinf(6.2831853f * hz * i / VOICE_FS));
}
int main(void) {
  assert(sizeof(v.dl_buf) == 49152);                         // half of the old 96 KB
  // An impulse comes back once, d samples later, at full level.
  for (int d = 1000; d <= 1001; d++) {
    setup((float)d); memset(in, 0, sizeof(in)); in[0] = Q28(0.5f); run(3000);
    int peak = 0; double sum = 0;
    for (int i = 1; i < 3000; i++) { if (abs(out[i]) > abs(out[peak ? peak : 1])) peak = i; if (i > d - 12 && i < d + 12) sum += out[i]; }
    assert(abs(peak - d) <= 1);
    assert(fabs(sum / Q28(0.5f) - 1.0) < 0.02);
    for (int i = 1; i < 3000; i++) if (i < d - 12 || i > d + 12) assert(abs(out[i]) < Q28(0.001f));
  }
  // 1 kHz and 6 kHz: echoes match the input exactly delayed (also fractional delays).
  float ds[3] = {1000.0f, 1000.5f, 7321.25f};
  for (int j = 0; j < 3; j++) {
    sine(1000, 0.5f); setup(ds[j]); run(N); assert(rms_echo_error(ds[j], (int)ds[j] + 40, N - 2) < 0.02);
    sine(6000, 0.5f); setup(ds[j]); run(N); assert(rms_echo_error(ds[j], (int)ds[j] + 40, N - 2) < 0.1);
  }
  // 16 kHz is kept out of the echo (it would otherwise fold back to 8 kHz).
  sine(16000, 0.5f); setup(1000); run(N);
  { double e = 0, s = 0; for (int i = 1100; i < N; i++) { double x = (double)out[i] - in[i]; e += x * x; s += (double)in[i] * in[i]; }
    assert(sqrt(e / s) < 0.03); }                            // below -30 dB
  // Stale memory after a cold activation reads as silence.
  setup(500); for (int i = 0; i < VOICE_DELAY_BUF; i++) v.dl_buf[i] = (int16_t)(10000 + i % 10000);
  v.dl_valid = 0; memset(in, 0, sizeof(in)); run(4000);
  for (int i = 0; i < 4000; i++) assert(out[i] == 0);
  assert(v.dl_buf[3000] == 13000);                           // and was never cleared in bulk
  // Wrap round the end of the buffer.
  setup(1000); v.dl_w = VOICE_DELAY_BUF - 3; memset(in, 0, sizeof(in)); in[0] = Q28(0.5f); run(3000);
  { int peak = 1; for (int i = 1; i < 3000; i++) if (abs(out[i]) > abs(out[peak])) peak = i; assert(abs(peak - 1000) <= 1); }
  // The longest delay (1/4 at 20 BPM is clipped to the buffer) and the shortest.
  setup(VOICE_DELAY_LEN - 4); sine(500, 0.5f); run(N);
  setup(16); sine(500, 0.5f); run(N); assert(rms_echo_error(16, 60, N - 2) < 0.02);
  // Through the voice: notes, delay toggles, tempo and division changes.
  unsigned rendered = 0;
  for (int mode = 0; mode < 2; mode++) {
    voice_init(&v); voice_note_on(&v, 48, true, false);
    voice_params_t p = v.p; p.dist_type = (uint8_t)mode; p.distortion = 0.8f; p.delay_amount = 0.7f;
    for (int j = 0; j < 12; j++) {
      p.delay_time = (float)(j % 6) / 6.0f; p.delay_amount = (j % 3) ? 0.2f + 0.06f * j : 0.0f;
      voice_set_params(&v, &p); voice_set_tempo(&v, 60.0f + 19.0f * j);
      if (j % 4 == 0) voice_note_on(&v, 40 + j, j & 1, false);
      int16_t buf[64];
      for (int k = 0; k < 900; k++) { voice_render(&v, buf, 64); rendered += 64; }
    }
    voice_set_tempo(&v, 10000000.0f); { int16_t buf[64]; for (int k = 0; k < 200; k++) { voice_render(&v, buf, 64); rendered += 64; } }
  }
  printf("PASS delay at 24 kHz: echo time and level, fractional delays, half-band keeps 16 kHz below -30 dB, stale history silent, wrap, %u voice samples under UBSan\n", rendered);
  return 0;
}
