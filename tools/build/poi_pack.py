#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Конвертер игрового пакета: map.jpg + pois.txt -> map.rgb565 + map_preview.rgb565 + pois.csv.

Запуск (из корня репозитория, каталог не важен):
  py -3.13 tools\\build\\poi_pack.py --game games/zabytye-istorii --out <каталог>
Исторический/ручной конвертер старых карт; build.py с 1.14.0 его не вызывает.
Для новых проектов используй HorizonMapStudio: карта не встраивается в прошивку.

Проверки (лимиты из docs/MAP_ORDER.md + docs/MAP.md): JPG 3:2 от 480x320 до 960x640;
типы строго из списка 5; имя <=20, комментарий <=150 символов; X 0-479, Y 0-319 для старых pois.txt;
без '|' и переводов строк; точек <=50. Координаты переводятся в тысячные доли,
ID стартовых точек: B001, B002, ... Для ручного запуска нужен Pillow.
"""
import argparse
import os
import re
import struct
import sys

MAP_W, MAP_H = 720, 480  # максимум нового файла на ПДА
LEGACY_W, LEGACY_H = 480, 320
MAX_SOURCE_W, MAX_SOURCE_H = 960, 640  # 960x640 старых проектов уменьшаем при записи
MAX_IMAGE_BYTES = 8 * 1024 * 1024


def valid_source_size(w, h):
    return LEGACY_W <= w <= MAX_SOURCE_W and 3 * h == 2 * w


def output_size(w, h):
    if not valid_source_size(w, h):
        raise PoiError(f"карта {w}x{h}: нужен JPG 3:2 от 480×320 до 960×640")
    if w > MAP_W:
        return MAP_W, MAP_H
    return w, h

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
    """JPG 3:2 -> RGB565 LE (не более 720x480) и обзор 480x320."""
    try:
        from PIL import Image  # noqa: E402 - тяжёлая зависимость, только здесь
    except ImportError:
        raise PoiError("нет Pillow (pip install pillow) - нечем перекодировать карту")
    try:
        file_size = os.path.getsize(src)
    except OSError as e:
        raise PoiError(f"не открыть карту {src}: {e}") from e
    if file_size > MAX_IMAGE_BYTES:
        raise PoiError("JPG слишком большой (максимум 8 МБ)")
    try:
        with Image.open(src) as source:
            if source.format != "JPEG":
                raise PoiError("нужен файл JPG")
            target = output_size(*source.size)  # ДО декодирования (защита от больших карт)
            im = source.convert("RGB")
    except (OSError, ValueError, Image.DecompressionBombError) as e:
        raise PoiError(f"не открыть карту {src}: {e}") from e
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
    if im.size != target:
        im = im.resize(target, Image.Resampling.LANCZOS)
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
    ap.add_argument("--poi-coordinates", default="480x320",
                    help="координаты pois.txt: 480x320 (по умолчанию), map (как JPG) или WxH 3:2")
    args = ap.parse_args(argv)
    try:
        if args.poi_coordinates == "map":
            from PIL import Image
            try:
                with Image.open(os.path.join(args.game, "map.jpg")) as img:
                    coords = img.size
            except OSError as e:
                raise PoiError(f"не прочитать размеры JPG: {e}") from e
        else:
            match = re.fullmatch(r"(\d+)x(\d+)", args.poi_coordinates)
            if not match:
                raise PoiError("--poi-coordinates: укажи map или WxH (например, 624x416)")
            coords = tuple(map(int, match.groups()))
        if not valid_source_size(*coords):
            raise PoiError("--poi-coordinates: нужны размеры 3:2 от 480×320 до 960×640")
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
