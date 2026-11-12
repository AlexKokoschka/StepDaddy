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
static inline float voice_profile_decay_rate(float resonance, float decay_shape,
                                         int accented) {
  // 0.3.6: 20% slower overall, matched to the same 303 emulation's Decay sweep.
  float fast=0.8f*(0.00029f*(1.0f-resonance)+0.00027f*resonance);
  return accented ? fast : fast*(1.0f-decay_shape)+0.8f*0.000017f*decay_shape;
}
#endif
