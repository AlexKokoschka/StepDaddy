# 1.2.0

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
- Saves are unchanged: patterns and settings from 1.1.0 load as they are.

# 1.1.0 (first public release for the Step Mother 2, RP2040)

The same StepBro and Yummy Gummy firmware as the Step Mother 1, built for the
RP2040 (200 MHz, 48 kHz USB audio and MIDI). It shows up as "Step Mother".
Why the RP2040: it is easier to source and cheaper than a Teensy module, it is
surface-mounted so the whole board can be machine-assembled (no hand soldering,
fewer errors), and the USB socket is now USB-C instead of micro-USB.

Everything below this entry is the history of the RP2040 firmware before this
release.

# 1.0.0 (first release; Yummy: zero-delay-feedback filter)

- New filter core: the four-stage ladder now solves its feedback loop within
  each sample (zero-delay feedback) instead of a sample late. It stays in tune
  and stable up the whole range without oversampling, so the 11 kHz cap of
  0.3.6 is gone (the open filter now reaches about 14.5 kHz), and so is the
  resonance taper above 5.4 kHz that hid the old core's 12 kHz ringing. The
  resonance now reaches the top of the range, as in the 303 emulation it is
  matched to.
- Refitted to the same emulation recordings as 0.3.6 (17 held settings,
  Resonance steps at Cutoff 50, open filter, sweeps and holds): resonant peaks
  mostly within 5-10% (Cutoff 50, Env Mod 100 to 0: within a few percent),
  levels within about 1 dB (Env Mod 0-50% at Cutoff 50 sits 1-2 dB loud;
  Cutoff 0 with high Resonance is a little more resonant than the emulation).
  The Resonance knob's volume drop at Cutoff 50 is about -2.1, -4.8, -6.6,
  -5.2 dB at 25, 50, 75, 100% (emulation -3.4, -5.4, -6.0, -5.4).
- Mapping changes: Cutoff/Env Mod curve refitted; feedback 4.6 + 0.79r^2 at
  the knob's top (the zero-delay loop needs more gain for the same peak); a
  little less feedback below about 1.2 kHz (at most 21%).
- Cheaper: less CPU than 0.3.6 on the RP2040 (heavy patch, measured:
  74% average and 92% peak since power-up against 85% and 98%, no blocks over
  budget, worst render 274 us against 296 us), and 14.3 KB of static RAM
  headroom.
- Cleaner at the extremes: in a 20 s random knob/chord/accent stress test 49
  samples reach full scale (0.3.6: 1963); no residual sound after release at
  any Cutoff/Resonance setting.

# 0.3.6 (Yummy filter matched to a 303 emulation, MIDI defaults)

- StepBro MIDI lead defaults to 4 ms (was 8), chosen by recording into
  Ableton. A saved lead is kept.
- Yummy MIDI out is off by default (Function pad 14 dark); tap pad 14 to
  send Yummy's notes and knob moves on its MIDI channel. Saves from before
  0.3.6 load with it off too; the choice is saved from 0.3.6 on. Flag bit 0
  of the saved Yummy settings (the old clock-source flag) is written clear to
  mark this; firmware older than 0.3.4 would read such a save as internal
  clock.
- Yummy's resonance at high settings is voiced closer to a 303: weak while the
  envelope holds the cutoff high, then growing, narrow and "rubbery" as it
  falls into the 0.5-3 kHz range, instead of a loud whistle around 4-6 kHz.
  Measured against a reference 303 emulation with Resonance and Env Mod at
  maximum (open filter and a downward sweep): in the 0.7-3 kHz range the peak
  rises from about -8 dB to about -1.5 dB relative to the fundamental, and the
  3.5-6 kHz band drops by about 10 dB.
- More feedback toward the top of the Resonance knob (3.7 -> 4.18 at maximum;
  unchanged at low settings), taken away above about 5.4 kHz with a steep
  taper (95% gone at the very top, so no whistle parks there). Fitted to the
  open-filter test, both sweeps and held settings (Cutoff, Env Mod and Decay
  at 100/50/0%) together.
- Level fitted to the same holds: the voice no longer jumps about 6 dB louder
  when the filter is wide open (the taper removes feedback, and feedback costs
  passband level), and, like the emulation, it gets louder as Cutoff closes:
  about +5 dB at 50% and +6.5 dB at 0, Resonance at maximum. Both scale with
  Resonance, so low Resonance is unchanged. Held-setting levels now match the
  emulation within about 1 dB.
- Resonance knob matched to the emulation at Cutoff 50% (Resonance 0, 25,
  50, 75, 100%): the volume drop when turning it up is now the emulation's
  (about -3.4, -5.4, -6.0, -5.4 dB; it was down to -9 dB), within 0.8 dB, and
  the knob follows r^1.5, so the resonance stays broad up to about 75% and
  turns sharp near the top, as in the emulation. Maximum is unchanged.
  With Cutoff fully open the drop comes earlier, as in the emulation (about
  -9.3, -10.3, -10.1 dB at 50, 70, 100% Resonance against -9.6, -10.3, -10.7).
  Checked against 17 held settings (Resonance, Env Mod and Decay steps at
  Cutoff 100 and 50): filter positions mostly within 10-15%, levels within
  about 1 dB; Env Mod 0-50% at Cutoff 50 sits about a quarter-octave high.
  Open303, compared as a second reference, drops 12-15 dB.
- Cutoff, Env Mod and Decay matched to the same emulation (each knob swept
  down and up with the others at maximum):
  - the filter's cutoff is now prewarped (tan) instead of compressed, so it
    reaches about 11 kHz; it stopped near 5-6 kHz, which made Env Mod seem
    weak and parked the resonance there. Capped at 11 kHz: higher, the ladder
    rings at a fixed ~12 kHz;
  - Env Mod at high Cutoff lifts the filter about two octaves (was about one);
    Env Mod at 0 is unchanged (about 3.1-3.5 kHz at full Cutoff, as the
    emulation);
  - the bottom of the Cutoff knob sits a little lower;
  - the envelope decays 20% more slowly at every Decay setting.
  Low Resonance sounds brighter at high Cutoff. Overall level is unchanged.
- tests/reference_voice303_pass3.c mirrors the change, so the delay comparison
  still checks the delay against an otherwise identical voice.

# 0.3.5 (Yummy MIDI lead in 1 ms steps, save diagnostics)

- Yummy MIDI lead (Function pad 15) moves 1 ms a step: 0-15 ms, default 8 ms
  (was 4 ms a step, 0-60 ms). Older saves are converted (2 steps = 8 ms;
  anything above 15 ms becomes 15 ms). Flag bit 2 of the saved Yummy settings
  (the old metronome flag) now marks the new unit.
- Yummy releases notes held over MIDI on All Notes Off / All Sound Off
  (CC 123 / 120) on its channel; a note left hanging no longer holds back
  saving.
- The knob counts as activity for saving only when it moves by more than 24
  of 4095 (was 8), so a slightly noisy knob can't keep postponing a save.
- Diagnostics (Function + Play): CC 83 = what is holding saving back, as bits
  (0 = nothing): 1 storage unavailable (Step Mother), 2 DAW transport running,
  4 a StepBro note held, 8 Yummy playing, 16 Yummy clock running, 32 Yummy
  notes held, 64 Yummy step audition, 128 a key is down. CC 84 = time since
  the last key / knob / MIDI activity, in 0.1 s. Both decoded by
  tools/decode_diagnostics.py.

# 0.3.4 (Yummy Function page, StepBro gate length back)

- Yummy follows USB MIDI clock only: the internal clock, the Tempo pad and the
  metronome (Function + Wave) are gone. Play mutes / unmutes Yummy on the beat,
  also while the host is stopped (the Play LED blinks dimly: it will start
  muted). Saves made on internal clock simply follow the host now.
- Function page: pad 11 = Swing (was Tempo), 13 = MIDI channel (tap; only the
  channel's pad lights; turn the knob to change it, once moved), 14 = MIDI out
  (dim = on, the default; dark = off; tap toggles), 15 = MIDI lead (tap, then
  turn; the value scrolls in ms, 4 ms a step). Changing the channel or turning
  MIDI out off ends notes still sounding.
- StepBro: Mute + knob sets the pattern's gate length again (5-100% of a step,
  saved per pattern, copied with the slot). It takes effect only once the knob
  moves, so pressing Mute alone changes nothing. Patterns keep the gate they
  had stored (80% unless changed before 0.3.2).

# 0.3.3 (StepBro Play timing, density knob)

- StepBro Play while synced: stopping is immediate, as before. StepBro now
  keeps following the DAW's clock silently while stopped (chains keep their
  place too), and pressing Play again arms the restart: Play blinks, and
  StepBro comes back in on the next beat, on the step the DAW is on, with the
  MIDI lead already working. Pressing Play again while it blinks cancels.
  Pattern changes made while stopped wait for the bar, like while playing.
- StepBro knob on its own: density. Fully right plays every hit; turning left
  drops the weakest first (lowest velocity, then off-beats: each hit scores
  its velocity, +40 on a beat, +15 on an off-beat eighth), down to the
  strongest eighth of the pattern. It takes over only once moved, and only on
  the StepBro level. Muted voices don't count. Patterns are not changed and
  the setting isn't saved (power-up: full).

# 0.3.2 (performance changes, both boards)

- StepBro: the beat repeat (Pattern + knob) is gone; Pattern + knob does nothing.
- StepBro: gate length (Mute + knob) is gone; Mute + knob does nothing. Every
  pattern plays at the default 80% gate, whatever an older save stored (the
  saved field stays, so the save layout is unchanged).
- StepBro Shift + Mute: pad 1 mutes StepBro as before; pad 2 mutes Yummy's MIDI
  notes (sounding notes get their note-off at once, CCs still go out); pad 3
  mutes Yummy's audio with a 5 ms fade (delay echoes included, metronome not).
  The Yummy sequencer keeps running underneath. Muted pads flash. Not saved:
  power-up is unmuted, like the StepBro mutes.
- Yummy MIDI out (channel 2) is on by default. The saved bit now means "MIDI out
  off", so older saves load with it on; turning it off on the Function page is
  remembered. Turning it off, or changing channel, closes sounding notes.
- Yummy: changing the scale moves every programmed note (all 16 patterns,
  chords included) to the same place in the new scale; between scales of
  different sizes, the same share of the octave. Worked out from the melody as
  last edited, so flipping through scales and back restores it exactly; an edit
  or a reboot makes the current notes the new original. Chromatic leaves notes
  as they are; leaving Chromatic snaps them into the new scale.
- RP2040: crash messages no longer use snprintf, which freed about 6 KB of
  RAM (the program runs from RAM) for the above.

# 0.3.1

- No RP2040 changes; version kept in step with the Teensy firmware (0.3.1 fixes
  its USB audio device listing and a MIDI-out race).

# 0.3.0 (one firmware, one version)

- First version in the repository: the RP2040 merged test 0.2.4 and the Teensy
  merged test 0.2.0, otherwise unchanged, now both 0.3.0
  (`src/firmware_version.h`).
- The board shows up as "Step Mother" in both editing levels: USB device,
  MIDI port and audio interface. The version is no longer in the name.
- A version tag in the image (`SSFW|<board>|<version>|`) for the StepDaddy updater.
- Build output named `StepMother2_firmware` (CI adds the version).

# Yummy Pass 3 engine port

- Port the supplied Cutoff, Resonance, Env Mod, Decay, Accent and FX behavior.
- Add independent held Drive editing/pickup while retaining Mix automation.
- Restore constant-time delay reset and compare output to supplied Pass 3.
- Freeze old disk parameter layout; retain Drive in tagged reserved metadata.
- Test real old-save import, upgraded save/reload and old-reader compatibility.
- Preserve board-specific audio, USB, panel, journal and diagnostic behavior.
- Include the exact attached previous board firmware as rollback.
