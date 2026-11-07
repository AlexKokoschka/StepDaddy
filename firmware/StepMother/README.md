# Step Mother firmware

StepBro (MIDI step sequencer) and the Yummy Gummy synth in one firmware, for
the Step Mother (Teensy 4.1, 600 MHz, native 48 kHz USB audio/MIDI).
**Shift + Bank + Play** switches the panel between StepBro and Yummy; both keep
playing. The version is in `src/firmware_version.h`. History: `CHANGELOG.md`.

## Install

With **StepDaddy** (see `updater/README.md`): choose Step Mother. If the board
is already running this firmware, StepDaddy asks it over USB to restart into
the Teensy bootloader (the firmware has a USB serial port for that), so no keys
are needed. Otherwise it asks for **Glide + Function** held while plugging in
(Shift + Clear on older panels). A Teensy that has never had this firmware
needs its PROGRAM button pressed once. By hand: Teensy Loader with
`StepMother_firmware_v<version>.hex`, Auto on, then the same key combination
or the button. Unplug and replug when the update is done.

The board shows up as **Step Mother** (MIDI port and 48 kHz audio interface).
Open/monitor the audio input so Yummy's sample clock advances. Enable MIDI
clock/Sync; StepBro sends on channel 1 and Yummy receives on channel 2 by
default. For a normal unlocked Teensy 4.1 only.

## Distortion controls

On Yummy's Function page:

- Tap pad 8 (Distortion), then turn the knob: **Mix**, the dry/wet amount.
- Hold pad 8 and turn the knob: **Drive**, independently of Mix.
- Hold pad 8 and tap pad 1 or 2: rational soft clipping or sine shaping.
- Mix and Drive use pickup: cross the current value before it changes.
- Double-tapping pad 8 arms Mix recording. Drive is a global sound setting,
  not recorded. For four-note chords try Drive at 20–35%.

## Saving

Program flash holds a 1 MiB LittleFS partition with the two journal files
`/merged-v1-a.bin` and `/merged-v1-b.bin`. A partition that fails to mount is
formatted when it is blank or holds no littlefs superblock in its first two
blocks (leftovers from other firmware); one with a superblock is kept and
saving stays off (diagnostics CC 85 = 1). Saving is stopped-only: Teensy flash
writes can block interrupts and USB briefly. See `SAVE_COMPATIBILITY.md`.

## Diagnostics

Yummy **Function + Play** sends a report as MIDI on channel 16 (CPU as a note:
pitch = average %, velocity = peak %, plus CCs); **Function + Accent + Play**
resets the peaks. Board-specific CCs:

- CC 20/52: untouched space in the 8 KiB shared main/IRQ stack; CC 21/53 is 0
  (one core).
- CC 30/62: the DTCM gap outside that stack, in 16-byte units (a layout budget,
  not live heap).
- CC 82/114: the longest gap between main-loop passes since power-up, in
  0.1 ms. Above about 50 (5 ms), Yummy's USB audio ran dry at least once
  (saves while stopped are expected to be long).
- CC 83/115: what holds saving back, as bits (0 = nothing); CC 84/116: time
  since the last key, knob or MIDI activity, in 0.1 s; CC 85: storage state.

```
python3 tools/decode_diagnostics.py capture.mid                          # channel-16 capture
python3 tools/decode_diagnostics.py capture.mid --channel 1 --flattened  # remapped DAW export
```

## Build and tests

`vendor/` holds the PJRC Teensyduino 1.62.0 subset the build needs (core,
LittleFS, SPI, Wire; see `NOTICE` at the top of the repo). The builder copies
and patches it locally; an installed Arduino is not needed or touched. Use
ARM's GNU 13.2.Rel1:

```
export ARM_TOOLCHAIN=/path/to/arm-gnu-toolchain-13.2.Rel1-x86_64-arm-none-eabi
cd firmware/StepMother
python3 tools/build_teensy.py > build-final.log 2>&1   # build/StepMother_firmware.hex
sh tests/run.sh > tests-final.log 2>&1
python3 tools/verify_teensy.py
```

The verifier checks the HEX (addresses, checksums, flash boundaries), both USB
configurations (48 kHz descriptors, 192-byte audio endpoints), the USB name,
the version tag and the memory guards. The host tests also run the Teensy audio
service and file backend, including 550 simulated journal-task interruptions.
