#!/usr/bin/env python3
"""Оверлей ПДА «Горизонт» поверх дерева прошивки Meshtastic.

Только копирование файлов - БЕЗ правок platformio.ini и БЕЗ форка прошивки.
Сборка после наложения - стоковым env: pio run -e elecrow-adv-35-tft.

Что копируется (подробности в fw/README.md):
 1. src/horizon/*.h|*.cpp|*.c       -> <fw>/src/horizon/
 2. fw/vendor/**                     -> <fw>/src/horizon/** (NodeStore, с подпапками)
 3. variants/elecrow-adv-35/config.h -> <fw>/src/horizon/config.h (класс панели)
 4. fw/horizon_target_firmware.h      -> <fw>/src/horizon/horizon_target.h (вкус!)
 5. fw/tftSetup_horizon.cpp          -> <fw>/src/graphics/tftSetup.cpp (бекап .stock)
 6. Boot-заливка в установленном device-ui: зелёный -> чёрный (идемпотентно).

Использование (Windows, PowerShell, из клона прошивки на теге v2.8.1):
  pio pkg install -e elecrow-adv-35-tft
  python C:\\path\\to\\pda-horizon\\fw\\overlay.py --fw .
  pio run -e elecrow-adv-35-tft

Откат: overlay.py --fw . --revert
"""
import argparse
import glob
import os
import shutil
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FW_PIN_TAG = "v2.8.1"
DEVICEUI_PIN = "f77bb709cd83449467520d14344308c63690b624"  # device-ui в прошивке v2.8.1
ENV = "elecrow-adv-35-tft"

BOOT_OLD = "color565(0x3D, 0xDA, 0x83)"
BOOT_NEW = "color565(0x00, 0x00, 0x00)"


def copy_tree(src, dst, dry):
    n = 0
    for root, _, files in os.walk(src):
        for fn in files:
            s = os.path.join(root, fn)
            rel = os.path.relpath(s, src)
            d = os.path.join(dst, rel)
            if not dry:
                os.makedirs(os.path.dirname(d), exist_ok=True)
                shutil.copy2(s, d)
            print(("  [dry] " if dry else "  copy ") + rel)
            n += 1
    return n


def main():
    ap = argparse.ArgumentParser(description="Horizon overlay over meshtastic-firmware tree")
    ap.add_argument("--fw", required=True, help="корень дерева прошивки (клон на теге v2.8.1)")
    ap.add_argument("--revert", action="store_true", help="откатить оверлей")
    ap.add_argument("--dry-run", action="store_true", help="только показать действия")
    ap.add_argument("--force", action="store_true", help="игнорировать несовпадение пина device-ui")
    ap.add_argument("--no-bootpatch", action="store_true", help="не трогать libdeps (boot-заливка)")
    args = ap.parse_args()

    fw = os.path.abspath(args.fw)
    if not os.path.isdir(os.path.join(fw, "src", "graphics")):
        sys.exit(f"ОШИБКА: {fw} не похоже на дерево прошивки (нет src/graphics)")

    horizon_dst = os.path.join(fw, "src", "horizon")
    tft_dst = os.path.join(fw, "src", "graphics", "tftSetup.cpp")
    tft_stock = tft_dst + ".stock"

    if args.revert:
        if os.path.isfile(tft_stock):
            if not args.dry_run:
                shutil.copy2(tft_stock, tft_dst)
                os.remove(tft_stock)
            print("  revert src/graphics/tftSetup.cpp из .stock")
        else:
            print("  нет tftSetup.cpp.stock - клей не откатываю")
        if os.path.isdir(horizon_dst):
            if not args.dry_run:
                shutil.rmtree(horizon_dst)
            print("  удалён src/horizon")
        print("Готово. Boot-патч в .pio/libdeps НЕ откатывается (безвреден для стока).")
        return

    # Проверка пина device-ui: наши #ifdef заточены под апстрим f77bb70.
    ini = os.path.join(fw, "platformio.ini")
    try:
        with open(ini, encoding="utf-8") as f:
            ini_txt = f.read()
    except OSError:
        ini_txt = ""
    if DEVICEUI_PIN not in ini_txt:
        msg = (f"ВНИМАНИЕ: в {ini} нет пина device-ui {DEVICEUI_PIN[:7]} "
               f"(ожидалась прошивка {FW_PIN_TAG}). Порт заточен под этот апстрим.")
        if args.force:
            print(msg + " Продолжаю по --force.")
        else:
            sys.exit("ОШИБКА: " + msg + " Проверьте тег либо добавьте --force.")

    total = 0
    print("1) src/horizon -> src/horizon:")
    total += copy_tree(os.path.join(REPO, "src", "horizon"), horizon_dst, args.dry_run)
    print("2) fw/vendor -> src/horizon (NodeStore):")
    total += copy_tree(os.path.join(REPO, "fw", "vendor"), horizon_dst, args.dry_run)
    print("3) config.h (класс панели LGFX_ELECROW_ADV_35):")
    if not args.dry_run:
        shutil.copy2(os.path.join(REPO, "variants", "elecrow-adv-35", "config.h"),
                     os.path.join(horizon_dst, "config.h"))
    print("  copy config.h")
    total += 1
    print("4) вкус прошивки (HORIZON_PACKET_LINK):")
    if not args.dry_run:
        shutil.copy2(os.path.join(REPO, "fw", "horizon_target_firmware.h"),
                     os.path.join(horizon_dst, "horizon_target.h"))
    print("  copy horizon_target.h (firmware)")
    total += 1
    print("5) клей tftSetup:")
    glue = os.path.join(REPO, "fw", "tftSetup_horizon.cpp")
    need_backup = os.path.isfile(tft_dst) and not os.path.isfile(tft_stock)
    if need_backup and not args.dry_run:
        with open(tft_dst, "rb") as f:
            cur = f.read()
        with open(glue, "rb") as f:
            new = f.read()
        if cur != new:
            shutil.copy2(tft_dst, tft_stock)
            print("  backup tftSetup.cpp.stock")
        else:
            print("  клей уже на месте, бекап не нужен")
    elif os.path.isfile(tft_stock):
        print("  бекап .stock уже есть")
    if not args.dry_run:
        shutil.copy2(glue, tft_dst)
    print("  copy tftSetup.cpp (glue)")
    total += 1

    if not args.no_bootpatch:
        print("6) boot-заливка в установленном device-ui:")
        cands = glob.glob(os.path.join(fw, ".pio", "libdeps", ENV, "*", "include", "graphics",
                                       "driver", "LGFXDriver.h"))
        if not cands:
            print("  ПРОПУСК: libdeps не установлены. Сначала "
                  f"'pio pkg install -e {ENV}', затем повторить overlay.py.")
        for path in cands:
            with open(path, encoding="utf-8") as f:
                s = f.read()
            if BOOT_OLD in s:
                if not args.dry_run:
                    with open(path, "w", encoding="utf-8", newline="") as f:
                        f.write(s.replace(BOOT_OLD, BOOT_NEW))
                print(f"  patched {path}")
            else:
                print(f"  уже чёрный: {path}")

    print(f"Готово: {total} файлов. Дальше: pio run -e {ENV}")


if __name__ == "__main__":
    main()
