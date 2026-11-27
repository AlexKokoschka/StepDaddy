#!/usr/bin/env python3
"""Tests StepDaddy against a simulated Teensy loader, with the real firmware
file CI just built.

usage: test_step_daddy.py StepMother_firmware.hex StepMother2_firmware.uf2 StepSister_firmware.uf2
"""
import builtins, io, os, shutil, sys, tempfile
from contextlib import redirect_stdout
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import step_daddy as sd

fails = 0
def check(cond, msg):
    global fails
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond:
        fails += 1


def run(argv, env, answers=()):
    """Runs StepDaddy's main() with arguments, environment and typed answers."""
    it = iter(answers)
    old_input, old_argv, old_env = builtins.input, sys.argv, dict(os.environ)
    builtins.input = lambda prompt="": (print(prompt, end=""), next(it))[1]
    sys.argv = ["stepdaddy", *argv]
    os.environ.update(env)
    out = io.StringIO()
    try:
        with redirect_stdout(out):
            code = sd.main()
    finally:
        builtins.input, sys.argv = old_input, old_argv
        os.environ.clear(); os.environ.update(old_env)
    return code, out.getvalue()


def main():
    mother, mother2, sister = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    res = Path(tempfile.mkdtemp())
    shutil.copy(mother, res / sd.MOTHER_FILE)
    shutil.copy(mother2, res / sd.MOTHER2_FILE)
    shutil.copy(sister, res / sd.SISTER_FILE)
    sd.resource_dir = lambda: res

    # 1. The version tag in the real image
    mt = sd.firmware_tag(res / sd.MOTHER_FILE)
    print(f"bundled: {mt}")
    check(mt and mt[0] == "Step Mother" and mt[1], "version tag read from the image")

    m2 = sd.firmware_tag(res / sd.MOTHER2_FILE)
    print(f"bundled RP2040: {m2}")
    check(m2 and m2[0] == "Step Mother" and m2[1] == mt[1], "RP2040 .uf2 carries the same version tag")
    ss = sd.firmware_tag(res / sd.SISTER_FILE)
    print(f"bundled Step Sister: {ss}")
    check(ss and ss[0] == "Step Sister" and ss[1] == mt[1], "Step Sister .uf2 carries its own name and the same version")

    # 2. A firmware file for another board is refused
    other = res / "other.hex"
    tag = b"SSFW|Other Board|1.0.0|"
    other.write_text("".join(f":{len(c):02X}{i:04X}00{c.hex().upper()}00\n"
                             for i, c in ((i, tag[i:i + 16]) for i in range(0, len(tag), 16))))
    code, out = run(["--firmware", str(other)], {})
    check(code == 1 and "not for the Step Mother (USB micro)" in out, "a firmware file for another board is refused")

    # 3. Step Mother 1: a Teensy with a serial port is asked to restart at 134
    #    baud, then the loader installs; without one, the key instructions
    real = (sd.find_teensy_serial_ports, sd.request_teensy_reboot, sd.teensy_loader_path, sd.subprocess.run)
    asked, ran = [], []
    class Done: returncode = 0
    sd.teensy_loader_path = lambda: Path("/fake/teensy_loader_cli")
    sd.subprocess.run = lambda cmd, **kw: ran.append(cmd) or Done()
    def scenario(ports, ok=True):
        asked.clear(); ran.clear()
        sd.find_teensy_serial_ports = lambda: list(ports)
        sd.request_teensy_reboot = lambda p: asked.append(p) or ok
        return run(["--product", "mother"], {})
    code, out = scenario(["/dev/cu.usbmodem1234"])
    check(code == 0 and asked == ["/dev/cu.usbmodem1234"] and "no buttons needed" in out
          and ran and ran[0][-1] == str(res / sd.MOTHER_FILE) and "-w" in ran[0]
          and f"Step Mother firmware v{mt[1]}" in out,
          "Step Mother: asked to restart over USB, then installed")
    code, out = scenario([])
    check(code == 0 and not asked and "no buttons needed" not in out and "GLIDE and FUNCTION" in out and ran,
          "Step Mother: no serial Teensy: Glide + Function instructions")
    code, out = scenario(["/dev/cu.usbmodem1234"], ok=False)
    check(code == 0 and "no buttons needed" not in out and "GLIDE and FUNCTION" in out,
          "Step Mother: a failed request falls back to the instructions")
    (sd.find_teensy_serial_ports, sd.request_teensy_reboot, sd.teensy_loader_path, sd.subprocess.run) = real
    check(isinstance(sd.find_teensy_serial_ports(), list) and sd.request_teensy_reboot("/nonexistent/port") is False,
          "Step Mother: real port search / request are safe with no Teensy (or no pyserial)")

    # 4. Step Mother USB-C (RP2040): detection, the question, and the copy onto RPI-RP2
    drive = Path(tempfile.mkdtemp())
    (drive / "INFO_UF2.TXT").write_text("UF2 Bootloader v3.0\nModel: Raspberry Pi RP2\nBoard-ID: RPI-RP2\n")
    real = (sd.find_rp2040_drive, sd.find_teensy_serial_ports, sd.time.sleep)
    sd.time.sleep = lambda s: None
    sd.find_teensy_serial_ports = lambda: []

    def present_then_gone():
        """The drive is there until the .uf2 has landed, then the board restarts."""
        def f():
            return None if (drive / sd.MOTHER2_FILE).exists() else drive
        return f

    def cleanup():
        for f in drive.glob("*.uf2"):
            f.unlink()

    # drive already present: auto-detected, copied, success
    cleanup()
    (drive / sd.MOTHER2_FILE).unlink(missing_ok=True)
    sd.find_rp2040_drive = present_then_gone()
    shutil.copy(mother2, drive / "CURRENT.UF2")   # the board already runs Step Mother firmware
    code, out = run([], {})
    check(code == 0 and "Found a Step Mother (USB-C) in update mode" in out and (drive / sd.MOTHER2_FILE).exists()
          and f"Step Mother (USB-C) has Step Mother firmware v{m2[1]}" in out,
          "Step Mother 2: RPI-RP2 drive detected, .uf2 copied")
    check((drive / sd.MOTHER2_FILE).read_bytes() == mother2.read_bytes(), "Step Mother 2: the copied file is the bundled image")

    # drive appears after the instructions
    cleanup()
    appear = iter([None, None, drive])
    state = {"copied": False}
    def late():
        if (drive / sd.MOTHER2_FILE).exists():
            return None
        return next(appear, drive)
    sd.find_rp2040_drive = late
    code, out = run(["--product", "mother2"], {})
    check(code == 0 and "GLIDE and FUNCTION" in out and (drive / sd.MOTHER2_FILE).exists(),
          "Step Mother 2: instructions shown, drive waited for, .uf2 copied")

    # drive never appears: fails cleanly
    cleanup()
    sd.find_rp2040_drive = lambda: None
    real_wait = sd.flash_rp2040
    sd.flash_rp2040 = lambda p, product="mother2", wait_seconds=0.0: real_wait(p, product, wait_seconds=0.0)
    code, out = run(["--product", "mother2"], {})
    sd.flash_rp2040 = real_wait
    check(code == 1 and "did not appear" in out, "Step Mother 2: no drive: clear failure")

    # drive stays after the copy: reported as failed
    cleanup()
    sd.find_rp2040_drive = lambda: drive
    code, out = run(["--product", "mother2"], {})
    check(code == 1 and "still there" in out, "Step Mother 2: drive still present after copy is a failure")

    # Step Sister: recognised by the firmware already on it
    cleanup()
    shutil.copy(sister, drive / "CURRENT.UF2")
    sd.find_rp2040_drive = lambda: None if (drive / sd.SISTER_FILE).exists() else drive
    code, out = run([], {})
    check(code == 0 and "Found a Step Sister in update mode" in out and (drive / sd.SISTER_FILE).exists()
          and not (drive / sd.MOTHER2_FILE).exists() and f"Step Sister has Step Sister firmware v{ss[1]}" in out,
          "Step Sister: recognised from CURRENT.UF2, the Step Sister .uf2 copied")
    check((drive / sd.SISTER_FILE).read_bytes() == sister.read_bytes(), "Step Sister: the copied file is the bundled image")

    # a blank RP2040 board: asked Step Mother or Step Sister
    cleanup(); (drive / "CURRENT.UF2").write_bytes(b"\xff" * 4096)
    code, out = run([], {}, answers=["2"])
    check(code == 0 and "no firmware on it yet" in out and (drive / sd.SISTER_FILE).exists(),
          "Blank RP2040 board: asks, answer 2 installs the Step Sister firmware")
    cleanup()
    sd.find_rp2040_drive = present_then_gone()
    code, out = run([], {}, answers=["1"])
    check(code == 0 and (drive / sd.MOTHER2_FILE).exists(), "Blank RP2040 board: answer 1 installs the Step Mother firmware")

    # the wrong RP2040 firmware for the board on it: asked first, no by default
    cleanup(); shutil.copy(sister, drive / "CURRENT.UF2")
    code, out = run(["--product", "mother2"], {}, answers=[""])
    check(code == 1 and "has the Step Sister firmware" in out and not list(drive.glob("Step*.uf2")),
          "Step Sister board, Step Mother firmware chosen: asked, nothing copied without a yes")
    code, out = run(["--product", "mother2"], {}, answers=["y"])
    check(code == 0 and (drive / sd.MOTHER2_FILE).exists(), "... and installed after a yes")
    cleanup()
    sd.find_rp2040_drive = lambda: None if (drive / sd.SISTER_FILE).exists() else drive
    code, out = run(["--firmware", str(res / sd.SISTER_FILE)], {})
    check(code == 0 and (drive / sd.SISTER_FILE).exists() and "Step Sister has" in out,
          "a Step Sister .uf2 passed with --firmware targets the Step Sister")
    code, out = run(["--product", "sister", "--firmware", str(res / sd.MOTHER2_FILE)], {})
    check(code == 1 and "not for the Step Sister" in out, "the Step Mother .uf2 is refused for the Step Sister")
    (drive / "CURRENT.UF2").unlink()

    # nothing detected: the question, all answers
    cleanup()
    sd.find_rp2040_drive = lambda: None
    sd.teensy_loader_path = lambda: Path("/fake/teensy_loader_cli")
    ran2 = []
    sd.subprocess.run = lambda cmd, **kw: ran2.append(cmd) or Done()
    code, out = run([], {}, answers=["1"])
    check(code == 0 and "Which board do you have?" in out and ran2 and ran2[0][-1] == str(res / sd.MOTHER_FILE),
          "Nothing detected: asks, answer 1 installs on the Teensy")
    sd.find_rp2040_drive = present_then_gone()
    code, out = run([], {}, answers=["1"])
    check(code == 0 and "Which board do you have?" not in out and "no firmware on it yet" in out,
          "RPI-RP2 drive without firmware: only asks Step Mother (USB-C) or Step Sister")
    cleanup()
    first = iter([None])   # detection finds nothing, then the board is in update mode
    sd.find_rp2040_drive = lambda: next(first, None if (drive / sd.MOTHER2_FILE).exists() else drive)
    code, out = run([], {}, answers=["x", "2"])
    check(code == 0 and "Type 1, 2 or 3" in out and (drive / sd.MOTHER2_FILE).exists(),
          "Nothing detected: asks, junk is re-asked, answer 2 installs on the Step Mother (USB-C)")
    cleanup()
    first = iter([None])
    sd.find_rp2040_drive = lambda: next(first, None if (drive / sd.SISTER_FILE).exists() else drive)
    code, out = run([], {}, answers=["3"])
    check(code == 0 and (drive / sd.SISTER_FILE).exists(), "Nothing detected: answer 3 installs on the Step Sister")

    # --firmware picks the board by suffix; a wrong suffix for --product is refused
    code, out = run(["--product", "mother2", "--firmware", str(res / sd.MOTHER_FILE)], {})
    check(code == 1 and ".uf2" in out, "Step Mother 2 refuses a .hex file")
    code, out = run(["--firmware", str(res / "notes.txt")], {})
    check(code == 1 and "expected a .hex" in out, "an unknown file type is refused")
    sd.find_rp2040_drive, sd.find_teensy_serial_ports, sd.time.sleep = real
    check(sd.find_rp2040_drive() is None or isinstance(sd.find_rp2040_drive(), Path), "real drive search is safe")

    print("FAILED" if fails else "StepDaddy tests passed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
