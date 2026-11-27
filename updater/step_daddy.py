#!/usr/bin/env python3
"""
StepDaddy
=========

One double-clickable tool that installs the firmware on a Step Mother or a
Step Sister, with no Arduino IDE and without opening the enclosure. It handles
all three boards: the Step Mother (USB micro: Teensy 4.1, .hex), the Step
Mother (USB-C: RP2040, .uf2) and the Step Sister (RP2040, .uf2; the same board
as the Step Mother USB-C on a smaller panel, so the same firmware built under
its own name). The firmware is StepBro and the Yummy Gummy synth in one
(Shift + Bank + Play switches between them).

How it works
------------
StepDaddy works out which board is connected: a Teensy with a USB serial port,
or an RP2040 already in update mode (its RPI-RP2 drive). The RPI-RP2 drive
looks the same for a Step Mother and a Step Sister, so StepDaddy reads the
firmware already on the board (the drive's CURRENT.UF2) to tell which one it
is. If it cannot tell (nothing connected yet, or a blank board), it asks.

Teensy: StepDaddy asks the board to restart into its bootloader over USB
(Teensyduino's 134 baud request); otherwise Glide + Function held at power-up
(Shift + Clear on older panels) does it. teensy_loader_cli, inside this
executable, then installs the .hex.

RP2040: Glide + Function held while plugging in USB (Shift + Clear on older
panels) opens the RPI-RP2 drive; StepDaddy copies the .uf2 onto it and the
board restarts by itself. A brand-new board opens RPI-RP2 on its own.

All three firmware images are bundled into the executable at build time, so the
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

MOTHER_FILE = "StepMother_firmware.hex"      # Step Mother (USB micro), Teensy 4.1
MOTHER2_FILE = "StepMother2_firmware.uf2"    # Step Mother (USB-C), RP2040
SISTER_FILE = "StepSister_firmware.uf2"      # Step Sister, RP2040

# The boards StepDaddy installs on. "name" is the board name in the firmware's
# version tag and USB name; "label" is what StepDaddy calls the board on screen.
PRODUCTS = {
    "mother": {"name": "Step Mother", "label": "Step Mother (USB micro)",
               "file": MOTHER_FILE, "kind": "teensy", "suffix": ".hex"},
    "mother2": {"name": "Step Mother", "label": "Step Mother (USB-C)",
                "file": MOTHER2_FILE, "kind": "rp2040", "suffix": ".uf2"},
    "sister": {"name": "Step Sister", "label": "Step Sister",
               "file": SISTER_FILE, "kind": "rp2040", "suffix": ".uf2"},
}
# The RP2040 boards by the board name in their firmware's tag.
RP2040_BY_NAME = {"Step Mother": "mother2", "Step Sister": "sister"}

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
    return uf2_payload(path.read_bytes())


def uf2_payload(raw: bytes) -> bytes:
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
    return image_tag(img)


def image_tag(img: bytes) -> tuple[str, str] | None:
    """(board, version) from the "SSFW|<board>|<version>|" tag in an image."""
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
    print(f"  {APP_NAME} v{FIRMWARE_VERSION}: firmware for the Step Mother and Step Sister")
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
    print("  The controller will go quiet and dark: that is correct.")
    print(f"  It is now waiting for {waiting}.")
    print()
    print("  Waiting for the controller... (Ctrl+C to cancel)")
    print()


# ---------------------------------------------------------------------------
# Step Mother (USB micro, Teensy 4.1) via teensy_loader_cli
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
    print(f"  DONE. Your Step Mother (USB micro) has {describe(hex_path)}.")
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
# Step Mother (USB-C) and Step Sister (RP2040): copy the .uf2 onto RPI-RP2
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


# How much of CURRENT.UF2 to read for the tag: the firmware sits in the first
# 192 KB of flash (the release check holds it there), 384 KB of UF2 blocks.
CURRENT_UF2_BYTES = 512 * 1024


def installed_firmware(drive: Path) -> tuple[str, str] | None:
    """(board, version) of the firmware already on an RP2040 in update mode,
    read from its drive's CURRENT.UF2 (a copy of the board's flash), or None
    for a blank board or one without a tag."""
    try:
        with open(drive / "CURRENT.UF2", "rb") as f:
            raw = f.read(CURRENT_UF2_BYTES)
    except OSError:
        return None
    return image_tag(uf2_payload(raw))


def confirm(question: str) -> bool:
    try:
        answer = input(f"  {question} Type y and press Enter to go ahead: ").strip().lower()
    except (EOFError, KeyboardInterrupt):
        return False
    return answer in ("y", "yes", "j", "ja", "o", "oui")


def flash_rp2040(uf2_path: Path, product: str = "mother2", wait_seconds: float = 300.0) -> int:
    label = PRODUCTS[product]["label"]
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
        print("  The controller will be dark: that is correct.")
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

    # A Step Mother and a Step Sister are the same board: either firmware runs
    # on either, but under the other's name. Check before overwriting.
    installed = installed_firmware(drive)
    if installed and installed[0] in RP2040_BY_NAME and installed[0] != PRODUCTS[product]["name"]:
        print()
        print(f"  This board has the {installed[0]} firmware (v{installed[1]}), and you are")
        print(f"  installing the {PRODUCTS[product]['name']} firmware. It would work, but the board")
        print(f"  would then call itself {PRODUCTS[product]['name']} in your music software.")
        if not confirm("Install it anyway?"):
            print()
            print("  Nothing was changed. Unplug the board to leave update mode.")
            return 1
        print()

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
    print(f"  DONE. Your {label} has {describe(uf2_path)}.")
    rule()
    print()
    print("  The controller restarted by itself. If it does not light up,")
    print("  unplug the USB cable and plug it back in.")
    return 0


def detect_board() -> str | None:
    """The product key of a connected board, "rp2040" for an RP2040 in update
    mode whose firmware doesn't say which board it is, or None."""
    drive = find_rp2040_drive()
    if drive is not None:
        installed = installed_firmware(drive)
        if installed and installed[0] in RP2040_BY_NAME:
            product = RP2040_BY_NAME[installed[0]]
            print(f"  Found a {PRODUCTS[product]['label']} in update mode "
                  f"(it has firmware v{installed[1]}).")
            print()
            return product
        return "rp2040"
    if find_teensy_serial_ports():
        print("  Found a Step Mother (USB micro) over USB.")
        print()
        return "mother"
    return None


def ask(options: list[tuple[str, str, tuple[str, ...]]]) -> str | None:
    """Asks for one of the options (product key, text, typed aliases)."""
    for i, (_, text, _) in enumerate(options, 1):
        print(f"    {i}. {text}")
    print()
    numbers = ", ".join(str(i) for i in range(1, len(options))) + f" or {len(options)}"
    while True:
        try:
            answer = input(f"  Type {numbers} and press Enter: ").strip().lower()
        except (EOFError, KeyboardInterrupt):
            return None
        for i, (key, _, aliases) in enumerate(options, 1):
            if answer == str(i) or answer in aliases:
                return key


def ask_board() -> str | None:
    print("  Which board do you have?")
    print()
    return ask([
        ("mother", "Step Mother (USB micro): the USB socket is a micro-USB", ("teensy", "mother", "mother1")),
        ("mother2", "Step Mother (USB-C): the USB socket is a USB-C", ("mother2",)),
        ("sister", "Step Sister: the small one, 8.5 x 10.3 cm", ("sister",)),
    ])


def ask_rp2040_board() -> str | None:
    print("  Found a board in update mode with no firmware on it yet.")
    print("  Which board is it?")
    print()
    return ask([
        ("mother2", "Step Mother (USB-C): 16 x 12 cm, full-size keys", ("mother2", "mother")),
        ("sister", "Step Sister: 8.5 x 10.3 cm, small keys", ("sister",)),
    ])


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------
def main() -> int:
    parser = argparse.ArgumentParser(
        prog="stepdaddy",
        description=f"{APP_NAME}: installs the firmware on a Step Mother or a Step Sister.",
    )
    parser.add_argument(
        "--product",
        choices=tuple(PRODUCTS),
        help="the board: mother = Step Mother (USB micro, Teensy), mother2 = Step "
             "Mother (USB-C, RP2040), sister = Step Sister (RP2040); by default "
             "StepDaddy detects it, or asks",
    )
    parser.add_argument(
        "--firmware",
        type=Path,
        help="install this firmware file (.hex for the Teensy, .uf2 for the RP2040 "
             "boards) instead of the bundled one",
    )
    parser.add_argument("--version", action="store_true", help="print the bundled firmware version and exit")
    args = parser.parse_args()

    if args.version:
        print(FIRMWARE_VERSION)
        return 0

    banner()
    product = args.product
    if product is None and args.firmware:
        suffix = args.firmware.suffix.lower()
        if suffix == ".hex":
            product = "mother"
        elif suffix == ".uf2":
            tag = firmware_tag(args.firmware)
            product = RP2040_BY_NAME.get(tag[0], "mother2") if tag else "mother2"
        else:
            print(f"ERROR: {args.firmware.name}: expected a .hex (Teensy) or .uf2 (RP2040) file.")
            return 1
    if product is None:
        product = detect_board()
        if product == "rp2040":
            product = ask_rp2040_board()
            print()
        elif product is None:
            product = ask_board()
            print()
        if product is None:
            print("No board chosen.")
            return 1

    info = PRODUCTS[product]
    firmware = args.firmware or bundled(info["file"])
    if firmware is None:
        print(f"ERROR: {info['file']} is missing from StepDaddy.")
        print("       This is a packaging problem, not something you did wrong.")
        return 1
    if not firmware.exists():
        print(f"ERROR: no such file: {firmware}")
        return 1
    if firmware.suffix.lower() != info["suffix"]:
        print(f"ERROR: {firmware.name} is not a {info['suffix']} file, which the {info['label']} needs.")
        return 1
    tag = firmware_tag(firmware)
    if tag and tag[0] != info["name"]:
        print(f"ERROR: {firmware.name} is the {tag[0]} firmware, not for the {info['label']}.")
        return 1
    if info["kind"] == "rp2040":
        return flash_rp2040(firmware, product)
    return flash_step_mother(firmware)


if __name__ == "__main__":
    try:
        code = main()
    except KeyboardInterrupt:
        print("\nCancelled.")
        code = 1
    pause_and_exit(code)
