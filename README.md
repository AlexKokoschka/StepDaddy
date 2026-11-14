# Step Mother firmware v1.2.0

Built by CI from commit `a1ed7e8` on branch main.
Rewritten on every build; do not edit or commit here by hand.

StepBro and the Yummy Gummy synth in one firmware
(Shift + Bank + Play switches between them).

| Board | File | Installed with |
|---|---|---|
| Step Mother 1 (Teensy 4.1) | `firmware/StepMother_firmware_v1.2.0.hex` | StepDaddy, or Teensy Loader |
| Step Mother 2 (RP2040) | `firmware/StepMother2_firmware_v1.2.0.uf2` | StepDaddy, or copy onto RPI-RP2 (Glide + Function at power-up) |

## StepDaddy (the updater)

| Platform | File |
|---|---|
| macOS (Apple Silicon) | `updater/StepDaddy-macOS-v1.2.0.dmg` |
| Windows (x64) | `updater/StepDaddy-Windows-v1.2.0.exe` |

Both are unsigned, so each OS warns once on first run.
See `updater/README.md` on main for how to get past that.
