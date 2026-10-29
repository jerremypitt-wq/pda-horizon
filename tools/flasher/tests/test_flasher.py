#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Проверки флэшера без железа и дисплея: GUI-ядро, ZIP-пакеты, сборщик пакета.

Запуск: python tools/flasher/tests/test_flasher.py (чистый Python, ~секунда).
"""
import json
import os
import queue
import shutil
import struct
import subprocess
import sys
import tempfile
import types
import zipfile
from types import SimpleNamespace

FLASHER = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, FLASHER)

# --- заглушка tkinter: импорт flasher_gui без дисплея (окно не создаём) ---
tk = types.ModuleType("tkinter")
for sub in ("filedialog", "messagebox", "scrolledtext", "ttk"):
    m = types.ModuleType(f"tkinter.{sub}")
    sys.modules[f"tkinter.{sub}"] = m
    setattr(tk, sub, m)
tk.Tk = type("Tk", (), {})  # только для class App(tk.Tk) при импорте
sys.modules["tkinter"] = tk

import flasher  # noqa: E402
import flasher_gui as gui  # noqa: E402
import package as zpkg  # noqa: E402

TMP = os.path.join(tempfile.gettempdir(), "hztest-flasher")
shutil.rmtree(TMP, ignore_errors=True)
os.makedirs(TMP, exist_ok=True)
PASS = []


def check(name, cond, extra=""):
    assert cond, f"FAIL: {name} {extra}"
    PASS.append(name)
    print(f"  ok: {name}")


print("== parse_progress ==")
check("pct-simple", gui.parse_progress("Writing at 0x00010000... (  7 %)") == 7)
check("pct-100", gui.parse_progress("Writing at 0x00010000... (100 %)") == 100)
check("pct-nospace", gui.parse_progress("x (100%) y") == 100)
check("pct-none", gui.parse_progress("Connecting...") is None)
check("pct-empty", gui.parse_progress("") is None)
check("pct-last", gui.parse_progress("( 3 %) ... ( 42 %)") == 42)

print("== auto_pick ==")


def P(dev, vid=None, desc="USB Serial"):
    return SimpleNamespace(device=dev, vid=vid, pid=0x7523, description=desc)


dev, note = gui.auto_pick([])
check("auto-empty", dev is None)
dev, note = gui.auto_pick([P("COM9")])
check("auto-single", dev == "COM9", note)
ports = [P("COM1", 0x1234), P("COM9", 0x1A86, "USB-SERIAL CH340"), P("COM10", 0x5678)]
dev, note = gui.auto_pick(ports)
check("auto-ch340", dev == "COM9", note)
ports2 = [P("COM5", 0x1A86), P("COM6", 0x10C4)]
dev, note = gui.auto_pick(ports2)
check("auto-first-pref", dev == "COM5" and "ещё 1" in note, note)
ports3 = [P("COM7", 0x1234), P("COM8", 0x303A, "USB JTAG/serial debug unit")]
dev, note = gui.auto_pick(ports3)
check("auto-esp-jtag", dev == "COM8", note)
ports4 = [P("COM5", 0x1A86, "USB-SERIAL CH340"),
          P("COM8", 0x303A, "USB JTAG/serial debug unit")]
dev, note = gui.auto_pick(ports4)
check("auto-native-beats-ch340", dev == "COM8", note)
check("auto-no-hint-on-j2", "J2" not in note, note)
dev, note = gui.auto_pick([P("COM3", 0x1A86, "USB-SERIAL CH340")])
check("auto-bridge-hint", "J2" in note and "CH340" in note, note)

print("== FlashWorker (мок esptool) ==")
MANY = {
    "name": "horizon-test", "chip": "esp32s3", "flash_mode": "qio",
    "flash_freq": "80m", "flash_size": "16MB", "baud": 921600,
    "parts": [
        {"offset": 0x0, "file": "/tmp/fake/bootloader.bin", "tag": ""},
        {"offset": 0x10000, "file": "/tmp/fake/firmware.bin", "tag": ""},
        {"offset": 0xC90000, "file": "/tmp/fake/littlefs.bin", "tag": "fs"},
    ],
}
calls = []


def fake_run(argv):
    calls.append(argv)
    if "erase-flash" in argv:
        print("Erasing flash (this may take a while)...")
        print("Chip erase completed successfully")
        return 0, None
    print("Writing at 0x00010000... ( 42 %)")
    print("Writing at 0x00020000... (100 %)")
    print("Hash of data verified.")
    return 0, None


def run_worker(_runner=fake_run, **kw):
    q = queue.Queue()
    flasher.run_esptool = _runner
    w = gui.FlashWorker(q, MANY, **kw)
    w.start()
    w.join(timeout=30)
    assert not w.is_alive(), "worker hung"
    out = []
    try:
        while True:
            out.append(q.get_nowait())
    except queue.Empty:
        pass
    return out


calls.clear()
out = run_worker(port_choice="COM9 [1A86:7523] USB-SERIAL CH340", baud_choice="",
                 erase_all=False, skip_fs=False)
kinds = [k for k, *_ in out]
check("worker-stages", "stage" in kinds and ("done", True) in [(k, r[0]) for k, *r in out],
      str(kinds))
logs = "".join(r[0] for k, *r in out if k == "log")
check("worker-log-pct", "( 42 %)" in logs and "COM9" in logs, logs[:300])
check("worker-argv", calls and "write-flash" in calls[0] and "-z" in calls[0]
      and "--flash-mode" in calls[0] and "0x10000" in calls[0]
      and calls[0].count("/tmp/fake/littlefs.bin") == 1, str(calls))
check("worker-baud-default", "--baud" in calls[0]
      and calls[0][calls[0].index("--baud") + 1] == "921600")

calls.clear()
out = run_worker(port_choice="COM9", baud_choice="460800", erase_all=True, skip_fs=False)
check("worker-erase-calls", len(calls) == 2 and "erase-flash" in calls[0]
      and "write-flash" in calls[1], str([c[-2:] for c in calls]))
check("worker-erase-baud", calls[0][calls[0].index("--baud") + 1] == "460800")

calls.clear()
out = run_worker(port_choice="COM9", baud_choice="", erase_all=False, skip_fs=True)
check("worker-skipfs", calls and "/tmp/fake/littlefs.bin" not in calls[0]
      and "/tmp/fake/firmware.bin" in calls[0])


def fake_fail(argv):
    print("A fatal error occurred: Failed to connect")
    return 2, None


out = run_worker(fake_fail, port_choice="COM9", baud_choice="", erase_all=False,
                 skip_fs=False)
dones = [r for k, *r in out if k == "done"]
check("worker-fail", dones and dones[0][0] is False and "код 2" in dones[0][1], str(dones))


def fake_ports():
    return [P("COM3", 0x1A86, "USB-SERIAL CH340")], None


flasher.list_serial_ports = fake_ports
calls.clear()
out = run_worker(port_choice=gui.AUTO_PORT, baud_choice="", erase_all=False, skip_fs=False)
logs = "".join(r[0] for k, *r in out if k == "log")
check("worker-auto", "автоматически" in logs and "COM3" in logs, logs[:200])

print("== ZipPackage ==")


def write_pkg(root):
    os.makedirs(root, exist_ok=True)
    for fn, body in (("bootloader.bin", b"B" * 100), ("firmware.bin", b"F" * 2000),
                     ("littlefs.bin", b"L" * 500)):
        with open(os.path.join(root, fn), "wb") as f:
            f.write(body)
    m = {"name": "horizon-1.7.0-test", "chip": "esp32s3", "flash_mode": "qio",
         "flash_freq": "80m", "flash_size": "16MB", "baud": 921600,
         "parts": [{"offset": "0x0", "file": "bootloader.bin"},
                   {"offset": "0x10000", "file": "firmware.bin"},
                   {"offset": "0xc90000", "file": "littlefs.bin", "tag": "fs"}]}
    with open(os.path.join(root, "flash.json"), "w", encoding="utf-8") as f:
        json.dump(m, f)
    return root


def make_zip(zippath, arcdir, srcdir):
    with zipfile.ZipFile(zippath, "w", zipfile.ZIP_DEFLATED) as zf:
        for base, _ds, fs in os.walk(srcdir):
            for fn in fs:
                full = os.path.join(base, fn)
                zf.write(full, os.path.join(arcdir, os.path.relpath(full, srcdir)) if arcdir
                         else os.path.relpath(full, srcdir))


src = write_pkg(os.path.join(TMP, "src"))
z1 = os.path.join(TMP, "nested.zip")
make_zip(z1, "horizon-1.7.0", src)
with zpkg.ZipPackage(z1) as p:
    check("zip-nested-ok", p.ok, p.error or "")
    check("zip-summary", "horizon-1.7.0-test" in p.summary() and "0x010000" in p.summary()
          and "[FS]" in p.summary(), p.summary())
    check("zip-manifest-parts", len(p.manifest["parts"]) == 3
          and p.manifest["parts"][1]["offset"] == 0x10000)
    tmpdir = p.tmp.name
check("zip-cleanup", not os.path.exists(tmpdir))

z2 = os.path.join(TMP, "root.zip")
make_zip(z2, "", src)
with zpkg.ZipPackage(z2) as p:
    check("zip-root-ok", p.ok, p.error or "")

z3 = os.path.join(TMP, "nomanifest.zip")
with zipfile.ZipFile(z3, "w") as zf:
    zf.writestr("readme.txt", "hello")
with zpkg.ZipPackage(z3) as p:
    check("zip-no-manifest", not p.ok and "flash.json" in p.error, p.error or "")

z4 = os.path.join(TMP, "two.zip")
with zipfile.ZipFile(z4, "w") as zf:
    zf.writestr("a/flash.json", "{}")
    zf.writestr("b/flash.json", "{}")
with zpkg.ZipPackage(z4) as p:
    check("zip-two-pkgs", not p.ok and "несколько" in p.error, p.error or "")

z5 = os.path.join(TMP, "notzip.zip")
with open(z5, "wb") as f:
    f.write(b"this is not a zip")
with zpkg.ZipPackage(z5) as p:
    check("zip-bad", not p.ok and "не ZIP" in p.error, p.error or "")

z6 = os.path.join(TMP, "slip.zip")
with zipfile.ZipFile(z6, "w") as zf:
    zf.writestr("../../evil.txt", "x")
    zf.writestr("horizon-1.7.0/flash.json", "{}")
evil_probe = os.path.join(tempfile.gettempdir(), "evil.txt")
try:
    os.remove(evil_probe)
except OSError:
    pass
with zpkg.ZipPackage(z6) as p:
    check("zip-slip", not p.ok and "опасный путь" in p.error, p.error or "")
check("zip-slip-noevil", not os.path.exists(evil_probe))

src7 = write_pkg(os.path.join(TMP, "src7"))
os.remove(os.path.join(src7, "firmware.bin"))
z7 = os.path.join(TMP, "missingbin.zip")
make_zip(z7, "horizon-1.7.0", src7)
with zpkg.ZipPackage(z7) as p:
    check("zip-missing-bin", not p.ok and "firmware.bin" in p.error, p.error or "")

print("== make_package --zip (синтетическая сборка) ==")
BUILD = os.path.join(TMP, "build")
os.makedirs(BUILD, exist_ok=True)
for fn, n in (("firmware.bin", 2048), ("bootloader.bin", 256), ("littlefs.bin", 1024)):
    with open(os.path.join(BUILD, fn), "wb") as f:
        f.write(os.urandom(n))
rec = struct.pack("<HBBII16sI", 0x50AA, 1, 0x82, 0xC90000, 0x370000, b"littlefs" + b"\0" * 8, 0)
end = struct.pack("<HBBII16sI", 0xFFFF, 0, 0, 0, 0, b"\0" * 16, 0)
with open(os.path.join(BUILD, "partitions.bin"), "wb") as f:
    f.write(rec + end + b"\xff" * (0x1000 - 64))
r = subprocess.run([sys.executable, os.path.join(FLASHER, "make_package.py"),
                    "--build", BUILD, "--out", os.path.join(TMP, "horizon-9.9.9-test"),
                    "--zip"], capture_output=True, text=True)
check("mkzip-exit", r.returncode == 0, r.stdout + r.stderr)
check("mkzip-offset", "0xc90000" in r.stdout, r.stdout)
zp = os.path.join(TMP, "horizon-9.9.9-test.zip")
check("mkzip-file", os.path.isfile(zp))
with zipfile.ZipFile(zp) as zf:
    names = zf.namelist()
check("mkzip-content", "horizon-9.9.9-test/flash.json" in names
      and "horizon-9.9.9-test/firmware.bin" in names, str(names))
with zpkg.ZipPackage(zp) as p:
    check("mkzip-roundtrip", p.ok, p.error or "")

print("== make_package: версионные имена Meshtastic ==")
BUILD2 = os.path.join(TMP, "build2")
os.makedirs(BUILD2, exist_ok=True)
with open(os.path.join(BUILD2, "firmware-elecrow-adv-35-tft-2.8.1.8e6a88d.bin"), "wb") as f:
    f.write(b"A" * 2048)
with open(os.path.join(BUILD2, "firmware-elecrow-adv-35-tft-2.8.1.8e6a88d.factory.bin"), "wb") as f:
    f.write(b"X" * 4096)
with open(os.path.join(BUILD2, "littlefs-elecrow-adv-35-tft-2.8.1.8e6a88d.bin"), "wb") as f:
    f.write(b"B" * 1024)
with open(os.path.join(BUILD2, "bootloader.bin"), "wb") as f:
    f.write(b"C" * 256)
with open(os.path.join(BUILD, "partitions.bin"), "rb") as f:
    pbin = f.read()
with open(os.path.join(BUILD2, "partitions.bin"), "wb") as f:
    f.write(pbin)
OUT2 = os.path.join(TMP, "horizon-ver")
r = subprocess.run([sys.executable, os.path.join(FLASHER, "make_package.py"),
                    "--build", BUILD2, "--out", OUT2], capture_output=True, text=True)
check("ver-exit", r.returncode == 0, r.stdout + r.stderr)
check("ver-found-msg", "найден как firmware-elecrow" in r.stdout
      and "найден как littlefs-elecrow" in r.stdout, r.stdout)
check("ver-canonical", os.path.getsize(os.path.join(OUT2, "firmware.bin")) == 2048
      and os.path.getsize(os.path.join(OUT2, "littlefs.bin")) == 1024)

BUILD3 = os.path.join(TMP, "build3")
os.makedirs(BUILD3, exist_ok=True)
old = os.path.join(BUILD3, "firmware-elecrow-adv-35-tft-2.8.0.old.bin")
new = os.path.join(BUILD3, "firmware-elecrow-adv-35-tft-2.8.1.8e6a88d.bin")
with open(old, "wb") as f:
    f.write(b"O" * 100)
os.utime(old, (1000000000, 1000000000))
with open(new, "wb") as f:
    f.write(b"N" * 200)
os.utime(new, (1900000000, 1900000000))
for fn, body in (("bootloader.bin", b"C" * 256), ("partitions.bin", pbin)):
    with open(os.path.join(BUILD3, fn), "wb") as f:
        f.write(body)
OUT3 = os.path.join(TMP, "horizon-stale")
r = subprocess.run([sys.executable, os.path.join(FLASHER, "make_package.py"),
                    "--build", BUILD3, "--out", OUT3, "--no-fs"], capture_output=True, text=True)
check("ver-stale-exit", r.returncode == 0, r.stdout + r.stderr)
check("ver-stale-newest", "самый свежий" in r.stdout
      and os.path.getsize(os.path.join(OUT3, "firmware.bin")) == 200, r.stdout)

BUILD4 = os.path.join(TMP, "build4")
os.makedirs(BUILD4, exist_ok=True)
with open(os.path.join(BUILD4, "firmware-elecrow-adv-35-tft-2.8.1.8e6a88d.factory.bin"), "wb") as f:
    f.write(b"X" * 4096)
for fn, body in (("bootloader.bin", b"C" * 256), ("partitions.bin", pbin)):
    with open(os.path.join(BUILD4, fn), "wb") as f:
        f.write(body)
r = subprocess.run([sys.executable, os.path.join(FLASHER, "make_package.py"),
                    "--build", BUILD4, "--out", os.path.join(TMP, "horizon-fact"), "--no-fs"],
                   capture_output=True, text=True)
check("ver-factory-ignored", r.returncode == 1 and "firmware.bin" in r.stdout, r.stdout)

print("== make_package: оверрайды flash-параметров ==")
OUTP = os.path.join(TMP, "horizon-params")
r = subprocess.run([sys.executable, os.path.join(FLASHER, "make_package.py"),
                    "--build", BUILD2, "--out", OUTP, "--no-fs",
                    "--flash-mode", "dio", "--flash-freq", "40m", "--flash-size", "8MB"],
                   capture_output=True, text=True)
check("prm-exit", r.returncode == 0, r.stdout + r.stderr)
check("prm-print", "dio/40m/8MB" in r.stdout, r.stdout)
with open(os.path.join(OUTP, "flash.json"), encoding="utf-8") as f:
    mp = json.load(f)
check("prm-manifest", mp["flash_mode"] == "dio" and mp["flash_freq"] == "40m"
      and mp["flash_size"] == "8MB" and mp["chip"] == "esp32s3", str(mp))
OUTD = os.path.join(TMP, "horizon-defaults")
r = subprocess.run([sys.executable, os.path.join(FLASHER, "make_package.py"),
                    "--build", BUILD2, "--out", OUTD, "--no-fs"],
                   capture_output=True, text=True)
check("prm-def-exit", r.returncode == 0, r.stdout + r.stderr)
with open(os.path.join(OUTD, "flash.json"), encoding="utf-8") as f:
    md = json.load(f)
check("prm-defaults", md["flash_mode"] == "qio" and md["flash_freq"] == "80m"
      and md["flash_size"] == "16MB", str(md))

print("== flasher: манифест -> argv esptool ==")
m = {"chip": "esp32s3", "flash_mode": "dio", "flash_freq": "40m", "flash_size": "8MB",
     "parts": [{"offset": 0x10000, "file": "/tmp/fake/firmware.bin", "tag": ""}]}
argv = flasher.build_write_argv(m, "COM4", 460800, False)
check("argv-mode", argv[argv.index("--flash-mode") + 1] == "dio", str(argv))
check("argv-freq", argv[argv.index("--flash-freq") + 1] == "40m", str(argv))
check("argv-size", argv[argv.index("--flash-size") + 1] == "8MB", str(argv))
check("argv-baud", argv[argv.index("--baud") + 1] == "460800", str(argv))

print(f"\nВСЕГО ПРОШЛО: {len(PASS)}")
