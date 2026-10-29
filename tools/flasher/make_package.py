#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Собрать полевой пакет прошивки (*.bin + flash.json) из каталога сборки PlatformIO.

Запускается ДОМА (нужен интернет только для первой установки esptool -
самому сборщику пакета esptool не нужен). Пример (дерево прошивки):

  pio run -e elecrow-adv-35-tft
  pio run -e elecrow-adv-35-tft -t buildfs
  python make_package.py --build .pio/build/elecrow-adv-35-tft --out horizon-1.6.1

Оффсет файловой системы берётся из partitions.bin сборки (авторитетно),
fallback - ключ --fs-offset. boot_app0.bin копируется из сборки, а если его
там нет - генерируется (8 КБ нулей, как у Espressif).
"""
import argparse
import json
import os
import shutil
import struct
import sys

FLASH_MODE = "qio"
FLASH_FREQ = "80m"
FLASH_SIZE = "16MB"
DEFAULT_BAUD = 921600
BOOT_APP0_SIZE = 0x2000


def parse_partitions_bin(path):
    """Найти (offset, size, label) раздела littlefs/spiffs в partitions.bin."""
    with open(path, "rb") as f:
        data = f.read()
    for i in range(0, len(data), 32):
        rec = data[i : i + 32]
        if len(rec) < 32:
            break
        magic, typ, sub, offset, size, label, _flags = struct.unpack("<HBBII16sI", rec)
        if magic == 0xFFFF:
            break
        if magic != 0x50AA:
            continue
        name = label.split(b"\0")[0].decode("ascii", "replace").strip()
        if name.lower() in ("littlefs", "spiffs") or (typ == 1 and sub == 0x82):
            return offset, size, name
    return None, None, None


def main(argv=None):
    ap = argparse.ArgumentParser(description="Сборка полевого пакета прошивки Горизонта.")
    ap.add_argument("--build", required=True, help="каталог сборки PIO (.pio/build/elecrow-adv-35-tft)")
    ap.add_argument("--out", required=True, help="куда сложить пакет")
    ap.add_argument("--name", default="", help="имя пакета (по умолчанию - имя каталога)")
    ap.add_argument("--fs-offset", default="",
                    help="оффсет ФС вручную (0x...), иначе берётся из partitions.bin")
    ap.add_argument("--no-fs", action="store_true", help="пакет без файловой системы")
    ap.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="скорость прошивки по умолчанию")
    ap.add_argument("--zip", action="store_true",
                    help="упаковать готовый пакет в ZIP рядом (<out>.zip, с папкой внутри)")
    ap.add_argument("--force", action="store_true", help="перезаписать каталог пакета")
    args = ap.parse_args(argv)

    build = os.path.abspath(args.build)
    need = ["firmware.bin", "bootloader.bin", "partitions.bin"] + ([] if args.no_fs else ["littlefs.bin"])
    missing = [f for f in need if not os.path.isfile(os.path.join(build, f))]
    if missing:
        print(f"ОШИБКА: в сборке нет файлов: {', '.join(missing)}")
        if "littlefs.bin" in missing:
            print("Подсказка: выполни 'pio run -e elecrow-adv-35-tft -t buildfs'.")
        return 1

    out = os.path.abspath(args.out)
    if os.path.isdir(out):
        if not args.force and os.listdir(out):
            print(f"ОШИБКА: каталог {out} не пуст (добавь --force).")
            return 1
    else:
        os.makedirs(out, exist_ok=True)

    for f in need:
        shutil.copy2(os.path.join(build, f), os.path.join(out, f))

    # boot_app0: из сборки, иначе сгенерировать нули.
    app0_src = os.path.join(build, "boot_app0.bin")
    if os.path.isfile(app0_src):
        shutil.copy2(app0_src, os.path.join(out, "boot_app0.bin"))
        print("boot_app0.bin: взят из сборки.")
    else:
        with open(os.path.join(out, "boot_app0.bin"), "wb") as f:
            f.write(b"\0" * BOOT_APP0_SIZE)
        print("boot_app0.bin: в сборке нет, сгенерирован (8 КБ нулей).")

    parts = [
        {"offset": "0x0", "file": "bootloader.bin"},
        {"offset": "0x8000", "file": "partitions.bin"},
        {"offset": "0xe000", "file": "boot_app0.bin"},
        {"offset": "0x10000", "file": "firmware.bin"},
    ]
    if not args.no_fs:
        if args.fs_offset:
            try:
                fs_off = int(args.fs_offset, 0)
            except ValueError:
                print(f"ОШИБКА: плохой --fs-offset: {args.fs_offset}")
                return 1
            print(f"Оффсет ФС: {fs_off:#x} (вручную).")
        else:
            fs_off, fs_size, fs_name = parse_partitions_bin(os.path.join(build, "partitions.bin"))
            if fs_off is None:
                print("ОШИБКА: в partitions.bin нет раздела littlefs/spiffs (укажи --fs-offset).")
                return 1
            print(f"Оффсет ФС: {fs_off:#x} (раздел '{fs_name}', размер {fs_size:#x}).")
        parts.append({"offset": hex(fs_off), "file": "littlefs.bin", "tag": "fs"})

    manifest = {
        "name": args.name or os.path.basename(out),
        "chip": "esp32s3",
        "flash_mode": FLASH_MODE,
        "flash_freq": FLASH_FREQ,
        "flash_size": FLASH_SIZE,
        "baud": args.baud,
        "parts": parts,
    }
    with open(os.path.join(out, "flash.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)
        f.write("\n")

    print(f"Пакет готов: {out}")
    for p in parts:
        sz = os.path.getsize(os.path.join(out, p["file"]))
        print(f"  {p['offset']:>10}  {p['file']} ({sz} Б)")

    if args.zip:
        archive = shutil.make_archive(out, "zip", os.path.dirname(out), os.path.basename(out))
        print(f"ZIP: {archive} ({os.path.getsize(archive)} Б)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
