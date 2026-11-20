#!/usr/bin/env python3
"""Run repository host checks; no PlatformIO, serial port or display required."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HOST = ROOT / "tools" / "tests"
SOURCES = ROOT / "src" / "horizon"


def run(label, argv):
    result = subprocess.run(argv, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, errors="replace")
    if result.returncode:
        print(f"FAIL: {label}\n{result.stdout[-8000:]}", flush=True)
        raise SystemExit(result.returncode)
    print(f"PASS: {label}", flush=True)
    return result.stdout


def main():
    # The legacy script-style checks are imported by unittest discovery and run
    # their assertions immediately; keep these two invocations explicit as well.
    for folder in ("tools/build/tests", "tools/flasher/tests"):
        out = run(folder, [sys.executable, "-m", "unittest", "discover", "-s", folder, "-q"])
        print("  " + next((line for line in out.splitlines() if line.startswith("Ran ")), ""))
    for name in ("test_group_contacts_ui_contract.py", "test_security_ui_contract.py",
                 "test_firmware_changelog.py"):
        run(name, [sys.executable, str(HOST / name)])

    compiler = shutil.which("g++")
    if not compiler:
        raise SystemExit("Host C++ checks require g++ (no compiler found)")
    cases = {
        "group_contacts": (),
        "max17048": ("HorizonBattery.cpp",),
        "poi_presentation": (),
        "security_battery_stats": ("HorizonBatteryStats.cpp",),
        "security_config": ("HorizonConfig.cpp", "HorizonStrings.cpp"),
    }
    with tempfile.TemporaryDirectory(prefix="hz-host-tests-") as temp:
        for name, files in cases.items():
            binary = str(Path(temp) / name)
            run(f"compile {name}", [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                                    "-I" + str(ROOT / "src"),
                                    "-I" + str(HOST / "stubs"),
                                    *(str(SOURCES / file) for file in files),
                                    str(HOST / f"test_{name}.cpp"), "-o", binary])
            run(f"run {name}", [binary])
    print("Host checks finished; firmware build and hardware are not covered.")


if __name__ == "__main__":
    main()
