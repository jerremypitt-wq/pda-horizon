#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Сборка прошивки Горизонта в 1 команду (шаг 1 из 2).

Запуск из корня репозитория (каталог не важен - скрипт сам встанет в корень):
  py -3.13 tools\\build\\build.py
или двойным кликом: build.bat

Делает по порядку: проверка Python -> PlatformIO (ставит сам) -> дерево
firmware-hz (клонирует сам) -> пакеты PIO -> оверлей (применяет сам) ->
сборка -> buildfs -> контроль выхлопа. Шаг 2 - tools/build/package.py.
"""
import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FLASHER_DIR = os.path.abspath(os.path.join(HERE, "..", "flasher"))

FW_DIR_NAME = "firmware-hz"
ENV = "elecrow-adv-35-tft"
FW_TAG = "v2.8.1.8e6a88d"
FW_URL = "https://github.com/meshtastic/firmware"
# Точный пин: этим ядром собрана рабочая 1.7.4 (build+buildfs+прошивка+загрузка).
# ensure_platformio ставит только если PIO нет вообще - существующие машины
# не трогаем, пин работает только для свежих установок.
PIO_SPEC = "platformio==6.1.19"
MIN_PY = (3, 10)
MAX_PY = (3, 14)  # 3.15+: зависимости PIO под него не проверены


class Fail(Exception):
    """Пользовательская ошибка: текст уже содержит что делать."""


def root_of_repo():
    return os.path.abspath(os.path.join(HERE, "..", ".."))


def python_ok(v):
    return (MIN_PY <= (v[0], v[1]) <= MAX_PY)


def check_python():
    v = sys.version_info
    if python_ok(v):
        print(f"Python: {v[0]}.{v[1]}.{v[2]} ({sys.executable}) - подходит.")
        return
    raise Fail(
        f"Python {v[0]}.{v[1]} не подходит (нужен {MIN_PY[0]}.{MIN_PY[1]}-"
        f"{MAX_PY[0]}.{MAX_PY[1]}, лучше 3.13).\n"
        "Что делать: поставь Python 3.13 (python.org/downloads, "
        "\"Windows installer (64-bit)\") и запусти: py -3.13 tools\\build\\build.py"
    )


def run_stream(cmd, cwd):
    """Запустить с живым выводом в консоль. Вернуть код возврата."""
    print(f"$ {' '.join(cmd)}")
    try:
        p = subprocess.run(cmd, cwd=cwd)
    except FileNotFoundError:
        raise Fail(f"не найдена программа: {cmd[0]}")
    return p.returncode


def run_capture(cmd, cwd=None):
    try:
        p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True,
                           encoding="utf-8", errors="replace")
    except FileNotFoundError:
        return 127, ""
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def ensure_platformio(py):
    code, out = run_capture([py, "-m", "platformio", "--version"])
    if code == 0:
        ver = out.strip().splitlines()[0] if out.strip() else "?"
        print(f"PlatformIO: {ver} - уже стоит.")
        return
    print(f"PlatformIO не найден - ставлю ({PIO_SPEC})...")
    code = run_stream([py, "-m", "pip", "install", PIO_SPEC], None)
    if code != 0:
        raise Fail("pip не смог поставить PlatformIO (нет интернета? VPN мешает?).\n"
                   "Что делать: выключи VPN и повтори команду.")
    code, out = run_capture([py, "-m", "platformio", "--version"])
    if code != 0:
        raise Fail("PlatformIO поставился, но не запускается. Пришли вывод целиком.")
    print(f"PlatformIO: {out.strip().splitlines()[0]} - поставлен.")


def ensure_tree(root):
    fw = os.path.join(root, FW_DIR_NAME)
    ini = os.path.join(fw, "platformio.ini")
    variant = os.path.join(fw, "variants", "esp32s3", "elecrow_panel", "platformio.ini")
    if os.path.isfile(ini) and os.path.isfile(variant):
        print(f"Дерево прошивки: {fw} - на месте.")
        return fw
    if os.path.exists(fw):
        raise Fail(f"Каталог {fw} есть, но это не дерево Meshtastic "
                   "(нет platformio.ini/variants).\n"
                   "Что делать: переименуй/удали его и повтори - дерево склонируется само.")
    print(f"Дерева прошивки нет - клонирую {FW_URL} ({FW_TAG})...")
    code, _ = run_capture(["git", "--version"])
    if code != 0:
        raise Fail("Нет git, а дерево прошивки надо склонировать.\n"
                   "Что делать: поставь Git (git-scm.com) и повтори.")
    code = run_stream(["git", "clone", "--branch", FW_TAG, "--depth", "1", FW_URL, fw], root)
    if code != 0 or not os.path.isfile(variant):
        raise Fail("Не смог склонировать дерево прошивки (сеть? VPN?).\n"
                   "Что делать: выключи VPN и повтори команду.")
    print("Дерево прошивки склонировано.")
    return fw


def overlay_ok(fw):
    h = os.path.join(fw, "src", "horizon", "horizon_target.h")
    try:
        with open(h, encoding="utf-8", errors="replace") as f:
            return "HORIZON_PACKET_LINK" in f.read()
    except OSError:
        return False


def ensure_overlay(root, fw, py):
    code = run_stream([py, "-m", "platformio", "pkg", "install", "-e", ENV], fw)
    if code != 0:
        raise Fail("pio pkg install не удался (сеть? VPN?).\n"
                   "Что делать: выключи VPN и повтори команду.")
    # Оверлей - ВСЕГДА заново: пропуск при «на месте» замораживал исходники
    # Горизонта в firmware-hz на первой применённой версии (эпоха 1.7.0) -
    # компилятор не видел правки 1.7.1+. overlay.py идемпотентен
    # (перекопирование + бекап tftSetup только в первый раз), повтор безопасен.
    print("Синхронизирую оверлей Горизонта (fw/overlay.py)...")
    ov = os.path.join(root, "fw", "overlay.py")
    code = run_stream([py, ov, "--fw", fw], root)
    if code != 0 or not overlay_ok(fw):
        raise Fail("Оверлей не применился. Пришли вывод целиком.")
    print("Оверлей синхронен (HORIZON_PACKET_LINK).")


# Игровой пакет по умолчанию (каталог games/<имя>: map.jpg + pois.txt).
# Пока игра одна - константа; вторую игру добавим флагом --game.
GAME_PACK = "zabytye-istorii"


def ensure_pillow(py):
    """Pillow для конвертера карты (перекод JPG->RGB565). Ставит, если нет."""
    code, _ = run_capture([py, "-c", "import PIL"])
    if code == 0:
        return
    print("Pillow не найден - ставлю (pip install pillow)...")
    code = run_stream([py, "-m", "pip", "install", "pillow"], None)
    if code != 0:
        raise Fail("pip не смог поставить Pillow (нет интернета? VPN мешает?).\n"
                   "Что делать: выключи VPN и повтори команду.")


def ensure_gamepack(root, fw):
    """Сконвертировать игровой пакет в data/hzmap (до buildfs)."""
    game = os.path.join(root, "games", GAME_PACK)
    if not os.path.isfile(os.path.join(game, "map.jpg")):
        raise Fail(f"Нет игрового пакета: {game} (нужны map.jpg + pois.txt).")
    out = os.path.join(fw, "data", "hzmap")
    tool = os.path.join(HERE, "poi_pack.py")
    code = run_stream([sys.executable, tool, "--game", game, "--out", out], root)
    if code != 0:
        raise Fail("Конвертер игрового пакета упал (красное выше - причина).")


def pio_build(fw, py):
    code = run_stream([py, "-m", "platformio", "run", "-e", ENV], fw)
    if code != 0:
        raise Fail("Сборка не удалась (красное выше - причина).\n"
                   "Что делать: если там про сеть - выключи VPN и повтори; "
                   "иначе пришли хвост вывода.")
    print("Сборка приложения: УСПЕХ.")
    code = run_stream([py, "-m", "platformio", "run", "-e", ENV, "-t", "buildfs"], fw)
    if code != 0:
        raise Fail("Сборка файловой системы (buildfs) не удалась. Пришли хвост вывода.")
    print("Сборка файловой системы: УСПЕХ.")


def verify_outputs(root):
    if FLASHER_DIR not in sys.path:
        sys.path.insert(0, FLASHER_DIR)
    import make_package as mp  # noqa: E402
    build = os.path.join(root, FW_DIR_NAME, ".pio", "build", ENV)
    need = [("firmware.bin", ["firmware-*.bin"]),
            ("bootloader.bin", []),
            ("partitions.bin", []),
            ("littlefs.bin", ["littlefs-*.bin"])]
    rows, missing = [], []
    for canonical, patterns in need:
        found = mp.resolve_build_file(build, canonical, patterns)
        if found is None:
            missing.append(canonical)
            continue
        sz = os.path.getsize(os.path.join(build, found))
        if sz <= 0:
            missing.append(f"{canonical} (пустой)")
            continue
        rows.append((canonical, found, sz))
    if missing:
        raise Fail("В сборке нет файлов: " + ", ".join(missing) + "\n"
                   "Что делать: повтори сборку; если повторяется - пришли вывод.")
    print("Выхлоп сборки:")
    for canonical, found, sz in rows:
        extra = "" if found == canonical else f" (как {found})"
        print(f"  {canonical} ({sz} Б){extra}")
    return rows


STAMP_FILE = "hz_fw_version.txt"


def stamp_version(root, ver):
    """Записать версию исходников в каталог сборки (package.py сверит со штампом).

    Штамп - единственная связь «бинарь <-> исходники»: package.py называет ZIP
    по версии ИСХОДНИКОВ, а бинарь может быть старше (забыли шаг 1).
    """
    if not ver:
        print("ПРЕДУПРЕЖДЕНИЕ: версию из исходников не прочитать - штамп не записан.")
        return
    build = os.path.join(root, FW_DIR_NAME, ".pio", "build", ENV)
    try:
        with open(os.path.join(build, STAMP_FILE), "w", encoding="utf-8") as f:
            f.write(ver + "\n")
    except OSError as e:
        print(f"ПРЕДУПРЕЖДЕНИЕ: штамп версии не записан: {e}")
        return
    print(f"Штамп версии сборки: {ver}.")


def _version_in(path):
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            m = re.search(r'kFirmwareVersion\s*=\s*"([^"]+)"', f.read())
    except OSError:
        return None
    return m.group(1) if m else None


def read_overlay_version(fw):
    """Версия Горизонта в ОВЕРЛЕЕ (то, что реально увидит компилятор).

    Штамп сборки берётся отсюда, а не из корня репозитория: иначе рассинхрон
    оверлея даст «честный» пакет со старьём внутри. None если не нашли.
    """
    return _version_in(os.path.join(fw, "src", "horizon", "HorizonStrings.h"))


def read_version(root):
    """Версия Горизонта из исходников (для имени пакета). None если не нашли."""
    return _version_in(os.path.join(root, "src", "horizon", "HorizonStrings.h"))


def main(argv=None, root=None):
    ap = argparse.ArgumentParser(description="Сборка прошивки Горизонта (шаг 1 из 2).")
    ap.parse_args(argv)
    root = root or root_of_repo()
    os.chdir(root)
    print(f"Корень репозитория: {root}")
    try:
        check_python()
        ensure_platformio(sys.executable)
        fw = ensure_tree(root)
        ensure_overlay(root, fw, sys.executable)
        ensure_pillow(sys.executable)
        ensure_gamepack(root, fw)
        pio_build(fw, sys.executable)
        verify_outputs(root)
        over_ver = read_overlay_version(fw)
        repo_ver = read_version(root)
        if over_ver and repo_ver and over_ver != repo_ver:
            print(f"ПРЕДУПРЕЖДЕНИЕ: оверлей v{over_ver}, а исходники v{repo_ver}?! "
                  "Скомпилировалось не то. Пришли вывод целиком.")
        stamp_version(root, over_ver or repo_ver)
    except Fail as e:
        print(f"\nОШИБКА: {e}")
        return 1
    except KeyboardInterrupt:
        print("\nПрервано пользователем.")
        return 1
    ver = read_version(root) or "?"
    print(f"\nШАГ 1 ГОТОВ. Версия Горизонта в исходниках: {ver}.")
    print("Шаг 2 (ZIP-пакет): py -3.13 tools\\build\\package.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())
