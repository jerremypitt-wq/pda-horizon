#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ZIP-пакет прошивки в 1 команду (шаг 2 из 2, после build.py).

Запуск из корня репозитория (каталог не важен):
  py -3.13 tools\\build\\package.py
  py -3.13 tools\\build\\package.py --out horizon-1.7.0-dio --flash-mode dio
или двойным кликом: package.bat

Имя пакета по умолчанию - horizon-<версия из исходников>. Тонкая обёртка
над tools/flasher/make_package.py с зафиксированными путями.
"""
import argparse
import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FLASHER_DIR = os.path.abspath(os.path.join(HERE, "..", "flasher"))


def load_build_mod():
    spec = importlib.util.spec_from_file_location(
        "hzbuild", os.path.join(HERE, "build.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main(argv=None, root=None):
    ap = argparse.ArgumentParser(description="ZIP-пакет прошивки (шаг 2 из 2).")
    ap.add_argument("--out", default="",
                    help="каталог/имя пакета (по умолчанию horizon-<версия>)")
    ap.add_argument("--name", default="", help="имя пакета в манифесте")
    ap.add_argument("--flash-mode", default="", help="оверрайд режима SPI flash")
    ap.add_argument("--flash-freq", default="", help="оверрайд частоты SPI flash")
    ap.add_argument("--flash-size", default="", help="оверрайд размера flash")
    ap.add_argument("--boot-app0", default="", help="реальный boot_app0.bin из фреймворка")
    ap.add_argument("--force", action="store_true", help="перезаписать каталог пакета")
    ap.add_argument("--no-zip", action="store_true", help="не делать ZIP (только папка)")
    args = ap.parse_args(argv)

    hzbuild = load_build_mod()
    root = root or hzbuild.root_of_repo()
    try:
        os.chdir(root)
    except OSError:
        print(f"ОШИБКА: нет каталога {root}")
        return 1
    print(f"Корень репозитория: {root}")

    ver = hzbuild.read_version(root)
    if args.out:
        out = args.out
    elif ver:
        out = f"horizon-{ver}"
    else:
        print("ОШИБКА: версию из исходников не прочитать - укажи имя явно: "
              "package.py --out horizon-X.Y.Z")
        return 1

    build = os.path.join(root, hzbuild.FW_DIR_NAME, ".pio", "build", hzbuild.ENV)
    if not os.path.isdir(build):
        print(f"ОШИБКА: нет каталога сборки: {build}\n"
              "Что делать: сначала шаг 1 - py -3.13 tools\\build\\build.py")
        return 1

    if FLASHER_DIR not in sys.path:
        sys.path.insert(0, FLASHER_DIR)
    import make_package as mp  # noqa: E402
    mk = ["--build", build, "--out", out]
    if not args.no_zip:
        mk.append("--zip")
    for k in ("name", "flash-mode", "flash-freq", "flash-size", "boot-app0", "force"):
        v = getattr(args, k.replace("-", "_"))
        if v is True:
            mk.append(f"--{k}")
        elif v:
            mk += [f"--{k}", v]
    code = mp.main(mk)
    if code != 0:
        return code
    try:
        with open(os.path.join(out, "flash.json"), encoding="utf-8") as f:
            fw = json.load(f).get("fw_version") or "unknown"
    except OSError:
        fw = "unknown"
    if fw == "unknown":
        print("ПРЕДУПРЕЖДЕНИЕ: версия прошивки в пакете неизвестна "
              "(бинарь собран не через build.bat?). Если исходники менялись "
              "после сборки - сначала шаг 1, иначе упакуется и прошьётся старье.")
    elif ver is None:
        print(f"В пакете прошивка v{fw} (исходники не прочитать - сравнить не с чем).")
    elif fw != ver:
        print(f"ОШИБКА: исходники v{ver}, а бинарь собран из v{fw} - пакет врёт.")
        print("Что делать: запусти шаг 1 (build.py), затем повтори упаковку.")
        if not args.no_zip:
            try:
                os.remove(out + ".zip")
                print("Врущий ZIP удалён, чтобы его случайно не прошили.")
            except OSError:
                pass
        return 1
    else:
        print(f"Версии совпали: прошивка v{fw}. Пакет честный.")
    if not args.no_zip:
        print("Дальше: HorizonFlasher.exe (поле) или "
              f"{sys.executable} tools\\flasher\\flasher_gui.py (исходники).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
