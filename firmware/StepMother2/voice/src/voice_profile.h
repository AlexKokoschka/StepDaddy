// Yummy control profile. Knob mappings fitted to audio measurements are kept
// here, apart from the fixed-point filter/amp/accent implementation.
#ifndef VOICE_PROFILE_H_
#define VOICE_PROFILE_H_

// Startup Drive for the independent Drive control (hold Distortion + knob).
// Distortion amount remains the wet mix. This default is chosen, not measured.
#ifndef VOICE_PROFILE_DRIVE
#define VOICE_PROFILE_DRIVE 0.5f
#endif

typedef struct { float scale, depth; } voice_profile_mod_t;
static inline voice_profile_mod_t voice_profile_mod(float cutoff, float envmod) {
  float c2=cutoff*cutoff, e2=envmod*envmod;
  float lo=0.142f*(1.0f-c2)+0.0898f*c2;
  // 0.3.6, matched to a 303 emulation (Cutoff, Env Mod and Decay sweeps with
  // Resonance at maximum): Env Mod reaches further at high Cutoff (0.37 ->
  // 0.48: the envelope now lifts the filter about two octaves, to the top of
  // its range, where it lifted it about one) and the bottom of the Cutoff knob
  // sits lower (scale at Cutoff 0: 0.19 -> 0.17). Env Mod at 0 is unchanged.
  // 1.0.0: refitted with the ZDF filter core (its resonance sits exactly at
  // the cutoff) to the same emulation's resonant peaks across Cutoff, Env Mod
  // and Decay.
  float hi=0.8589f*(1.0f-c2)+0.4841f*c2;
  return (voice_profile_mod_t){
    0.05f+0.636f*(0.1643f+0.3521f*c2),
    lo+(hi-lo)*e2
  };
}
// Per-sample coefficient at 44.1 kHz, before sample-rate conversion.
// decay_shape is sqrt(normalized Decay), smoothed in that domain.
// 1.3.0: time constants measured on a TB-303. They come from the DW-303
// project (GPL-3.0-or-later, github.com/mo0kid/DW-303), whose circuit model
// was fitted to 346 hardware recordings: an accented note decays in 68 ms
// whatever the knob; otherwise Decay sweeps 68 ms .. 0.83 s on that
// project's knob taper (0.6x^3 + 0.4x^2).
static inline float voice_profile_decay_rate(float decay_shape, int accented) {
  float x=decay_shape*decay_shape;
  float tau=accented ? 0.068f : 0.068f+0.76f*x*x*(0.6f*x+0.4f);
  return 1.0f/(tau*44100.0f);
}
#endif
