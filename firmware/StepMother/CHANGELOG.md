# Changelog

## 1.0.0 (first public release)

**StepBro** (MIDI step sequencer)

- Patterns in two banks, pattern chaining, per-step velocity, note repeat and
  record arm; the knob alone sets density (drops the weakest hits first).
- Follows the host's MIDI clock; Play restarts on the next beat in the host's
  time. MIDI lead (sequenced notes sent slightly ahead of the clock so they
  record on time in a DAW), default 4 ms.

**Yummy Gummy** (303-style synth)

- Saw/square voice, mono or paraphonic (up to four notes), with slides and
  accents; its own step sequencer with swing and per-step automation of eight
  parameters; melodies follow scale changes.
- Zero-delay-feedback ladder filter, in tune up to the top of its range.
  Cutoff, Resonance, Env Mod, Decay and level fitted to audio measurements of
  a reference 303 emulation.
- Distortion (soft clipping or sine shaping) with independent Mix and Drive;
  tempo-synced delay.
- 48 kHz audio to the computer over USB. Follows USB MIDI clock; MIDI in on
  channel 2 by default; MIDI out off by default (Function pad 14).

**General**

- **Shift + Bank + Play** switches the panel between StepBro and Yummy; both
  keep playing.
- Stopped-only saving to a CRC-checked two-bank journal.
- Diagnostics report over MIDI (Yummy **Function + Play**).
- StepDaddy updater for macOS and Windows.
