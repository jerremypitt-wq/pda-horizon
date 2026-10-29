#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Проверки build.py / package.py / boot_app0 / GUI-ловушки (без PIO и железа).

Запуск: python tools/build/tests/test_build.py (чистый Python, ~секунда).
"""
import importlib.util
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import types

TESTS = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.abspath(os.path.join(TESTS, ".."))
REPO = os.path.abspath(os.path.join(BUILD_DIR, "..", ".."))
FLASHER_DIR = os.path.join(REPO, "tools", "flasher")
TMP = os.path.join(tempfile.gettempdir(), "hztest-build")
shutil.rmtree(TMP, ignore_errors=True)
os.makedirs(TMP, exist_ok=True)
PASS = []


def check(name, cond, extra=""):
    assert cond, f"FAIL: {name} {extra}"
    PASS.append(name)
    print(f"  ok: {name}")


def load_mod(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


build = load_mod("hzbuild", os.path.join(BUILD_DIR, "build.py"))
pkg = load_mod("hzpackage", os.path.join(BUILD_DIR, "package.py"))

print("== build.py: чистые функции ==")
check("py-ok-313", build.python_ok((3, 13, 1)))
check("py-ok-310", build.python_ok((3, 10, 0)))
check("py-ok-314", build.python_ok((3, 14, 0)))
check("py-no-39", not build.python_ok((3, 9, 9)))
check("py-no-315", not build.python_ok((3, 15, 0)))
check("py-no-27", not build.python_ok((2, 7, 18)))

hdr = os.path.join(TMP, "src", "horizon")
os.makedirs(hdr, exist_ok=True)
with open(os.path.join(hdr, "HorizonStrings.h"), "w") as f:
    f.write('constexpr const char *kFirmwareVersion = "1.7.0";\n')
check("version-read", build.read_version(TMP) == "1.7.0")
check("version-missing", build.read_version(os.path.join(TMP, "nope")) is None)

fw = os.path.join(TMP, "fw1")
os.makedirs(os.path.join(fw, "src", "horizon"), exist_ok=True)
with open(os.path.join(fw, "src", "horizon", "horizon_target.h"), "w") as f:
    f.write("#define HORIZON_PACKET_LINK 1\n")
check("overlay-ok", build.overlay_ok(fw))
check("overlay-no-dir", not build.overlay_ok(os.path.join(TMP, "fw-missing")))
fw2 = os.path.join(TMP, "fw2")
os.makedirs(os.path.join(fw2, "src", "horizon"), exist_ok=True)
with open(os.path.join(fw2, "src", "horizon", "horizon_target.h"), "w") as f:
    f.write("// stock\n")
check("overlay-stock", not build.overlay_ok(fw2))

print("== build.py: verify_outputs ==")


def craft_build(d, versioned=False):
    os.makedirs(d, exist_ok=True)
    app = "firmware-elecrow-adv-35-tft-2.8.1.8e6a88d.bin" if versioned else "firmware.bin"
    lfs = "littlefs-elecrow-adv-35-tft-2.8.1.8e6a88d.bin" if versioned else "littlefs.bin"
    files = {app: 2048, "bootloader.bin": 256, "partitions.bin": 0, lfs: 1024}
    rec = struct.pack("<HBBII16sI", 0x50AA, 1, 0x82, 0xC90000, 0x370000,
                      b"littlefs" + b"\0" * 8, 0)
    end = struct.pack("<HBBII16sI", 0xFFFF, 0, 0, 0, 0, b"\0" * 16, 0)
    with open(os.path.join(d, "partitions.bin"), "wb") as f:
        f.write(rec + end + b"\xff" * (0x1000 - 64))
    for fn, n in files.items():
        if fn == "partitions.bin":
            continue
        with open(os.path.join(d, fn), "wb") as f:
            f.write(os.urandom(n))
    return d


root1 = os.path.join(TMP, "root1")
bdir = craft_build(os.path.join(root1, "firmware-hz", ".pio", "build", build.ENV),
                   versioned=True)
rows = build.verify_outputs(root1)
check("verify-versioned", len(rows) == 4 and rows[0][0] == "firmware.bin"
      and rows[0][2] == 2048, str(rows))
root2 = os.path.join(TMP, "root2")
craft_build(os.path.join(root2, "firmware-hz", ".pio", "build", build.ENV))
rows = build.verify_outputs(root2)
check("verify-exact", len(rows) == 4)
os.remove(os.path.join(root2, "firmware-hz", ".pio", "build", build.ENV, "firmware.bin"))
try:
    build.verify_outputs(root2)
    check("verify-missing", False, "no Fail raised")
except build.Fail as e:
    check("verify-missing", "firmware.bin" in str(e), str(e))
build.stamp_version(root2, "1.2.3")
with open(os.path.join(root2, "firmware-hz", ".pio", "build", build.ENV,
                       build.STAMP_FILE)) as f:
    check("stamp-file", f.read().strip() == "1.2.3")

print("== build.py --help ==")
r = subprocess.run([sys.executable, os.path.join(BUILD_DIR, "build.py"), "--help"],
                   capture_output=True, text=True)
check("build-help", r.returncode == 0 and "шаг 1 из 2" in r.stdout, r.stdout + r.stderr)
r = subprocess.run([sys.executable, os.path.join(BUILD_DIR, "package.py"), "--help"],
                   capture_output=True, text=True)
check("package-help", r.returncode == 0 and "шаг 2 из 2" in r.stdout, r.stdout + r.stderr)

print("== package.py: сквозная упаковка на синтетике ==")
root3 = os.path.join(TMP, "root3")
os.makedirs(os.path.join(root3, "src", "horizon"), exist_ok=True)
with open(os.path.join(root3, "src", "horizon", "HorizonStrings.h"), "w") as f:
    f.write('constexpr const char *kFirmwareVersion = "9.9.9";\n')
craft_build(os.path.join(root3, "firmware-hz", ".pio", "build", build.ENV), versioned=True)
cwd = os.getcwd()
try:
    code = pkg.main([], root=root3)
finally:
    os.chdir(cwd)
check("pkg-exit", code == 0)
check("pkg-autoname", os.path.isfile(os.path.join(root3, "horizon-9.9.9.zip")))
with open(os.path.join(root3, "horizon-9.9.9", "flash.json"), encoding="utf-8") as f:
    m = json.load(f)
check("pkg-manifest", m["name"] == "horizon-9.9.9" and len(m["parts"]) == 5, str(m))
try:
    code = pkg.main(["--out", "hz-dio", "--flash-mode", "dio"], root=root3)
finally:
    os.chdir(cwd)
check("pkg-override-exit", code == 0)
with open(os.path.join(root3, "hz-dio", "flash.json"), encoding="utf-8") as f:
    m2 = json.load(f)
check("pkg-override", m2["flash_mode"] == "dio" and m2["name"] == "hz-dio", str(m2))
os.makedirs(os.path.join(TMP, "root-noversion"), exist_ok=True)
try:
    code = pkg.main([], root=os.path.join(TMP, "root-noversion"))
finally:
    os.chdir(cwd)
check("pkg-nover-noout", code == 1)
try:
    code = pkg.main([], root=os.path.join(TMP, "root-noexist"))
finally:
    os.chdir(cwd)
check("pkg-noroot-clean", code == 1)

print("== package.py: сверка версии бинаря ==")
root4 = os.path.join(TMP, "root4")
os.makedirs(os.path.join(root4, "src", "horizon"), exist_ok=True)
with open(os.path.join(root4, "src", "horizon", "HorizonStrings.h"), "w") as f:
    f.write('constexpr const char *kFirmwareVersion = "9.9.9";\n')
b4 = craft_build(os.path.join(root4, "firmware-hz", ".pio", "build", build.ENV),
                 versioned=True)
with open(os.path.join(b4, "hz_fw_version.txt"), "w") as f:
    f.write("1.0.0\n")
try:
    code = pkg.main([], root=root4)
finally:
    os.chdir(cwd)
check("pkg-mismatch-exit", code == 1)
check("pkg-mismatch-nozip",
      not os.path.isfile(os.path.join(root4, "horizon-9.9.9.zip")))
with open(os.path.join(b4, "hz_fw_version.txt"), "w") as f:
    f.write("9.9.9\n")
try:
    code = pkg.main(["--out", "hz-match"], root=root4)
finally:
    os.chdir(cwd)
check("pkg-match-exit", code == 0)
with open(os.path.join(root4, "hz-match", "flash.json"), encoding="utf-8") as f:
    mm = json.load(f)
check("pkg-match-fw", mm["fw_version"] == "9.9.9", str(mm))

print("== make_package: boot_app0 ==")
MP = os.path.join(FLASHER_DIR, "make_package.py")


def run_mk(build_d, out_d, extra=(), env=None):
    e = dict(os.environ)
    if env:
        e.update(env)
    return subprocess.run([sys.executable, MP, "--build", build_d, "--out", out_d,
                           "--no-fs"] + list(extra),
                          capture_output=True, text=True, env=e)


b5 = craft_build(os.path.join(TMP, "build5"))
explicit = os.path.join(TMP, "real_app0.bin")
with open(explicit, "wb") as f:
    f.write(b"R" * 8192)
r = run_mk(b5, os.path.join(TMP, "o-explicit"), ["--boot-app0", explicit])
check("app0-explicit", r.returncode == 0 and "по --boot-app0" in r.stdout,
      r.stdout + r.stderr)
with open(os.path.join(TMP, "o-explicit", "boot_app0.bin"), "rb") as f:
    check("app0-explicit-bytes", f.read() == b"R" * 8192)
r = run_mk(b5, os.path.join(TMP, "o-missing"), ["--boot-app0", os.path.join(TMP, "nope.bin")])
check("app0-missing-err", r.returncode == 1 and "--boot-app0 не найден" in r.stdout,
      r.stdout + r.stderr)
fakehome = os.path.join(TMP, "fakehome")
fwdir = os.path.join(fakehome, ".platformio", "packages",
                     "framework-arduinoespressif32@3.3.3", "tools", "partitions")
os.makedirs(fwdir, exist_ok=True)
with open(os.path.join(fwdir, "boot_app0.bin"), "wb") as f:
    f.write(b"F" * 8192)
r = run_mk(b5, os.path.join(TMP, "o-auto"), env={"HOME": fakehome,
                                                 "USERPROFILE": fakehome,
                                                 "PLATFORMIO_CORE_DIR": ""})
check("app0-autofind", r.returncode == 0 and "из фреймворка" in r.stdout,
      r.stdout + r.stderr)
with open(os.path.join(TMP, "o-auto", "boot_app0.bin"), "rb") as f:
    check("app0-autofind-bytes", f.read() == b"F" * 8192)
emptyhome = os.path.join(TMP, "emptyhome")
os.makedirs(emptyhome, exist_ok=True)
r = run_mk(b5, os.path.join(TMP, "o-zero"), env={"HOME": emptyhome,
                                                 "USERPROFILE": emptyhome,
                                                 "PLATFORMIO_CORE_DIR": ""})
check("app0-zeros", r.returncode == 0 and "сгенерирован" in r.stdout, r.stdout + r.stderr)

print("== GUI: ловушка фаталов ==")
sys.path.insert(0, FLASHER_DIR)
tk = types.ModuleType("tkinter")
shown = []


class FakeMsg:
    @staticmethod
    def showerror(title, msg):
        shown.append((title, msg))


for sub in ("filedialog", "scrolledtext", "ttk"):
    m = types.ModuleType(f"tkinter.{sub}")
    sys.modules[f"tkinter.{sub}"] = m
    setattr(tk, sub, m)
sys.modules["tkinter.messagebox"] = FakeMsg
tk.messagebox = FakeMsg
tk.Tk = type("Tk", (), {})
sys.modules["tkinter"] = tk
import flasher_gui as gui  # noqa: E402


def boom():
    raise RuntimeError("трагедия в конструкторе")


gui.App = boom
import tempfile as _tf
fixed = os.path.join(TMP, "fatal.txt")
_tf.mkstemp = lambda prefix="", suffix="": (os.open(fixed, os.O_WRONLY | os.O_CREAT), fixed)
code = gui.main()
check("fatal-code", code == 1)
check("fatal-log", os.path.isfile(fixed)
      and "трагедия в конструкторе" in open(fixed, encoding="utf-8").read())
check("fatal-dialog", shown and shown[0][0] == "HorizonFlasher", str(shown))

print(f"\nВСЕГО ПРОШЛО: {len(PASS)}")
