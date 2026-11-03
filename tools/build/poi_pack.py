#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Конвертер игрового пакета: map.jpg + pois.txt -> map.rgb565 + map_preview.rgb565 + pois.csv.

Запуск (из корня репозитория, каталог не важен):
  py -3.13 tools\\build\\poi_pack.py --game games/zabytye-istorii --out <каталог>
Обычно вызывается сам из build.py (ensure_gamepack) - руками не нужен.

Проверки (лимиты из docs/MAP_ORDER.md + docs/MAP.md): карта ровно 960x640;
типы строго из списка 5; имя <=20, комментарий <=150 символов; X 0-479, Y 0-319 для старых pois.txt;
без '|' и переводов строк; точек <=50. Координаты переводятся в тысячные доли,
ID стартовых точек: B001, B002, ... Нужен Pillow (build.py ставит сам).
"""
import argparse
import os
import re
import struct
import sys

MAP_W, MAP_H = 960, 640
LEGACY_W, LEGACY_H = 480, 320
TYPES = ["Точка интереса", "База", "Опасность", "Аномалия", "Схрон"]
MAX_POINTS = 50
MAX_NAME = 20
MAX_COMMENT = 150

LINE_RE = re.compile(r"^\d+\.\s*(.+?),\s*(.+?),\s*x(\d+),\s*y(\d+),\s*(.+?),\s*(.*)$")


class PoiError(Exception):
    """Ошибка конвертации с понятным текстом (показываем как есть)."""


def to_promille(px, vmax):
    return round(px * 1000 / vmax)


def parse_pois_txt(path, coord_w=LEGACY_W, coord_h=LEGACY_H):
    """Разобрать pois.txt поставщика. Вернуть список словарей. Кинуть PoiError."""
    try:
        with open(path, encoding="utf-8") as f:
            text = f.read()
    except OSError as e:
        raise PoiError(f"не прочитать {path}: {e}")
    pois = []
    for ln, raw in enumerate(text.splitlines(), 1):
        line = raw.strip().strip("\ufeff")
        if not line:
            continue
        m = LINE_RE.match(line)
        if not m:
            raise PoiError(f"строка {ln}: не понял формат (нужно 'N. Тип, Название, xNNN, yNNN, "
                           f"Отправитель, Комментарий'): {line[:60]}")
        typ, name, xs, ys, sender, comment = (g.strip() for g in m.groups())
        x, y = int(xs), int(ys)
        if typ not in TYPES:
            raise PoiError(f"строка {ln}: тип '{typ}' - нет в списке пяти "
                           f"({', '.join(TYPES)})")
        if len(name) == 0 or len(name) > MAX_NAME:
            raise PoiError(f"строка {ln}: имя '{name}' - нужно 1-{MAX_NAME} символов")
        if not 0 <= x <= coord_w - 1:
            raise PoiError(f"строка {ln}: X={x} - нужно 0-{coord_w - 1}")
        if not 0 <= y <= coord_h - 1:
            raise PoiError(f"строка {ln}: Y={y} - нужно 0-{coord_h - 1}")
        if not sender:
            raise PoiError(f"строка {ln}: пустой отправитель")
        if len(comment) > MAX_COMMENT:
            raise PoiError(f"строка {ln}: комментарий {len(comment)} символов - нужно до "
                           f"{MAX_COMMENT}")
        for field, fname in ((name, "имени"), (sender, "отправителе"), (comment, "комментарии")):
            if "|" in field or "\n" in field:
                raise PoiError(f"строка {ln}: символ '|' запрещён в {fname}")
        pois.append({"type": TYPES.index(typ), "name": name,
                     "x": to_promille(x, coord_w - 1), "y": to_promille(y, coord_h - 1),
                     "sender": sender, "comment": comment})
    if len(pois) > MAX_POINTS:
        raise PoiError(f"точек {len(pois)} - лимит {MAX_POINTS}")
    return pois


def convert_map(src, dst, preview=None):
    """JPG -> RGB565 LE 960x640 и сглаженный обзор 480x320."""
    try:
        from PIL import Image  # noqa: E402 - тяжёлая зависимость, только здесь
    except ImportError:
        raise PoiError("нет Pillow (pip install pillow) - нечем перекодировать карту")
    try:
        im = Image.open(src).convert("RGB")
    except Exception as e:
        raise PoiError(f"не открыть карту {src}: {e}")
    if im.size != (MAP_W, MAP_H):
        raise PoiError(f"карта {im.size[0]}x{im.size[1]} - нужна ровно {MAP_W}x{MAP_H}")
    def write_rgb565(image, path):
        width, height = image.size
        px = image.load()
        out = bytearray(width * height * 2)
        for y in range(height):
            for x in range(width):
                r, g, b = px[x, y]
                struct.pack_into("<H", out, (y * width + x) * 2,
                                 ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))
        with open(path, "wb") as f:
            f.write(out)
        return len(out)

    if preview is not None:
        write_rgb565(im.resize((LEGACY_W, LEGACY_H), Image.Resampling.LANCZOS), preview)
    return write_rgb565(im, dst)


def write_csv(pois, dst):
    with open(dst, "w", encoding="utf-8", newline="\n") as f:
        for i, p in enumerate(pois, 1):
            f.write("B%03d|%d|%s|%d|%d|%s|%s\n" % (
                i, p["type"], p["name"], p["x"], p["y"], p["sender"], p["comment"]))


def main(argv=None):
    ap = argparse.ArgumentParser(description="Конвертер игрового пакета (карта+точки).")
    ap.add_argument("--game", required=True, help="каталог пакета (map.jpg + pois.txt)")
    ap.add_argument("--out", required=True, help="выходной каталог (map.rgb565 + map_preview.rgb565 + pois.csv)")
    ap.add_argument("--poi-coordinates", choices=("480x320", "960x640"),
                    default="480x320", help="размер системы координат pois.txt (по умолчанию старые 480x320)")
    args = ap.parse_args(argv)
    try:
        coords = (LEGACY_W, LEGACY_H) if args.poi_coordinates == "480x320" else (MAP_W, MAP_H)
        pois = parse_pois_txt(os.path.join(args.game, "pois.txt"), *coords)
        os.makedirs(args.out, exist_ok=True)
        nbytes = convert_map(os.path.join(args.game, "map.jpg"),
                             os.path.join(args.out, "map.rgb565"),
                             os.path.join(args.out, "map_preview.rgb565"))
        write_csv(pois, os.path.join(args.out, "pois.csv"))
    except PoiError as e:
        print(f"ОШИБКА: {e}")
        return 1
    print(f"Игропакет готов: карта {nbytes} Б + точек {len(pois)} -> {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
