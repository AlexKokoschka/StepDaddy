#!/usr/bin/env python3
"""
StepDaddy
=========

One double-clickable tool that installs the firmware on a Step Mother
(Teensy 4.1), with no Arduino IDE and without opening the enclosure. The
firmware is StepBro and the Yummy Gummy synth in one (Shift + Bank + Play
switches between them).

How it works
------------
StepDaddy first asks a Teensy that has a USB serial port (this firmware does,
and so does a Teensy running Teensyduino code) to restart into its
bootloader; otherwise Glide + Function held at power-up (Shift + Clear on
older panels) does it. teensy_loader_cli, inside this executable, then
installs the .hex.

The firmware image is bundled into the executable at build time, so the user
downloads exactly one file. Run with --help for options.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

APP_NAME = "StepDaddy"
# Overwritten at build time by CI with the firmware version
# (firmware/StepMother/src/firmware_version.h).
FIRMWARE_VERSION = "0.0.0"

MOTHER_FILE = "StepMother_firmware.hex"

TEENSY_MCU = "TEENSY41"


# ---------------------------------------------------------------------------
# Bundled-resource handling
# ---------------------------------------------------------------------------
def resource_dir() -> Path:
    """Where bundled files live, frozen (PyInstaller) or running from source."""
    if getattr(sys, "frozen", False):
        return Path(sys._MEIPASS)  # type: ignore[attr-defined]
    return Path(__file__).resolve().parent


def bundled(name: str) -> Path | None:
    p = resource_dir() / name
    return p if p.exists() else None


def teensy_loader_path() -> Path | None:
    for candidate in ("teensy_loader_cli.exe", "teensy_loader_cli"):
        p = bundled(candidate)
        if p:
            if os.name != "nt":
                try:
                    p.chmod(0o755)
                except OSError:
                    pass
            return p
    return shutil.which("teensy_loader_cli") and Path(shutil.which("teensy_loader_cli"))  # type: ignore[arg-type]


# ---------------------------------------------------------------------------
# Which version a firmware file holds: its "SSFW|<board>|<version>|" tag
# ---------------------------------------------------------------------------
def hex_image(path: Path) -> bytes:
    """The data bytes of an Intel .hex file, in file order (enough to find tags)."""
    out = bytearray()
    for line in path.read_text(encoding="ascii", errors="replace").splitlines():
        if line.startswith(":") and len(line) >= 11 and line[7:9] == "00":
            try:
                out += bytes.fromhex(line[9:9 + 2 * int(line[1:3], 16)])
            except ValueError:
                pass
    return bytes(out)


def firmware_tag(path: Path | None) -> tuple[str, str] | None:
    """(board, version) from a .hex, or None if it has no tag."""
    if path is None or not path.exists():
        return None
    img = hex_image(path) if path.suffix.lower() == ".hex" else path.read_bytes()
    i = img.find(b"SSFW|")
    if i < 0:
        return None
    fields = img[i + 5:i + 80].split(b"|")
    if len(fields) < 3:
        return None
    return fields[0].decode("ascii", "replace"), fields[1].decode("ascii", "replace")


def describe(path: Path | None) -> str:
    tag = firmware_tag(path)
    return f"{tag[0]} firmware v{tag[1]}" if tag else (path.name if path else "?")


# ---------------------------------------------------------------------------
# Small console helpers
# ---------------------------------------------------------------------------
def rule(char: str = "-") -> None:
    print(char * 68)


def banner() -> None:
    rule("=")
    print(f"  {APP_NAME} v{FIRMWARE_VERSION}: firmware for the Step Mother")
    print("  (StepBro and the Yummy Gummy synth in one)")
    rule("=")
    print()


def pause_and_exit(code: int) -> None:
    """Keep the window open when double-clicked from a GUI file manager."""
    print()
    if sys.stdin is not None and sys.stdin.isatty():
        try:
            input("Press Enter to close this window.")
        except (EOFError, KeyboardInterrupt):
            pass
    sys.exit(code)


def print_bootloader_instructions() -> None:
    print()
    rule()
    print("  PUT THE CONTROLLER INTO UPDATE MODE")
    rule()
    print()
    print("   1. Unplug the USB cable.")
    print("   2. Hold down GLIDE and FUNCTION together (marked SHIFT and CLEAR")
    print("      on older panels).")
    print("   3. Keeping both held, plug the USB cable back in.")
    print("   4. Keep holding for about one more second, then let go.")
    print()
    print("  The Step Mother will go quiet and dark: that is correct.")
    print("  It is now waiting for StepDaddy.")
    print()
    print("  Waiting for the controller... (Ctrl+C to cancel)")
    print()


# ---------------------------------------------------------------------------
# Step Mother (Teensy 4.1) via teensy_loader_cli
# ---------------------------------------------------------------------------
TEENSY_VID = 0x16C0   # PJRC's USB vendor ID


def find_teensy_serial_ports() -> list[str]:
    """Serial ports of Teensys with a USB serial port: a Step Mother running
    this firmware (USB type MIDI + audio + serial), StepBro built on its own,
    or a Teensy running Teensyduino code such as PJRC's blink test."""
    try:
        from serial.tools import list_ports
    except ImportError:
        return []
    try:
        return [p.device for p in list_ports.comports() if p.vid == TEENSY_VID]
    except Exception:
        return []


def request_teensy_reboot(port: str) -> bool:
    """Teensyduino's USB Serial restarts the Teensy into its bootloader when the
    computer sets the line to 134 baud: how the Arduino IDE uploads without the
    button being pressed. A Step Mother in a closed enclosure then updates with
    no keys or buttons at all."""
    try:
        import serial
    except ImportError:
        return False
    try:
        serial.Serial(port, 134).close()
        return True
    except Exception:
        return False


def flash_step_mother(hex_path: Path) -> int:
    loader = teensy_loader_path()
    if loader is None:
        print("ERROR: teensy_loader_cli is missing from StepDaddy.")
        print("       This is a packaging problem, not something you did wrong.")
        return 1

    print(f"  Installing: {describe(hex_path)}")
    asked = [p for p in find_teensy_serial_ports() if request_teensy_reboot(p)]
    if asked:
        print()
        print("  Found the controller over USB: asking it to restart into update")
        print("  mode, no buttons needed...")
        print()
        print("  If nothing happens within about ten seconds, do it by hand:")
    print_bootloader_instructions()

    # -w waits for the board to appear in bootloader mode instead of failing
    # immediately; -v prints progress. teensy_loader_cli blocks until it sees
    # a board, so this is the "wait for the user" step as well as the flash.
    cmd = [str(loader), f"--mcu={TEENSY_MCU}", "-w", "-v", str(hex_path)]
    try:
        result = subprocess.run(cmd)
    except KeyboardInterrupt:
        print("\nCancelled.")
        return 1
    except OSError as exc:
        print(f"ERROR: could not run the Teensy loader: {exc}")
        return 1

    if result.returncode != 0:
        print()
        print("The update did not complete.")
        print()
        print("Things to try:")
        print("  - Make sure you held GLIDE + FUNCTION while plugging in.")
        print("  - Try a different USB cable; some are charge-only and carry")
        print("    no data.")
        print("  - Unplug, wait five seconds, and run StepDaddy again.")
        print("  - A Teensy that has never had this firmware may need its small")
        print("    PROGRAM button pressed once.")
        return result.returncode

    print()
    rule()
    print(f"  DONE. Your Step Mother has {describe(hex_path)}.")
    rule()
    print()
    print("  ONE LAST STEP: unplug the USB cable and plug it back in.")
    print()
    print("  The controller may look dead until you do: no lights, and your")
    print("  computer may not list it. That is expected. Updating makes the")
    print("  device disappear and reappear on the USB bus several times in a")
    print("  couple of seconds, and computers do not always keep up. A replug")
    print("  settles it immediately, and nothing has gone wrong.")
    return 0


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------
def main() -> int:
    parser = argparse.ArgumentParser(
        prog="stepdaddy",
        description=f"{APP_NAME}: installs the firmware on a Step Mother.",
    )
    parser.add_argument(
        "--product",
        choices=("mother",),
        help="the product to update (only the Step Mother for now)",
    )
    parser.add_argument(
        "--firmware",
        type=Path,
        help="install this firmware file (.hex) instead of the bundled one",
    )
    parser.add_argument("--version", action="store_true", help="print the bundled firmware version and exit")
    args = parser.parse_args()

    if args.version:
        print(FIRMWARE_VERSION)
        return 0

    banner()
    firmware = args.firmware or bundled(MOTHER_FILE)
    if firmware is None:
        print(f"ERROR: {MOTHER_FILE} is missing from StepDaddy.")
        print("       This is a packaging problem, not something you did wrong.")
        return 1
    if not firmware.exists():
        print(f"ERROR: no such file: {firmware}")
        return 1
    tag = firmware_tag(firmware)
    if tag and tag[0] != "Step Mother":
        print(f"ERROR: {firmware.name} is the {tag[0]} firmware, not for the Step Mother.")
        return 1
    return flash_step_mother(firmware)


if __name__ == "__main__":
    try:
        code = main()
    except KeyboardInterrupt:
        print("\nCancelled.")
        code = 1
    pause_and_exit(code)
