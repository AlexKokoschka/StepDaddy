# Changelog

## 1.2.0

- **StepBro, Note mode:** with record armed and the host playing, the playhead
  runs through the grid (pad 1 brighter at the start of each bar), so you can
  see where the bar starts.
- **StepBro, Note mode:** the knob on its own sets the selected sound's
  velocity. It picks up (nothing changes until the knob crosses the current
  value). With hits programmed, the knob is the sound's loudest hit and the
  others follow in proportion: accents keep their shape, the full range is
  reachable and nothing clips. A small dead band keeps a resting knob from
  flipping between two values. Density is now Pattern mode only.
- **StepBro, note repeat:** while a pad rolls (Shift + pad), the knob rides the
  velocity of the repeats, and a recorded roll keeps it.
- **StepBro, Pattern mode: loop window.** Hold Shift and press a step: that
  step repeats while you hold it. Hold a step and press a second: the steps
  between them loop for as long as Shift is down, even with the steps let go.
  Let go of Shift and the full pattern carries on where it would have been. The
  loop starts on the next sixteenth and uses the 16 steps in view (Bank);
  the looped steps glow dimly.
- **Yummy, Note page:** the playhead runs while recording is armed. Scale is
  dark until held; Oct - / + are dark at octave 0 and lit below / above it.
- **Yummy, Function page:** MIDI Out and Mono/Poly are dark when off / mono
  (the defaults) and bright when on / poly.
- **Yummy, chains:** as on StepBro: double-tap Pattern, tap the slots, then tap
  Pattern, Note or Function to finish. Holding Pattern and tapping a slot picks
  that one pattern.
- **Yummy, Pattern page (fix):** holding a step and turning now reaches the
  lowest and highest note of the Note page's range (C1 to C4, in the scale)
  from any knob position. Before, the range depended on where the knob sat
  when you grabbed the step, and could shrink to a few notes.
- **Yummy:** double-taps (Note record, Pattern chain, Function pads) accept
  0.4 s between taps, as on StepBro (was 0.3 s). Holding a step and turning
  has a dead band, so a still knob near one end cannot flicker between notes.
- **Saving is silent:** no more Play blink while saving, and no all-LED flash
  for the USB audio packet a save costs (it is silence: saves only run with
  everything stopped). The flash still warns about real dropouts in playback.
- **Yummy delay:** echoes are stored at 24 kHz instead of 48 kHz, which halves
  the delay's memory (96 KB to 48 KB; the RP2040 runs the whole program from
  RAM, so this is room for future features). A half-band filter on the way in
  and out keeps the echoes flat within about 0.5 dB up to 8 kHz; above that
  the first echo is a little darker (-3.5 dB at 10 kHz at most). Repeats were
  already darkened to 3.5 kHz. Delay times are unchanged.
- Saves are unchanged: patterns and settings from 1.1.0 load as they are.

## 1.1.0

- No changes to the Teensy firmware. The version is raised so that the Step
  Mother 1 (Teensy 4.1) and the new Step Mother 2 (RP2040, `firmware/StepMother2`)
  share one number, and StepDaddy installs either.

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
