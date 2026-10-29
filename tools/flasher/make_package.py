#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Собрать полевой пакет прошивки (*.bin + flash.json) из каталога сборки PlatformIO.

Запускается ДОМА (нужен интернет только для первой установки esptool -
самому сборщику пакета esptool не нужен). Пример (дерево прошивки):

  pio run -e elecrow-adv-35-tft
  pio run -e elecrow-adv-35-tft -t buildfs
  python make_package.py --build .pio/build/elecrow-adv-35-tft --out horizon-1.6.1

Оффсет файловой системы берётся из partitions.bin сборки (авторитетно),
fallback - ключ --fs-offset. boot_app0.bin берётся из сборки, иначе ищется
штатный во фреймворке PIO (~/.platformio), иначе генерируется (8 КБ нулей).
Явно задать можно ключом --boot-app0.
"""
import argparse
import glob
import json
import os
import shutil
import struct
import sys

# Дефолт - DIO: на CrowPanel Advance 3.5 QIO-заголовок даёт ROM-петлю
# (mode:QIO -> ets_loader.c 78 -> TG0WDT, вторая стадия не стартует;
# проверено 29.10.2026: QIO циклится, DIO грузится с первого раза).
# QIO остаётся доступен через --flash-mode для экспериментов.
FLASH_MODE = "dio"
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


def resolve_build_file(build, canonical, patterns):
    """Найти файл сборки: точное имя, иначе единственный/самый свежий по паттернам.

    Возвращает имя файла в каталоге сборки (для пакета он копируется
    под каноническим именем) или None, если ничего нет.
    """
    if os.path.isfile(os.path.join(build, canonical)):
        return canonical
    found = []
    for pat in patterns:
        found += [f for f in glob.glob(os.path.join(build, pat))
                  if os.path.isfile(f)
                  and not os.path.basename(f).endswith(".factory.bin")]
    if not found:
        return None
    if len(found) > 1:
        found.sort(key=os.path.getmtime)
        print(f"{canonical}: несколько кандидатов, беру самый свежий: "
              f"{os.path.basename(found[-1])}.")
        return os.path.basename(found[-1])
    print(f"{canonical}: найден как {os.path.basename(found[0])}.")
    return os.path.basename(found[0])


def find_framework_boot_app0():
    """Штатный boot_app0.bin из пакета фреймворка PIO (тот, что шьёт сам PIO)."""
    roots = []
    core = os.environ.get("PLATFORMIO_CORE_DIR", "")
    if core:
        roots.append(core)
    roots.append(os.path.join(os.path.expanduser("~"), ".platformio"))
    for r in roots:
        pat = os.path.join(r, "packages", "framework-arduinoespressif32*",
                            "tools", "partitions", "boot_app0.bin")
        for f in sorted(glob.glob(pat)):
            if os.path.isfile(f):
                return f
    return None


def main(argv=None):
    ap = argparse.ArgumentParser(description="Сборка полевого пакета прошивки Горизонта.")
    ap.add_argument("--build", required=True, help="каталог сборки PIO (.pio/build/elecrow-adv-35-tft)")
    ap.add_argument("--out", required=True, help="куда сложить пакет")
    ap.add_argument("--name", default="", help="имя пакета (по умолчанию - имя каталога)")
    ap.add_argument("--fs-offset", default="",
                    help="оффсет ФС вручную (0x...), иначе берётся из partitions.bin")
    ap.add_argument("--no-fs", action="store_true", help="пакет без файловой системы")
    ap.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="скорость прошивки по умолчанию")
    ap.add_argument("--flash-mode", default="",
                    help=f"режим SPI flash в заголовке образа (по умолчанию {FLASH_MODE})")
    ap.add_argument("--flash-freq", default="",
                    help=f"частота SPI flash в заголовке (по умолчанию {FLASH_FREQ})")
    ap.add_argument("--flash-size", default="",
                    help=f"размер flash в заголовке (по умолчанию {FLASH_SIZE})")
    ap.add_argument("--boot-app0", default="",
                    help="реальный boot_app0.bin из фреймворка "
                         "(иначе авто-поиск, иначе 8 КБ нулей)")
    ap.add_argument("--zip", action="store_true",
                    help="упаковать готовый пакет в ZIP рядом (<out>.zip, с папкой внутри)")
    ap.add_argument("--force", action="store_true", help="перезаписать каталог пакета")
    args = ap.parse_args(argv)

    build = os.path.abspath(args.build)
    # Meshtastic подписывает выхлоп версией (firmware-<env>-<ver>.bin,
    # littlefs-<env>-<ver>.bin) - принимаем и его. *.factory.bin - слитый
    # образ, для пакета не подходит и всегда игнорируется.
    need = [("firmware.bin", ["firmware-*.bin"]),
            ("bootloader.bin", []),
            ("partitions.bin", [])]
    if not args.no_fs:
        need.append(("littlefs.bin", ["littlefs-*.bin"]))
    resolved = {}
    missing = []
    for canonical, patterns in need:
        found = resolve_build_file(build, canonical, patterns)
        if found is None:
            missing.append(canonical)
        else:
            resolved[canonical] = found
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

    for f in resolved:
        shutil.copy2(os.path.join(build, resolved[f]), os.path.join(out, f))

    # boot_app0: из сборки -> --boot-app0 -> авто-поиск во фреймворке -> нули.
    app0_src = os.path.join(build, "boot_app0.bin")
    app0_note = "взят из сборки."
    if not os.path.isfile(app0_src):
        if args.boot_app0:
            if not os.path.isfile(args.boot_app0):
                print(f"ОШИБКА: --boot-app0 не найден: {args.boot_app0}")
                return 1
            app0_src = args.boot_app0
            app0_note = "взят по --boot-app0."
        else:
            found = find_framework_boot_app0()
            if found is not None:
                app0_src = found
                app0_note = f"взят из фреймворка: {found}."
    if os.path.isfile(app0_src):
        shutil.copy2(app0_src, os.path.join(out, "boot_app0.bin"))
        print(f"boot_app0.bin: {app0_note}")
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
        "flash_mode": args.flash_mode or FLASH_MODE,
        "flash_freq": args.flash_freq or FLASH_FREQ,
        "flash_size": args.flash_size or FLASH_SIZE,
        "baud": args.baud,
        "parts": parts,
    }
    print(f"Параметры flash: {manifest['flash_mode']}/{manifest['flash_freq']}/"
          f"{manifest['flash_size']} (чип {manifest['chip']}).")
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
