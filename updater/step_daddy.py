#!/usr/bin/env python3
"""
StepDaddy
=========

One double-clickable tool that installs the firmware on a Step Mother, with
no Arduino IDE and without opening the enclosure. It handles both boards:
the Step Mother 1 (Teensy 4.1, .hex) and the Step Mother 2 (RP2040, .uf2). The
firmware is StepBro and the Yummy Gummy synth in one (Shift + Bank + Play
switches between them).

How it works
------------
StepDaddy works out which board is connected: a Teensy with a USB serial port,
or an RP2040 already in update mode (its RPI-RP2 drive). If it cannot tell, it
asks "Step Mother 1 (Teensy) or Step Mother 2 (RP2040)?".

Teensy: StepDaddy asks the board to restart into its bootloader over USB
(Teensyduino's 134 baud request); otherwise Glide + Function held at power-up
(Shift + Clear on older panels) does it. teensy_loader_cli, inside this
executable, then installs the .hex.

RP2040: Glide + Function held while plugging in USB (Shift + Clear on older
panels) opens the RPI-RP2 drive; StepDaddy copies the .uf2 onto it and the
board restarts by itself. A brand-new board opens RPI-RP2 on its own.

Both firmware images are bundled into the executable at build time, so the
user downloads exactly one file. Run with --help for options.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

APP_NAME = "StepDaddy"
# Overwritten at build time by CI with the firmware version
# (firmware/StepMother/src/firmware_version.h; the RP2040 firmware
# firmware/StepMother2 carries the same version, CI checks that).
FIRMWARE_VERSION = "0.0.0"

MOTHER_FILE = "StepMother_firmware.hex"      # Step Mother 1, Teensy 4.1
MOTHER2_FILE = "StepMother2_firmware.uf2"    # Step Mother 2, RP2040

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


def uf2_image(path: Path) -> bytes:
    """The payload bytes of a .uf2 file, block by block (enough to find tags)."""
    raw = path.read_bytes()
    out = bytearray()
    for off in range(0, len(raw) - 511, 512):
        blk = raw[off:off + 512]
        if blk[0:4] == b"UF2\n" and blk[4:8] == b"WQ]\x9e":
            size = int.from_bytes(blk[16:20], "little")
            if size <= 476:
                out += blk[32:32 + size]
    return bytes(out)


def firmware_tag(path: Path | None) -> tuple[str, str] | None:
    """(board, version) from a .hex, or None if it has no tag."""
    if path is None or not path.exists():
        return None
    suffix = path.suffix.lower()
    if suffix == ".hex":
        img = hex_image(path)
    elif suffix == ".uf2":
        img = uf2_image(path)
    else:
        img = path.read_bytes()
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
    print(f"  {APP_NAME} v{FIRMWARE_VERSION}: firmware for the Step Mother 1 and 2")
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


def print_bootloader_instructions(waiting: str = "StepDaddy") -> None:
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
    print(f"  It is now waiting for {waiting}.")
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
# Step Mother 2 (RP2040): copy the .uf2 onto the RPI-RP2 drive
# ---------------------------------------------------------------------------
def _drive_candidates() -> list[Path]:
    if os.name == "nt":
        return [Path(f"{c}:/") for c in "CDEFGHIJKLMNOPQRSTUVWXYZ"]
    roots = [Path("/Volumes"), Path("/mnt")]
    user = os.environ.get("USER") or os.environ.get("LOGNAME") or ""
    roots += [Path("/media") / user, Path("/run/media") / user, Path("/media")]
    out: list[Path] = []
    for root in roots:
        try:
            out += [d for d in root.iterdir() if d.is_dir()]
        except OSError:
            pass
    return out


def find_rp2040_drive() -> Path | None:
    """The RPI-RP2 drive of an RP2040 in update mode, or None."""
    for d in _drive_candidates():
        try:
            info = d / "INFO_UF2.TXT"
            if info.is_file() and "RPI-RP2" in info.read_text(errors="replace"):
                return d
        except OSError:
            continue
    return None


def flash_step_mother_2(uf2_path: Path, wait_seconds: float = 300.0) -> int:
    print(f"  Installing: {describe(uf2_path)}")
    drive = find_rp2040_drive()
    if drive is None:
        print()
        rule()
        print("  PUT THE CONTROLLER INTO UPDATE MODE")
        rule()
        print()
        print("   1. Unplug the USB cable.")
        print("   2. Hold down GLIDE and FUNCTION together (marked SHIFT and CLEAR")
        print("      on older panels).")
        print("   3. Keeping both held, plug the USB cable back in.")
        print("   4. Let go once a drive called RPI-RP2 appears.")
        print()
        print("  A brand-new board needs no keys: it opens RPI-RP2 by itself.")
        print("  The Step Mother will be dark: that is correct.")
        print()
        print("  Waiting for the RPI-RP2 drive... (Ctrl+C to cancel)")
        print()
        deadline = time.monotonic() + wait_seconds
        while drive is None and time.monotonic() < deadline:
            time.sleep(0.5)
            drive = find_rp2040_drive()
    if drive is None:
        print("The RPI-RP2 drive did not appear.")
        print()
        print("Things to try:")
        print("  - Make sure you held GLIDE + FUNCTION while plugging in.")
        print("  - Try a different USB cable; some are charge-only and carry")
        print("    no data.")
        print("  - Unplug, wait five seconds, and run StepDaddy again.")
        return 1

    print(f"  Found the controller ({drive}). Copying the firmware...")
    copy_error = None
    try:
        shutil.copyfile(uf2_path, drive / uf2_path.name)
    except OSError as exc:
        # The board restarts the moment the last block lands, which some
        # systems (macOS in particular) report as a copy error. It only counts
        # as a failure if the drive is still there afterwards.
        copy_error = exc
    for _ in range(40):
        if find_rp2040_drive() is None:
            break
        time.sleep(0.25)
    else:
        print()
        print("The update did not complete" + (f": {copy_error}" if copy_error else "."))
        print("The RPI-RP2 drive is still there. Unplug, plug in again with")
        print("GLIDE + FUNCTION held, and run StepDaddy again.")
        return 1

    print()
    rule()
    print(f"  DONE. Your Step Mother 2 has {describe(uf2_path)}.")
    rule()
    print()
    print("  The controller restarted by itself. If it does not light up,")
    print("  unplug the USB cable and plug it back in.")
    return 0


def detect_board() -> str | None:
    """"rp2040" for a board in update mode (RPI-RP2 drive), "teensy" for a Teensy
    with a USB serial port, else None."""
    if find_rp2040_drive() is not None:
        return "rp2040"
    if find_teensy_serial_ports():
        return "teensy"
    return None


def ask_board() -> str | None:
    print("  Which board do you have?")
    print()
    print("    1. Step Mother 1 (Teensy): the USB socket is a micro-USB")
    print("    2. Step Mother 2 (RP2040): the USB socket is a USB-C")
    print()
    print("  Look at the USB socket on the back edge of your controller to tell.")
    print()
    while True:
        try:
            answer = input("  Type 1 or 2 and press Enter: ").strip().lower()
        except (EOFError, KeyboardInterrupt):
            return None
        if answer in ("1", "teensy", "mother1"):
            return "teensy"
        if answer in ("2", "rp2040", "mother2"):
            return "rp2040"


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------
def main() -> int:
    parser = argparse.ArgumentParser(
        prog="stepdaddy",
        description=f"{APP_NAME}: installs the firmware on a Step Mother 1 or 2.",
    )
    parser.add_argument(
        "--product",
        choices=("mother", "mother2"),
        help="the board: mother = Step Mother 1 (Teensy), mother2 = Step Mother 2 "
             "(RP2040); by default StepDaddy detects it, or asks",
    )
    parser.add_argument(
        "--firmware",
        type=Path,
        help="install this firmware file (.hex for the Teensy, .uf2 for the RP2040) "
             "instead of the bundled one",
    )
    parser.add_argument("--version", action="store_true", help="print the bundled firmware version and exit")
    args = parser.parse_args()

    if args.version:
        print(FIRMWARE_VERSION)
        return 0

    banner()
    board = None
    if args.product:
        board = "rp2040" if args.product == "mother2" else "teensy"
    elif args.firmware:
        suffix = args.firmware.suffix.lower()
        board = {".uf2": "rp2040", ".hex": "teensy"}.get(suffix)
        if board is None:
            print(f"ERROR: {args.firmware.name}: expected a .hex (Teensy) or .uf2 (RP2040) file.")
            return 1
    else:
        board = detect_board()
        if board:
            print("  Found a Step Mother 2 (RP2040) in update mode." if board == "rp2040"
                  else "  Found a Step Mother 1 (Teensy) over USB.")
            print()
        else:
            board = ask_board()
            print()
            if board is None:
                print("No board chosen.")
                return 1

    name = MOTHER2_FILE if board == "rp2040" else MOTHER_FILE
    firmware = args.firmware or bundled(name)
    if firmware is None:
        print(f"ERROR: {name} is missing from StepDaddy.")
        print("       This is a packaging problem, not something you did wrong.")
        return 1
    if not firmware.exists():
        print(f"ERROR: no such file: {firmware}")
        return 1
    want = ".uf2" if board == "rp2040" else ".hex"
    if firmware.suffix.lower() != want:
        print(f"ERROR: {firmware.name} is not a {want} file, which this board needs.")
        return 1
    tag = firmware_tag(firmware)
    if tag and tag[0] != "Step Mother":
        print(f"ERROR: {firmware.name} is the {tag[0]} firmware, not for the Step Mother.")
        return 1
    if board == "rp2040":
        return flash_step_mother_2(firmware)
    return flash_step_mother(firmware)


if __name__ == "__main__":
    try:
        code = main()
    except KeyboardInterrupt:
        print("\nCancelled.")
        code = 1
    pause_and_exit(code)
