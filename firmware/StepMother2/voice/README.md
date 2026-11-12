# Yummy voice — pass 3

Experimental fixed-point bass synthesizer with independent distortion Mix and Drive.

- Cutoff/Env Mod use a squared-control blend and exponential map, fitted to audio measurements.
- Decay is smoothed in its square-root domain; the measured decay coefficient
  depends on Resonance and selects a fast branch for accented notes.
- Several controls use a block-aggregated version of a .005 smoother,
  normalized to a 44.1 kHz time base. Audio coefficients interpolate each sample.
- Accent uses persistent charge/follower/amplitude states. The detailed envelope
  network is an approximation; its boost now precedes distortion.
- The original four-stage fixed-point integration remains, with rational rather
  than tanh saturation. High resonance uses a progressive taper and brightness/accent damping.
- Distortion mode 0 is rational clipping; mode 1 is sine shaping. The knob sets
  wet amount. Drive is independent (hold Distortion + knob), initially VOICE_PROFILE_DRIVE=.5.
- Nonlinear processing uses generated LUTs, not per-sample floating point.
- Slides, chords, sequencer, delay and the 16-sample control clock are retained.

The filter topology, amplifier timing, accent RC network, oscillator waveform,
FX filtering/oversampling and final gain are still approximations. A measured reference match has not been established.

## Verify on a host

```sh
make test profile-test stress-test
make wavs
python3 calibration/calibrate.py render
```

Host GCC/Clang and Python 3 are needed. Tests default to UBSan; on a machine that
supports AddressSanitizer/LeakSanitizer, use SANITIZERS=undefined,address.
Firmware continues to require Pico SDK 2.1.1 and an Arm compiler. A test UF2 is included in the release package; its board CPU/USB performance
has not yet been verified.

The two distortion choices keep their existing pad/MIDI numbers, but their sound
has changed. Recordings/presets made with the old tube/Rat modes will sound
different. This merged port retains combined journal saving; Drive uses reserved metadata.
