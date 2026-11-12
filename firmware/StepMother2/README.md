# Step Mother 2 (RP2040) firmware 1.1.0

StepBro and Yummy Gummy in one firmware, for the Step Mother 2 (RP2040,
200 MHz, 48 kHz USB audio). Shift + Bank + Play switches the panel between
StepBro and Yummy; both keep playing. The version is in
`src/firmware_version.h`, shared with the Step Mother 1 (Teensy 4.1: same
firmware, same version). History: `CHANGELOG.md`.

## Install

With **StepDaddy** (the updater): choose Step Mother 2 (RP2040) and follow it, or let it detect the board. It asks for
**Glide + Function** held while plugging in USB (Shift + Clear on older
panels), which opens the RPI-RP2 drive, then copies the firmware on and checks
it arrived. By hand: same key combination, then copy
`StepMother2_firmware_v<version>.uf2` onto RPI-RP2. A brand-new board with
nothing installed opens RPI-RP2 by itself when plugged in.

The board then shows up as **Step Mother** (MIDI port and 48 kHz audio
interface), in both editing levels. Open/monitor the audio input before
playback, enable MIDI clock/Sync and start the host. StepBro sends channel 1;
Yummy receives channel 2 by default.

An update writes only the application and leaves the two save banks at
0x30000/0x38000 intact. Play at power-up is not a chooser.
Erasing the whole flash, or another firmware, can overwrite the saves.

The linked static margin is about 12.5 KiB, with separate 4 KiB stacks
reserved for both cores (the build checks at least 12 KiB; the whole program
runs from RAM, so code size counts). Built with ARM's GNU 13.2.Rel1; Ubuntu's
build of the same compiler makes code about 700 bytes larger, enough to fail
that check. Cold serialization functions were compiled for size; audio
processing retains its existing optimization settings. Stack watermarks and
real CPU load still need a fresh hardware capture.

## Updated Yummy sound engine

Ports the uploaded Yummy Pass 3 Clean engine: revised Cutoff/Env Mod mappings,
smoothed Resonance and Decay, persistent Accent states and excitation before
FX, upper-resonance taper/damping, rational soft clipping and sine shaping,
and revised chord drive/level compensation. The portable DSP matches the supplied
source except for restoring the already-tested constant-time delay-history reset.
Sequencer code is unchanged from the baseline and the supplied engine.

## Distortion controls

On Yummy's Function page:

- Tap pad 8 (Distortion), then turn the knob: **Mix**, the dry/wet amount.
- Hold pad 8 and turn the knob: **Drive**, independently of Mix.
- Hold pad 8 and tap pad 1 or 2: rational soft clipping or sine shaping.
- Both Mix and Drive use pickup. Cross the current value before editing.
- Releasing pad 8 restores Mix pickup. The Drive value bar appears while turning
  the held control; the knob marker blinks until pickup.
- Double-tapping still arms Mix recording. Holding/turning Drive or choosing a
  type does not count as a recording tap. There is no new Drive automation lane
  or MIDI CC. Drive and Mix are global sound controls.

For four-note chords the supplied engine recommends trying Drive at 20–35%
and setting Mix to taste. The old distortion mode numbers are retained, but
old tube/Rat presets now sound like the new clipping/sine modes. Existing knob
positions, patterns and automation load, but their tone follows the new mappings.

## Saving and compatibility

The previous combined record layout is retained: StepBro 16,456 bytes, Yummy
4,280 bytes, four metadata bytes; total 20,740 bytes. The runtime voice parameter
struct gains Drive, but a dedicated stable disk struct keeps all old field offsets.
No original pattern, automation or MIDI-lead fields are moved.

Metadata is now: MIDI lead, extension tag 0xD3, low/high bytes of normalized
16-bit Drive. Older records have three zero reserved bytes and load Drive at
EXACTLY 50%. New Drive saves use 65,536 levels (finer than the panel knob).
A 50% saved value reloads within 0.000008 of that setting due to quantization.
There is no manual migration step. The next stopped-only save writes the extension.

Actual old v0.2.3 adapters/journal were used to generate a legacy fixture and
then tested reading a new Drive-tagged record. Both directions passed. Old
firmware ignores Drive and can still load patterns/settings. If the old firmware
saves again, it drops the Drive extension; the new build then defaults Drive to
50%. Unknown extension tags are rejected before changing either engine.
Compatibility applies to the combined merged save format, not standalone saves.

## Fixes and features retained

- LED 7/14 post-wake driver initialization and read-back checks.
- Delay history invalidation instead of a 96 KiB clear on activation; separate
  checks for both interpolation taps; rapid off/on tail behavior retained.
- Combined stopped-only CRC journal, original flash/file locations and controls.
- CPU, stage timing, resettable peaks, stack and error diagnostics.
- Modifier ordering, held-note cleanup and six orders of Shift + Bank + Play.
- Four-note polyphony, full delay length and all StepBro A features (0.3.2:
  beat repeat and gate length removed).
- Both engines keep playing when switching panel levels; streams B/C remain absent.

The input Pass 3 firmware was RAM-only. Its older LED/storage/platform code was
NOT substituted for the fixed merged board backends.

## First hardware checks

1. Confirm old combined patterns/settings restore. Check Drive starts at 50% on
   the first upgrade; edit it, stop all playback, save, restart and check retention.
2. Test Cutoff, Resonance, Env Mod, Decay, Accent and both distortion types with
   mono notes and four-note chords. Check Mix/Drive independence and pickup.
3. Start both engines with host clock; switch levels and check notes/automation.
4. Reset timing with Yummy Function + Accent + Play; wait two seconds. Record
   Function + Play into a fresh MIDI clip after a heavy session and export it.
5. Repeat Delay Amount off for three seconds then on, without resetting between
   the separate off/on diagnostic captures. Listen for clicks or stale echoes.
6. Compare CPU peaks, missing samples, late USB indications and queue/save errors.
   Performance and stack margins must be measured again for this changed engine.

MIDI export decoder (for remapped/collapsed DAW exports):

```
python3 tools/decode_diagnostics.py capture.mid --channel 1 --flattened
```

Use separate clips per phase. Omit the options for original channel-16 captures.
These builds are compiled/host-tested candidates, not physically verified here.
The previous baseline's hardware results do not validate the updated DSP load.

## Build and tests

Pico SDK 2.1.1, TinyUSB pin 86ad6e56c1700e85f1c5678607a762cfe3aa2f47,
Arm GNU 13.2.Rel1. The build stays below 0x30000, all runtime code/constants
remain in RAM for asynchronous flash access, and the original blocking SDK
range-save APIs remain absent. Pattern/automation retention choices are unchanged.

```
export PICO_SDK_PATH=/path/to/pico-sdk-2.1.1
export PICO_TOOLCHAIN_PATH=/path/to/arm-gnu-toolchain-13.2.Rel1-x86_64-arm-none-eabi
cd firmware/StepMother2
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4 > build-final.log 2>&1
sh tests/run.sh > tests-final.log 2>&1
python3 tools/verify_release.py
```

The source tests include the supplied six-knob/Drive stress tests and new save
compatibility checks, plus the original journal, LED and timing regressions.
