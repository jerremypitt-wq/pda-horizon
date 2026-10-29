#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""HorizonFlasher - полевая прошивка ПДА «Горизонт» (ESP32-S3) без VSCode и интернета.

В поле рядом с программой лежит папка пакета прошивки (flash.json + .bin,
готовится дома через make_package.py), плата подключена к USB-UART (J1).
Запуск - дальше всё само: порт определяется автоматически, прошивка льётся
по манифесту. Сборка одного .exe для поля: build_exe.bat.
"""
import argparse
import json
import os
import sys

APP = "HorizonFlasher"
MANIFEST = "flash.json"

VID_CH340 = 0x1A86  # USB-UART мост - приоритет автоопределения
VID_CP210X = 0x10C4  # запасной вариант мостов
VID_ESP_JTAG = 0x303A  # штатный USB-Serial/JTAG самого ESP32-S3 (без драйверов)
VID_PREFERRED = (VID_CH340, VID_CP210X, VID_ESP_JTAG)


def win_console_utf8():
    if os.name != "nt":
        return
    try:
        import ctypes

        ctypes.windll.kernel32.SetConsoleOutputCP(65001)
    except Exception:
        pass
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass


def base_dir():
    # Рядом с .exe (PyInstaller) или рядом со скриптом.
    if getattr(sys, "frozen", False):
        return os.path.dirname(os.path.abspath(sys.executable))
    return os.path.dirname(os.path.abspath(__file__))


def find_package(arg):
    if arg:
        d = os.path.abspath(arg)
        if not os.path.isdir(d):
            return None, f"папка пакета не найдена: {arg}"
        return d, None
    root = base_dir()
    cands = []
    fw = os.path.join(root, "firmware")
    if os.path.isfile(os.path.join(fw, MANIFEST)):
        cands.append(fw)
    try:
        names = sorted(os.listdir(root))
    except OSError:
        names = []
    for name in names:
        p = os.path.join(root, name)
        if name != "firmware" and os.path.isdir(p) and os.path.isfile(os.path.join(p, MANIFEST)):
            cands.append(p)
    if len(cands) == 1:
        return cands[0], None
    if not cands:
        return None, f"пакет прошивки не найден (рядом с программой нет папки с {MANIFEST})"
    return None, "найдено несколько пакетов - укажи один ключом --package:\n  " + "\n  ".join(cands)


def load_manifest(pkgdir):
    path = os.path.join(pkgdir, MANIFEST)
    try:
        with open(path, encoding="utf-8") as f:
            m = json.load(f)
    except FileNotFoundError:
        return None, f"в пакете нет {MANIFEST}"
    except json.JSONDecodeError as e:
        return None, f"{MANIFEST} не читается: {e}"
    if not isinstance(m, dict):
        return None, f"{MANIFEST}: корень должен быть объектом"
    for k in ("chip", "parts"):
        if k not in m:
            return None, f"{MANIFEST}: нет ключа '{k}'"
    parts = []
    for i, p in enumerate(m["parts"]):
        if not isinstance(p, dict):
            return None, f"{MANIFEST}: часть #{i} - не объект"
        try:
            off = int(str(p["offset"]), 0)
        except (KeyError, ValueError, TypeError):
            return None, f"{MANIFEST}: часть #{i}: плохой offset"
        fn = p.get("file", "")
        fp = os.path.join(pkgdir, fn)
        if not fn or not os.path.isfile(fp):
            return None, f"{MANIFEST}: часть #{i}: нет файла '{fn}'"
        parts.append({"offset": off, "file": fp, "tag": p.get("tag", "")})
    if not parts:
        return None, f"{MANIFEST}: пустой список частей"
    m["parts"] = parts
    return m, None


def list_serial_ports():
    try:
        from serial.tools import list_ports
    except ImportError:
        return None, "нет модуля pyserial (дома: pip install -r requirements.txt)"
    try:
        ports = sorted(list_ports.comports(), key=lambda p: p.device or "")
    except Exception as e:
        return None, f"не могу перечислить порты: {e}"
    return ports, None


def describe(p):
    s = p.device or "?"
    if p.vid is not None:
        s += f" [{p.vid:04X}:{(p.pid or 0):04X}]"
    if p.description:
        s += f" {p.description}"
    return s


def pick_port(cli_port):
    if cli_port:
        return cli_port, None
    ports, err = list_serial_ports()
    if err:
        return None, err
    ports = [p for p in ports if p.device]
    if not ports:
        return None, (
            "не найдено COM-портов. Проверь: кабель воткнут в J1 (нижний USB-C), "
            "кабель с данными (не charge-only), драйвер CH340 установлен."
        )
    pref = [p for p in ports if (p.vid or 0) in VID_PREFERRED]
    cands = pref or ports
    if len(cands) == 1:
        print(f"Порт определён автоматически: {describe(cands[0])}")
        return cands[0].device, None
    print("Найдено несколько портов:")
    for i, p in enumerate(cands, 1):
        mark = "  <-- похож на программатор" if (p.vid or 0) in VID_PREFERRED else ""
        print(f"  {i}. {describe(p)}{mark}")
    try:
        n = int(input("Номер порта платы: ").strip())
        return cands[n - 1].device, None
    except (ValueError, IndexError, EOFError, KeyboardInterrupt):
        return None, "порт не выбран"


def run_esptool(argv):
    try:
        import esptool
    except ImportError:
        return None, "нет модуля esptool (дома: pip install -r requirements.txt)"
    try:
        esptool.main(argv)
    except SystemExit as e:
        code = e.code
        return (0 if code in (None, 0) else int(code)), None
    except Exception as e:
        return None, f"esptool: {e}"
    return 0, None


def global_argv(m, port, baud):
    return [
        "--chip", str(m.get("chip", "esp32s3")),
        "--port", port,
        "--baud", str(baud),
        "--before", "default-reset",
        "--after", "hard-reset",
    ]


def build_write_argv(m, port, baud, skip_fs):
    argv = global_argv(m, port, baud) + [
        "write-flash",
        "-z",
        "--flash-mode", str(m.get("flash_mode", "qio")),
        "--flash-freq", str(m.get("flash_freq", "80m")),
        "--flash-size", str(m.get("flash_size", "16MB")),
    ]
    for p in m["parts"]:
        if skip_fs and p["tag"] == "fs":
            print(f"  пропуск (данные сохраняем): {os.path.basename(p['file'])}")
            continue
        argv += [hex(p["offset"]), p["file"]]
    return argv


def pause_if_needed(args):
    if os.name == "nt" and not args.no_pause:
        try:
            if sys.stdin.isatty():
                input("Нажми Enter, чтобы закрыть...")
        except (EOFError, KeyboardInterrupt):
            pass


def main(argv=None):
    win_console_utf8()
    ap = argparse.ArgumentParser(
        prog=APP, description="Полевая прошивка ПДА «Горизонт» по USB (J1), без VSCode и интернета."
    )
    ap.add_argument("--package", help="папка пакета прошивки (по умолчанию - автопоиск рядом)")
    ap.add_argument("--port", help="COM-порт (по умолчанию - автоопределение)")
    ap.add_argument("--baud", type=int, default=0,
                    help="скорость (по умолчанию из пакета; при ошибках - 460800/115200)")
    ap.add_argument("--list-ports", action="store_true", help="показать COM-порты и выйти")
    ap.add_argument("--skip-fs", action="store_true",
                    help="не трогать файловую систему (сохранить данные на устройстве)")
    ap.add_argument("--erase-all", action="store_true",
                    help="стереть весь чип перед записью (чистое устройство)")
    ap.add_argument("--no-pause", action="store_true", help="не ждать Enter в конце (для скриптов)")
    args = ap.parse_args(argv)

    if args.list_ports:
        ports, err = list_serial_ports()
        if err:
            print(f"ОШИБКА: {err}")
            return 1
        if not ports:
            print("COM-порты не найдены.")
        for p in ports:
            print(f"  {describe(p)}")
        return 0

    print(f"{APP}: прошивка ПДА «Горизонт»")
    pkgdir, err = find_package(args.package)
    if err:
        print(f"ОШИБКА: {err}")
        pause_if_needed(args)
        return 1
    m, err = load_manifest(pkgdir)
    if err:
        print(f"ОШИБКА: {err}")
        pause_if_needed(args)
        return 1
    baud = args.baud or int(m.get("baud", 921600))
    port, err = pick_port(args.port)
    if err:
        print(f"ОШИБКА: {err}")
        pause_if_needed(args)
        return 1

    print(f"Пакет: {m.get('name', os.path.basename(pkgdir))} ({pkgdir})")
    print(f"Порт: {port}, скорость: {baud}")
    print("Состав:")
    for p in m["parts"]:
        tag = " [FS]" if p["tag"] == "fs" else ""
        print(f"  {p['offset']:#08x}  {os.path.basename(p['file'])}{tag}")

    if args.erase_all:
        print("Стираю весь чип...")
        code, err = run_esptool(global_argv(m, port, baud) + ["erase-flash"])
        if err or code != 0:
            print(f"ОШИБКА: стирание не удалось: {err or f'esptool код {code}'}")
            pause_if_needed(args)
            return 1

    print("Записываю прошивку...")
    code, err = run_esptool(build_write_argv(m, port, baud, args.skip_fs))
    if err or code != 0:
        print(f"ОШИБКА: запись не удалась: {err or f'esptool код {code}'}")
        print("Подсказки: другой кабель/порт USB, --baud 460800, зажать BOOT на время старта.")
        pause_if_needed(args)
        return 1
    print("ГОТОВО: прошивка записана, устройство перезапущено.")
    pause_if_needed(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
