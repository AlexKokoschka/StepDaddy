// TB-303-style voice: mono like the original, or paraphonic (up to 4
// oscillators through the one filter and envelopes).
//
// Portable C (no RP2040 calls) so it can be rendered on a computer and then
// dropped into the firmware unchanged.
//
// Split of work:
//  - The audio path (oscillator, filter, VCA, distortion) runs per sample in
//    fixed point (Q28), because the RP2040 has no floating-point unit.
//  - Envelopes, pitch glide and parameter mapping run once per 16-sample block
//    in float (about 3,000 times a second, cheap even in software float),
//    and are interpolated linearly across the block.

#ifndef VOICE303_H_
#define VOICE303_H_

#include <stdint.h>
#include <stdbool.h>

// Output sample rate (both boards: 48 kHz USB audio). Everything in the
// voice and sequencer is derived from it.
#ifndef VOICE_FS
#define VOICE_FS          48000
#endif
#define VOICE_BLOCK       16      // control-rate block length (samples)
#define VOICE_MAX_NOTES   4       // oscillators (paraphonic chords)

typedef enum { WAVE_SAW = 0, WAVE_SQUARE = 1 } voice_wave_t;

// All knob parameters are 0.0 .. 1.0, like the panel controls.
typedef struct {
  float cutoff;
  float resonance;
  float envmod;
  float decay;
  float accent;
  float tune;        // 0.5 = centre
  float volume;
  float distortion;  // 0 = clean
  voice_wave_t wave;
  uint8_t dist_type; // 0 = rational soft clipping, 1 = sine shaping (pass 2)
  float delay_time;  // 0..1 -> 1/16, 1/8T, 1/8, 1/4T, 1/8D, 1/4 (tempo-synced)
  float dist_drive;  // 0..1: independent Drive; default 0.5
  float delay_amount;// 0 = off .. 1 = long dub tails (echo level and feedback together)
} voice_params_t;

#define VOICE_DELAY_LEN   49152   // delay span in 48 kHz samples (~1 s)
// 1.2.0: echoes are stored at half rate (24 kHz): ~1 s in 48 KB instead of 96.
#define VOICE_DELAY_BUF   (VOICE_DELAY_LEN / 2)
#define VOICE_DELAY_DIVS  6
extern const char *const voice_delay_div_names[VOICE_DELAY_DIVS];
int voice_delay_div(float delay_time);   // knob value -> division index

typedef struct {
  uint32_t phase, phase_inc;
  int32_t  phase_inc_step;            // per-sample interpolation
  uint32_t target_inc;
  int32_t  gain, gain_step;           // mix level (Q28), faded so notes never click in or out
  float    pitch, pitch_target;       // semitones (MIDI note units)
  float    last_semis;
  bool     gliding;
  bool     active;                    // part of the current note / chord
} voice_osc_t;

typedef struct {
  // --- per-sample state (fixed point, Q28) ---
  voice_osc_t osc[VOICE_MAX_NOTES];
  int      n_notes;                   // oscillators in the current note / chord
  // Filter (1.0.0): zero-delay-feedback ladder, trapezoidal one-pole stages
  // with rational saturation and the feedback loop solved within each sample.
  int32_t  y4;                        // filter output (Q28, signal scale)
  int32_t  fb_lp;                     // feedback high-pass (low-pass part)
  int32_t  g_hp;                      // feedback high-pass coefficient (Q30)
  int32_t  zs[4];                     // stage integrator states (Q28, normalised)
  int32_t  zG, zG_step;               // stage gain G = g/(1+g) (Q30)
  int32_t  za[4], za_step[4];         // state weights of the solved loop (Q30)
  int32_t  zkc, zkc_step;             // feedback x high-pass factor (Q28)
  int32_t  zr, zr_step;               // 1/(1 + kc G^4) (Q30)
  int32_t  out_gain, out_gain_step;   // VCA x accent x makeup (Q28)
  int32_t  post_gain, post_gain_step; // chords: accent and level after the distortion (Q28)
  int32_t  drive;                     // distortion drive (Q24)
  int32_t  dist_makeup;               // rational-shaper gain compensation (Q28)
  int32_t  dist_makeup_target, dist_makeup_step; // interpolated with Drive
  int32_t  dist_prev;                 // previous input, for 2x oversampling
  int32_t  dist_dc;                   // DC blocker state
  int32_t  tone_lp;                   // post-distortion tone filter state
  int32_t  g_tone, g_dc;              // (Q30)
  int32_t  dist_wet;                  // blend (Q28)
  bool     dist_on;
  int32_t  od_gain;                   // rational path gain (Q16)
  int32_t  od_gain_n[VOICE_MAX_NOTES + 1];   // chords: gain for n notes (Q16, n = 1 is od_gain)
  int32_t  od_makeup_n[VOICE_MAX_NOTES + 1]; // unity; chord makeup is after FX

  // Pass 2: effect controls interpolated without float in the sample loop.
  int32_t  soft_gain, soft_gain_step; // Q16
  int32_t  sine_gain, sine_gain_step; // Q16
  int32_t  dist_mode, dist_mode_step; // Q28 crossfade between the two modes
  int32_t  dist_wet_step;

  // --- delay ---
  int16_t  dl_buf[VOICE_DELAY_BUF];   // 24 kHz
  uint32_t dl_valid;                  // valid newest stored samples since cold activation
  uint32_t dl_w;                      // next write position (24 kHz)
  uint8_t  dl_half;                   // 0: this 48 kHz sample writes one at 24 kHz
  uint8_t  dl_xi;                     // newest entry in dl_x
  int32_t  dl_x[16];                  // 48 kHz history for the half-band decimator (Q15)
  int32_t  dl_d, dl_target;           // delay in samples (Q8), glides to the target
  int32_t  dl_lp, dl_hp;              // feedback tone (darker each repeat, no mud)
  int32_t  g_dl_lp, g_dl_hp;          // (Q30)
  int32_t  dl_wet, dl_fb;             // (Q28)
  bool     dl_on;                     // running (keeps running ~1 s after being turned off)
  int32_t  dl_off;                    // samples left to run while off (keeps the memory fresh)
  float    bpm;

  // --- control-rate state (float) ---
  float pitch_target;   // lowest note of the current chord (for tests / display)
  bool  gate;
  bool  accented;       // current note has accent
  float meg;            // filter (main) envelope, 1 -> 0
  float vca;            // amp envelope 0..1
  bool  vca_attack;
  float acc_sweep;      // accent "wow" capacitor, 0..1
  float last_cutoff_hz; // approximate effective frequency used by Yummy's core
  float acc_charge, acc_vca; // separate persistent accent states
  float knob[6];       // C, R, E, sqrt(D), scaled A, distortion amount
  float mode_smooth;
  struct {
    float knob_coef, acc_follow, acc_amp;
  } profile;


  struct {              // derived from the knobs (voice_set_params)
    float tune_semis, decay_shape, acc_gain, level;
  } d;
  struct {              // fixed constants
    float glide_coef, vca_attack_step, vca_hold_mul, vca_release_mul;
  } c;

  voice_params_t p;
  // Preserve the 16-sample control clock across arbitrary host render sizes.
  uint8_t control_remaining;
} voice_t;

void voice_init(voice_t *v);
// Recomputes derived values (a few dozen float operations): call when knobs change.
void voice_set_params(voice_t *v, const voice_params_t *p);
// Tempo for the synced delay (call when it changes; cheap).
void voice_set_tempo(voice_t *v, float bpm);

// 303 note semantics:
//  legato = false: new note, envelopes retrigger.
//  legato = true : the previous note had slide (or this is a tie) - the pitch
//                  glides to the new note and nothing retriggers.
void voice_note_on(voice_t *v, int note, bool accent, bool legato);
void voice_note_off(voice_t *v);

// Chords (paraphonic): up to VOICE_MAX_NOTES notes, any order.
//  VOICE_RETRIGGER: new chord, envelopes restart.
//  VOICE_SLIDE    : no retrigger; each note glides to the matching note of the
//                   new chord (lowest to lowest). Extra notes start fresh,
//                   missing ones fade out.
//  VOICE_LEGATO   : no retrigger and no glide (adding / removing notes of a
//                   chord that is being played).
typedef enum { VOICE_RETRIGGER, VOICE_SLIDE, VOICE_LEGATO } voice_mode_t;
void voice_chord_on(voice_t *v, const uint8_t *notes, int n, bool accent, voice_mode_t mode);

// Renders n samples (any n; internally processed in VOICE_BLOCK chunks).
void voice_render(voice_t *v, int16_t *out, int n);

#endif
