# Step Daddy

Open-source firmware and updater for the **Step Mother** and the **Step Sister**,
23-key USB MIDI controllers. The Step Mother comes with a micro-USB socket
(Teensy 4.1) or a USB-C socket (RP2040); the Step Sister is the USB-C Step
Mother's board on a smaller panel with smaller keys. Their firmware runs **StepBro**, a
MIDI step sequencer, and **Yummy Gummy**, a 303-style bass synth with its own
sequencer that streams 48 kHz audio to the computer over USB.
**Shift + Bank + Play** switches the panel between them; both keep playing.
**StepDaddy**, the updater, installs the firmware without opening the
enclosure. The PCB and enclosure are open hardware too, in the
**[StepMother](https://github.com/AlexKokoschka/StepMother)** repository.

| | Step Mother (USB micro) | Step Mother (USB-C) | Step Sister |
|---|---|---|---|
| MCU | Teensy 4.1, 600 MHz | RP2040, 200 MHz | RP2040, 200 MHz |
| Source | `firmware/StepMother/` | `firmware/StepMother2/` | `firmware/StepMother2/`, built with `-DSTEP_BOARD_NAME="Step Sister"` |
| Built with | Teensyduino 1.62.0 subset (`vendor/`), `tools/build_teensy.py` | Pico SDK 2.1.1, CMake | Pico SDK 2.1.1, CMake |
| USB name (MIDI and audio) | `Step Mother` | `Step Mother` | `Step Sister` |
| Firmware file | `StepMother_firmware_v<version>.hex` | `StepMother2_firmware_v<version>.uf2` | `StepSister_firmware_v<version>.uf2` |
| Installed with | StepDaddy, or Teensy Loader | StepDaddy, or copy onto the RPI-RP2 drive | StepDaddy, or copy onto the RPI-RP2 drive |

The Step Sister runs exactly the Step Mother (USB-C) firmware: same board,
same features, same saved patterns. Only its name differs, so it shows up as
"Step Sister" in your music software.

## Why the RP2040 boards

The Step Mother (USB-C) swaps the Teensy 4.1 for an RP2040 on the same panel
and the same firmware.

- **Easier to source.** The RP2040 is a widely stocked chip, where the Teensy
  module is a single-supplier part.
- **Lower cost.** An RP2040 on the board costs less than a Teensy module.
- **Fully machine-assembled.** The Teensy is a through-hole module that has to
  be soldered by hand. The RP2040 is surface-mounted, so the whole board can go
  through SMD assembly: less time, less cost, fewer soldering errors.
- **USB-C.** The Teensy's micro-USB socket is replaced by a USB-C connector.

The Step Mother (USB micro, Teensy 4.1) stays supported, and one StepDaddy
updates all three boards.

## Where to buy

Step Mothers and Step Sisters are handmade in small batches in Lausanne,
Switzerland: **[stepmother-sequencer.com](https://stepmother-sequencer.com)**.

## Download and install

Get StepDaddy for macOS or Windows from the [Releases](../../releases) page
and open it. It carries the firmware for all three boards and works out which
one you have (or asks). Patterns and settings are kept. Details,
including the one-time security warning for the unsigned apps:
`updater/README.md`.

By hand: hold **Glide + Function** (Shift + Clear on older panels) while
plugging in USB and keep holding for about a second. On the Step Mother (USB
micro) the Teensy bootloader starts: use Teensy Loader with the `.hex`. On the
Step Mother (USB-C) and the Step Sister an RPI-RP2 drive appears: copy that
board's `.uf2` onto it. The check runs at power-up
before anything else, so it works even if the rest of the firmware misbehaves.

## Documentation

- **Video tutorials:** [general introduction](https://youtu.be/5FidxB0JIPQ),
  [beat programming (StepBro)](https://youtu.be/w0sGw5nPOf4) and
  [synth programming (Yummy Gummy)](https://youtu.be/CbF5JtVqbY4).
- **User manuals** (PDF): [StepBro](docs/manuals/StepBro_User_Manual_v1.3.0.pdf)
  and [Yummy Gummy](docs/manuals/Yummy_User_Manual_v1.3.0.pdf).
- **Quick references** (one page each, firmware 1.3.0):
  [StepBro](docs/manuals/StepBro_Quick_Reference_v1.3.0.pdf) and
  [Yummy Gummy](docs/manuals/Yummy_Quick_Reference_v1.3.0.pdf).
- **[Developer guide](docs/DEVELOPER_GUIDE.md)**: changing the firmware,
  writing your own, installing it, and the panel's wiring.
- **Hardware:** the [StepMother](https://github.com/AlexKokoschka/StepMother) repository (PCB and enclosure).

## Layout

```
firmware/StepMother/   Step Mother USB micro (Teensy 4.1) firmware: src/, voice/ (the
                       Yummy synth voice and sequencer), tests/, tools/,
                       vendor/ (Teensyduino 1.62.0 subset, patched at build time)
firmware/StepMother2/  Step Mother USB-C and Step Sister (RP2040) firmware:
                       src/, voice/, tests/, tools/ (built with the Pico SDK,
                       once per board name)
updater/               StepDaddy, the updater (step_daddy.py, its tests, README)
docs/                  user manuals (PDF) and the developer guide
.github/workflows/     CI: builds and tests the firmware, builds StepDaddy
```

## Building

    export ARM_TOOLCHAIN=/path/to/arm-gnu-toolchain-13.2.Rel1-x86_64-arm-none-eabi
    cd firmware/StepMother
    python3 tools/build_teensy.py > build-final.log 2>&1   # build/StepMother_firmware.hex
    sh tests/run.sh > tests-final.log 2>&1
    python3 tools/verify_teensy.py

See `firmware/StepMother/README.md` for details. The RP2040 firmware
(`firmware/StepMother2`) builds with CMake and the Pico SDK; see its README.

## Releasing

1. Set `FIRMWARE_VERSION_STRING` to the same value in
   `firmware/StepMother/src/firmware_version.h` and
   `firmware/StepMother2/src/firmware_version.h` (CI checks they agree) and add
   a CHANGELOG entry to each.
2. Push to `main`. CI builds and tests the firmware, tests StepDaddy, builds it
   for macOS and Windows, and publishes the files to the `builds` branch.
3. Create a GitHub release `v<version>` with the `.hex`, both `.uf2` files, both
   StepDaddy files and the manual PDFs.

## Contributing

Issues and pull requests are welcome. Please run the firmware's host tests
(`sh tests/run.sh`) and the voice tests (`make -C voice test`) first.

## License

Copyright (C) 2026 Alex Kokoschka.

The firmware and updater are free software: you can redistribute them and/or
modify them under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or (at your
option) any later version. See `LICENSE`. The hardware, in the
[StepMother](https://github.com/AlexKokoschka/StepMother) repository, is
licensed under CERN-OHL-S-2.0.

Third-party code (`firmware/StepMother/vendor/`, and the Pico SDK and TinyUSB that
the RP2040 firmware is built against) keeps its own licenses;
see `NOTICE`. "TB-303" is a trademark of Roland Corporation; this project is
not affiliated with or endorsed by Roland.
