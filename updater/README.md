# StepDaddy

The updater: one double-clickable tool that installs the firmware on a
**Step Mother**, with no Arduino IDE and without opening the enclosure. The
firmware is StepBro and the Yummy Gummy synth in one; StepDaddy carries the
image (`StepMother_firmware.hex`), so a customer downloads exactly one file.

## What the customer does

Open StepDaddy.

If the board is running this firmware (or anything else with
a Teensy USB serial port), StepDaddy asks it over USB to restart into the
Teensy bootloader: no keys or buttons. Otherwise it asks for GLIDE + FUNCTION
while plugging in. Then **unplug and replug** when it says so.

Patterns and settings are kept. Each firmware file carries a version tag
(`SSFW|<board>|<version>|`), which StepDaddy shows, and it refuses a file
meant for another board.

## Step Mother without buttons

Before anything else, StepDaddy looks for a Teensy with a USB serial port
(PJRC's vendor ID, 0x16C0) and opens it at **134 baud**. Teensyduino's USB
Serial treats that as "restart into the bootloader", which is how the Arduino
IDE uploads without the program button being pressed. The Step Mother firmware
has a USB serial port (USB type MIDI + audio + serial), so a board running it
updates with no keys at all. Needs `pyserial` (bundled in the executables;
`pip install pyserial` when running from source).

**Known gap:** on a brand-new Teensy (PJRC's blink test) this didn't work in
a test in Oct 2026; the PROGRAM button was still needed for that first
install. To be looked into.

## Why Step Mother needs a replug afterwards

Confirmed on hardware: after a successful update the Step Mother can sit dark
and unlisted until the USB cable is unplugged and reconnected. Nothing has
failed — the firmware is already written.

Updating makes the device disappear and reappear on the USB bus three times in
about two seconds (running firmware → HalfKay bootloader → new firmware), and
hosts do not reliably re-enumerate through that. It is a host-side USB
behaviour rather than a firmware defect, which is why a replug fixes it
instantly and why there is nothing to fix in the sketch.

StepDaddy states this as an explicit final step instead of claiming the
device is ready to use.

## Known first-run warnings

The executables are **not code-signed**, so each OS warns once:

- **macOS** — "cannot be opened because it is from an unidentified developer."
  Right-click the app → **Open** → **Open**. Only needed the first time.
  The clean fix is an Apple Developer account (~$99/yr) and notarization.
- **Windows** — "Windows protected your PC." Click **More info** →
  **Run anyway**. The clean fix is a code-signing certificate.

Both require credentials that can only be bought in your name, so CI cannot
sign for you. Once you have them, add them as repository secrets and the
signing step slots into `.github/workflows/build.yml`.

Some antivirus products also flag PyInstaller executables on heuristics — worth
testing against a couple before a wide release.

## Architecture note

The macOS build is **Apple Silicon (arm64)**, matching the runner it is built
on. If you need an Intel Mac build, add `macos-13` to the workflow matrix.

## Running from source

No build needed if Python 3.9+ is present:

```bash
python3 step_daddy.py --firmware path/to/StepMother_firmware_v1.0.0.hex
python3 step_daddy.py                     # with the bundled file
```

`--firmware` overrides the bundled image, which is handy for testing a local
build before cutting a release. Running from source, the bundled file is the
one next to the script (`StepMother_firmware.hex`), and `teensy_loader_cli`
must be on `PATH`.

`test_step_daddy.py` runs the update flow against a simulated Teensy loader
with the image CI just built (CI runs it on every build).

## Rebuilding

CI builds StepDaddy on every push to `main` and publishes
`StepDaddy-macOS-v<version>.dmg` and `StepDaddy-Windows-v<version>.exe` on the
`builds` branch. Its version is the firmware's
(`firmware/StepMother/src/firmware_version.h`), so every firmware release gets a new
StepDaddy.
