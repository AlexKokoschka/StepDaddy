# Step Daddy

Open-source firmware and updater for the **Step Mother**, a 23-key USB MIDI
controller built around a Teensy 4.1. Its firmware runs **StepBro**, a
MIDI step sequencer, and **Yummy Gummy**, a 303-style bass synth with its own
sequencer that streams 48 kHz audio to the computer over USB.
**Shift + Bank + Play** switches the panel between them; both keep playing.
**StepDaddy**, the updater, installs the firmware without opening the
enclosure. The Step Mother's PCB and enclosure are open hardware too, in the
**[StepMother](https://github.com/AlexKokoschka/StepMother)** repository.

| | Step Mother |
|---|---|
| MCU | Teensy 4.1, 600 MHz |
| Built with | Teensyduino 1.62.0 subset (`firmware/StepMother/vendor/`), `tools/build_teensy.py` |
| USB name (MIDI and audio) | `Step Mother` |
| Firmware file | `StepMother_firmware_v<version>.hex` |
| Installed with | StepDaddy, or Teensy Loader |

## Download and install

Get StepDaddy for macOS or Windows from the [Releases](../../releases) page
and open it. It carries the firmware. Patterns and settings are kept. Details,
including the one-time security warning for the unsigned apps:
`updater/README.md`.

By hand: hold **Glide + Function** (Shift + Clear on older panels) while
plugging in USB and keep holding for about a second; the Teensy bootloader
starts. Then use Teensy Loader with the `.hex`. The check runs at power-up
before anything else, so it works even if the rest of the firmware misbehaves.

## Documentation

- **User manuals** (PDF): [StepBro](docs/manuals/StepBro_User_Manual_v1.0.0.pdf)
  and [Yummy Gummy](docs/manuals/Yummy_User_Manual_v1.0.0.pdf).
- **[Developer guide](docs/DEVELOPER_GUIDE.md)**: changing the firmware,
  writing your own, installing it, and the panel's wiring.
- **Hardware:** the [StepMother](https://github.com/AlexKokoschka/StepMother) repository (PCB and enclosure).

## Layout

```
firmware/StepMother/   the firmware: src/, voice/ (the Yummy synth voice and
                       sequencer), tests/, tools/, vendor/ (Teensyduino 1.62.0
                       subset, patched at build time)
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

See `firmware/StepMother/README.md` for details.

## Releasing

1. Set `FIRMWARE_VERSION_STRING` in `firmware/StepMother/src/firmware_version.h`
   and add a CHANGELOG entry.
2. Push to `main`. CI builds and tests the firmware, tests StepDaddy, builds it
   for macOS and Windows, and publishes the files to the `builds` branch.
3. Create a GitHub release `v<version>` with the `.hex`, both StepDaddy files
   and the two manual PDFs.

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

Third-party code under `firmware/StepMother/vendor/` keeps its own licenses;
see `NOTICE`. "TB-303" is a trademark of Roland Corporation; this project is
not affiliated with or endorsed by Roland.
