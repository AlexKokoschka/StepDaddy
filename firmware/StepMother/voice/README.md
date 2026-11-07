# Yummy voice

Portable fixed-point 303-style bass voice and step sequencer. Plain C, no
platform code: `src/voice303.c` renders 48 kHz audio in 16-sample control
blocks; `src/seq303.c` is the sequencer; `src/voice_profile.h` holds the knob
mappings.

- **Oscillator:** saw or square, mono or paraphonic (up to four notes), with slides.
- **Filter:** four-pole zero-delay-feedback ladder (trapezoidal one-pole stages,
  rational saturation, feedback solved within each sample), in tune up to the
  top of its range without oversampling. Cutoff, Resonance, Env Mod, Decay and
  level are fitted to audio measurements of a reference 303 emulation.
- **Envelope and accent:** Decay smoothed in its square-root domain, faster
  branch for accented notes; accent with persistent charge/follower/amplitude
  states, boosting the signal before distortion.
- **Distortion:** rational soft clipping or sine shaping, with independent Mix
  and Drive; then a tempo-synced delay.
- Nonlinear stages use lookup tables generated at startup, so the per-sample
  path is integer-only (portable to microcontrollers without an FPU).

## Host tests

```sh
make test profile-test stress-test
make wavs
```

Host GCC or Clang is needed. Tests use UBSan by default; where
AddressSanitizer/LeakSanitizer are available, use `SANITIZERS=undefined,address`.
