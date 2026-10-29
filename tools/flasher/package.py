#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ZIP-пакет прошивки: распаковка во временный каталог и проверка.

Общее для GUI (flasher_gui.py) и скриптов. Манифест лежит либо в корне ZIP,
либо в одной вложенной папке (так делает make_package.py --zip).
"""
import os
import sys
import tempfile
import zipfile

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

import flasher  # noqa: E402  (ядро: MANIFEST, load_manifest)


def safe_extract(zf, dest):
    """Распаковать весь архив, не давая выйти за dest (защита от zip-slip)."""
    base = os.path.abspath(dest)
    for m in zf.infolist():
        target = os.path.abspath(os.path.join(base, m.filename))
        if target != base and not target.startswith(base + os.sep):
            raise ValueError(f"опасный путь в ZIP: {m.filename}")
    zf.extractall(dest)


def find_pkgdir(root):
    """Найти каталог с flash.json: корень либо единственная вложенная папка."""
    if os.path.isfile(os.path.join(root, flasher.MANIFEST)):
        return root, None
    try:
        names = sorted(os.listdir(root))
    except OSError as e:
        return None, f"не читается временный каталог: {e}"
    subs = [
        d for d in names
        if os.path.isdir(os.path.join(root, d))
        and os.path.isfile(os.path.join(root, d, flasher.MANIFEST))
    ]
    if len(subs) == 1:
        return os.path.join(root, subs[0]), None
    if not subs:
        return None, f"в ZIP нет {flasher.MANIFEST} (ни в корне, ни во вложенной папке)"
    return None, "в ZIP несколько пакетов: " + ", ".join(subs)


class ZipPackage:
    """Распакованый и проверенный ZIP-пакет.

    Держи объект живым, пока нужен pkgdir (временный каталог удаляется
    в close()). Использование: ZipPackage(path) -> .ok / .error / .summary().
    """

    def __init__(self, zippath):
        self.zippath = os.path.abspath(zippath)
        self.tmp = None
        self.pkgdir = None
        self.manifest = None
        self.error = None
        try:
            if not os.path.isfile(self.zippath):
                raise ValueError(f"файл не найден: {zippath}")
            self.tmp = tempfile.TemporaryDirectory(prefix="hzflash-")
            with zipfile.ZipFile(self.zippath) as zf:
                bad = zf.testzip()
                if bad:
                    raise ValueError(f"ZIP повреждён (файл {bad})")
                safe_extract(zf, self.tmp.name)
            pkgdir, err = find_pkgdir(self.tmp.name)
            if err:
                raise ValueError(err)
            m, err = flasher.load_manifest(pkgdir)
            if err:
                raise ValueError(err)
            self.pkgdir = pkgdir
            self.manifest = m
        except zipfile.BadZipFile:
            self.error = "это не ZIP-файл (или файл повреждён)"
        except ValueError as e:
            self.error = str(e)
        except Exception as e:  # noqa: BLE001 - любой сюрприз распаковки в текст
            self.error = f"неожиданная ошибка: {e}"
        if self.error:
            self.close()

    @property
    def ok(self):
        return self.error is None

    def summary(self):
        m = self.manifest
        lines = [
            f"Пакет: {m.get('name', '?')}  (из {os.path.basename(self.zippath)})",
            f"Прошивка: v{m.get('fw_version', '?')}",
            f"Чип: {m.get('chip', '?')}, "
            f"flash {m.get('flash_mode', '?')}/{m.get('flash_freq', '?')}/"
            f"{m.get('flash_size', '?')}, скорость {m.get('baud', '?')}",
        ]
        total = 0
        for p in m["parts"]:
            sz = os.path.getsize(p["file"])
            total += sz
            tag = " [FS]" if p["tag"] == "fs" else ""
            lines.append(f"  {p['offset']:#08x}  {os.path.basename(p['file'])} ({sz} Б){tag}")
        lines.append(f"Всего к записи: {total} Б")
        return "\n".join(lines)

    def close(self):
        try:
            if self.tmp is not None:
                self.tmp.cleanup()
        except Exception:
            pass
        self.tmp = None
        self.pkgdir = None
        self.manifest = None

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()
