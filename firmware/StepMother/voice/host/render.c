// Renders the 303 voice to WAV files on a computer, driven by 303-style
// step patterns (note, accent, slide, tie, rest) with knob automation.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "voice303.h"
#include "seq303.h"

#define REST (-1)
#define TIE  (-2)

typedef struct { int note; bool accent; bool slide; } step_t;
typedef void (*automation_fn)(float t, voice_params_t *p);   // t = 0..1 over the render

// ---------------------------------------------------------------------------
// WAV output
// ---------------------------------------------------------------------------

static void write_wav(const char *path, const int16_t *data, int n) {
  FILE *f = fopen(path, "wb");
  if (!f) { perror(path); exit(1); }
  uint32_t data_bytes = (uint32_t)n * 2, rate = VOICE_FS, byte_rate = VOICE_FS * 2;
  uint32_t riff_size = 36 + data_bytes, fmt_size = 16;
  uint16_t pcm = 1, channels = 1, align = 2, bits = 16;
  fwrite("RIFF", 1, 4, f); fwrite(&riff_size, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
  fwrite(&fmt_size, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&channels, 2, 1, f);
  fwrite(&rate, 4, 1, f); fwrite(&byte_rate, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
  fwrite("data", 1, 4, f); fwrite(&data_bytes, 4, 1, f); fwrite(data, 2, (size_t)n, f);
  fclose(f);

  int peak = 0, clipped = 0;
  double sum = 0;
  for (int i = 0; i < n; i++) {
    int a = abs(data[i]);
    if (a > peak) peak = a;
    if (a >= 32767) clipped++;
    sum += (double)data[i] * data[i];
  }
  printf("%-28s %6.2f s  peak %6.1f dBFS  rms %6.1f dBFS  clipped samples %d\n", path, (double)n / VOICE_FS,
         20 * log10((peak + 1) / 32768.0), 10 * log10(sum / n / (32768.0 * 32768.0) + 1e-12), clipped);
}

// ---------------------------------------------------------------------------
// Pattern player with 303 gate rules
// ---------------------------------------------------------------------------

static int16_t *render_pattern(const step_t *pat, int len, int bars, float bpm, automation_fn automate, int *out_n) {
  int spb = (int)(VOICE_FS * 60.0f / bpm / 4.0f);         // samples per 16th step
  int steps = bars * 16;
  int total = steps * spb + VOICE_FS / 2;                 // + tail
  int16_t *buf = calloc((size_t)total, sizeof(int16_t));
  voice_t v;
  voice_init(&v);

  bool prev_slide = false;
  int pos = 0;
  for (int s = 0; s < steps; s++) {
    const step_t *cur = &pat[s % len];
    const step_t *next = &pat[(s + 1) % len];

    voice_params_t p = v.p;
    automate((float)s / steps, &p);
    voice_set_params(&v, &p);

    if (cur->note >= 0) voice_note_on(&v, cur->note, cur->accent, prev_slide);

    // gate stays open through the step if this step slides or the next one is a tie
    bool sounding = cur->note >= 0 || (cur->note == TIE && v.gate);
    bool hold = sounding && (cur->slide || next->note == TIE);

    int half = spb / 2;
    voice_render(&v, buf + pos, half);
    if (!hold) voice_note_off(&v);
    voice_render(&v, buf + pos + half, spb - half);
    pos += spb;
    prev_slide = sounding && cur->slide;
  }
  voice_note_off(&v);
  voice_render(&v, buf + pos, total - pos);
  *out_n = total;
  return buf;
}

// ---------------------------------------------------------------------------
// Patterns (original, written for testing)
// ---------------------------------------------------------------------------

static const step_t pattern_a[16] = {
  {45, true, false}, {45, false, false}, {57, false, true}, {55, false, false},
  {REST, false, false}, {45, true, false}, {48, false, true}, {51, false, false},
  {45, false, false}, {TIE, false, false}, {57, true, true}, {45, false, false},
  {43, false, false}, {REST, false, false}, {48, true, false}, {50, false, true},
};

// Accents in a row, to hear the accent sweep build up with high resonance
static const step_t pattern_accents[16] = {
  {40, true, false}, {40, true, false}, {40, true, false}, {40, true, false},
  {40, true, false}, {40, true, false}, {52, true, false}, {40, true, false},
  {40, false, false}, {40, false, false}, {40, false, false}, {40, false, false},
  {43, false, true}, {47, false, true}, {52, false, true}, {55, true, false},
};

// One long held note for filter sweeps
static const step_t pattern_drone[1] = { {33, false, true} };

// ---------------------------------------------------------------------------
// Knob automation
// ---------------------------------------------------------------------------

static float lerp(float a, float b, float t) { return a + (b - a) * t; }

static void auto_acid_saw(float t, voice_params_t *p) {
  p->wave = WAVE_SAW; p->cutoff = lerp(0.12f, 0.65f, t); p->resonance = 0.75f;
  p->envmod = 0.6f; p->decay = 0.35f; p->accent = 0.8f; p->distortion = 0; p->volume = 0.8f;
}
static void auto_acid_square(float t, voice_params_t *p) {
  auto_acid_saw(t, p); p->wave = WAVE_SQUARE;
}
static void auto_acid_dist(float t, voice_params_t *p) {
  auto_acid_saw(t, p); p->resonance = 0.85f; p->distortion = 0.5f; p->volume = 0.7f;
}
static void auto_acid_dist_mild(float t, voice_params_t *p) {
  auto_acid_saw(t, p); p->resonance = 0.85f; p->distortion = 0.25f; p->volume = 0.7f;
}
static void auto_acid_dist_full(float t, voice_params_t *p) {
  auto_acid_saw(t, p); p->resonance = 0.85f; p->distortion = 1.0f; p->volume = 0.7f;
}
static void auto_sweep(float t, voice_params_t *p) {
  p->wave = WAVE_SAW; p->cutoff = t; p->resonance = 0.9f; p->envmod = 0; p->decay = 1.0f;
  p->accent = 0; p->distortion = 0; p->volume = 0.8f;
}
static void auto_accents(float t, voice_params_t *p) {
  p->wave = WAVE_SAW; p->cutoff = 0.25f; p->envmod = 0.5f; p->decay = 0.4f; p->accent = 1.0f;
  p->resonance = t < 0.5f ? 0.95f : 0.2f;                 // first half high res, then low
  p->distortion = 0; p->volume = 0.8f;
}
static void auto_decay_envmod(float t, voice_params_t *p) {
  p->wave = WAVE_SAW; p->cutoff = 0.2f; p->resonance = 0.7f; p->accent = 0.6f;
  p->decay = t; p->envmod = lerp(0.3f, 1.0f, t); p->distortion = 0; p->volume = 0.8f;
}

// Same pattern through the real sequencer engine (seq303.c), as on the board.
static int16_t *render_sequencer(int bars, float bpm, automation_fn automate, int *out_n) {
  int total = (int)(bars * 16 * (VOICE_FS * 60.0f / bpm / 4.0f)) + VOICE_FS / 2;
  int16_t *buf = calloc((size_t)total, sizeof(int16_t));
  voice_t v;
  voice_init(&v);
  seq_init();
  seq_bpm = bpm;
  voice_params_t p = v.p;
  automate(0.5f, &p);
  voice_set_params(&v, &p);
  seq_start(&v);
  int pos = 0, play_end = total - VOICE_FS / 2;
  while (pos + VOICE_BLOCK <= total) {
    if (pos >= play_end && seq_playing()) seq_stop(&v);
    seq_tick(&v, VOICE_BLOCK);
    voice_render(&v, buf + pos, VOICE_BLOCK);
    pos += VOICE_BLOCK;
  }
  *out_n = pos;
  return buf;
}

static void auto_fixed(float t, voice_params_t *p) { (void)t; auto_acid_saw(0.5f, p); }

int main(int argc, char **argv) {
  const char *dir = argc > 1 ? argv[1] : ".";
  char path[512];
  int n;
  int16_t *b;

  struct { const char *name; const step_t *pat; int len; int bars; automation_fn fn; } jobs[] = {
    {"01_acid_saw.wav",        pattern_a,       16, 8, auto_acid_saw},
    {"02_acid_square.wav",     pattern_a,       16, 8, auto_acid_square},
    {"03_acid_distortion.wav", pattern_a,       16, 8, auto_acid_dist},
    {"03b_distortion_mild.wav",  pattern_a,     16, 8, auto_acid_dist_mild},
    {"03c_distortion_full.wav",  pattern_a,     16, 8, auto_acid_dist_full},
    {"04_accent_sweep.wav",    pattern_accents, 16, 4, auto_accents},
    {"05_decay_envmod.wav",    pattern_a,       16, 8, auto_decay_envmod},
    {"06_cutoff_sweep.wav",    pattern_drone,    1, 4, auto_sweep},
  };
  for (size_t i = 0; i < sizeof(jobs) / sizeof(jobs[0]); i++) {
    b = render_pattern(jobs[i].pat, jobs[i].len, jobs[i].bars, 130.0f, jobs[i].fn, &n);
    snprintf(path, sizeof(path), "%s/%s", dir, jobs[i].name);
    write_wav(path, b, n);
    free(b);
  }
  // pattern player vs. the firmware's sequencer engine, same knobs
  b = render_pattern(pattern_a, 16, 4, 130.0f, auto_fixed, &n);
  snprintf(path, sizeof(path), "%s/%s", dir, "07a_pattern_player.wav"); write_wav(path, b, n); free(b);
  b = render_sequencer(4, 130.0f, auto_fixed, &n);
  snprintf(path, sizeof(path), "%s/%s", dir, "07b_sequencer_engine.wav"); write_wav(path, b, n); free(b);
  return 0;
}
