#!/usr/bin/env python3
"""Tests StepDaddy against a simulated Teensy loader, with the real firmware
file CI just built.

usage: test_step_daddy.py StepMother_firmware.hex
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
    mother = Path(sys.argv[1])
    res = Path(tempfile.mkdtemp())
    shutil.copy(mother, res / sd.MOTHER_FILE)
    sd.resource_dir = lambda: res

    # 1. The version tag in the real image
    mt = sd.firmware_tag(res / sd.MOTHER_FILE)
    print(f"bundled: {mt}")
    check(mt and mt[0] == "Step Mother" and mt[1], "version tag read from the image")

    # 2. A firmware file for another board is refused
    other = res / "other.hex"
    tag = b"SSFW|Other Board|1.0.0|"
    other.write_text("".join(f":{len(c):02X}{i:04X}00{c.hex().upper()}00\n"
                             for i, c in ((i, tag[i:i + 16]) for i in range(0, len(tag), 16))))
    code, out = run(["--firmware", str(other)], {})
    check(code == 1 and "not for the Step Mother" in out, "a firmware file for another board is refused")

    # 3. Step Mother: a Teensy with a serial port is asked to restart at 134
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
        return run([], {})
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

    print("FAILED" if fails else "StepDaddy tests passed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
