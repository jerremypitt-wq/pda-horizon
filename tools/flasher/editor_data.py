#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Проект настольного редактора и безопасная отправка стартовых точек на ПДА.

Формат .hzmap — ZIP с JPG и JSON. Точки в проекте — в пикселях 960×640;
на ПДА (pois.csv) переводятся в тысячные доли, как точки экрана ПДА.
"""
import hashlib
import io
import json
import os
import sys
import tempfile
import zipfile
from pathlib import Path

import data_update
import flasher
from make_package import parse_partitions_bin

# В PyInstaller poi_pack включается через --paths ../build. В исходниках
# каталог build лежит рядом с flasher и добавляется один раз перед импортом.
try:
    import poi_pack
except ImportError:
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "build"))
    import poi_pack

WIDTH, HEIGHT = 960, 640
MAX_IMAGE_BYTES = 16 * 1024 * 1024
MAX_PROJECT_BYTES = 1024 * 1024
PROJECT_VERSION = 1
COLORS = ("#e8c33a", "#3fa34d", "#c0392b", "#8e44ad", "#2e86de")


def validate_image(data):
    from PIL import Image
    if not isinstance(data, bytes) or not 0 < len(data) <= MAX_IMAGE_BYTES:
        raise ValueError("файл карты пустой или слишком большой (максимум 16 МБ)")
    try:
        with Image.open(io.BytesIO(data)) as image:
            if image.format != "JPEG" or image.size != (WIDTH, HEIGHT):
                raise ValueError("нужен JPG ровно 960×640 пикселей")
            image.load()
    except (OSError, SyntaxError) as e:
        raise ValueError(f"JPG повреждён: {e}") from e
    return data


def validate_point(point):
    if not isinstance(point, dict) or set(point) != {"x", "y", "type", "name", "sender", "comment"}:
        raise ValueError("неверный формат точки")
    x, y, typ = point["x"], point["y"], point["type"]
    if any(isinstance(v, bool) or not isinstance(v, int) for v in (x, y, typ)):
        raise ValueError("координаты и тип точки должны быть целыми числами")
    if not (0 <= x < WIDTH and 0 <= y < HEIGHT and 0 <= typ < len(poi_pack.TYPES)):
        raise ValueError("точка выходит за границу карты или тип неизвестен")
    for key, limit, optional in (("name", 20, False), ("sender", 40, False), ("comment", 150, True)):
        text = point[key]
        if not isinstance(text, str) or len(text) > limit or (not optional and not text.strip()):
            raise ValueError(f"поле «{key}»: нужно 1–{limit} символов" if not optional else
                             f"поле «{key}»: максимум {limit} символов")
        if any(ch in text for ch in "|\r\n"):
            raise ValueError(f"поле «{key}»: нельзя использовать | и переносы строк")
    return point


class MapProject:
    def __init__(self, image, points=None):
        self.image = validate_image(image)
        self.points = [validate_point(p.copy()).copy() for p in (points or [])]
        if len(self.points) > poi_pack.MAX_POINTS:
            raise ValueError("не больше 50 стартовых точек")

    @classmethod
    def from_jpg(cls, path):
        """JPG + старый pois.txt рядом (если есть): сохранить прежние B001…"""
        path = Path(path)
        image = path.read_bytes()
        source = path.with_name("pois.txt")
        points = []
        if source.is_file():
            try:
                rows = poi_pack.parse_pois_txt(str(source))  # старые координаты 480×320
            except poi_pack.PoiError as e:
                raise ValueError(f"рядом с картой найден pois.txt, но он не читается: {e}") from e
            for p in rows:
                points.append({"x": round(p["x"] * (WIDTH - 1) / 1000),
                               "y": round(p["y"] * (HEIGHT - 1) / 1000),
                               "type": p["type"], "name": p["name"],
                               "sender": p["sender"], "comment": p["comment"]})
        return cls(image, points)

    def snapshot(self):
        return MapProject(self.image, self.points)

    def add(self, point):
        if len(self.points) >= poi_pack.MAX_POINTS:
            raise ValueError("лимит — 50 стартовых точек")
        self.points.append(validate_point(point.copy()).copy())

    def replace(self, index, point):
        self.points[index] = validate_point(point.copy()).copy()

    def save(self, path):
        """Атомарно сохранить самодостаточный проект (JPG внутри ZIP)."""
        target = Path(path)
        target.parent.mkdir(parents=True, exist_ok=True)
        fd, tmp = tempfile.mkstemp(prefix=".hzmap-", suffix=".tmp", dir=target.parent)
        os.close(fd)
        try:
            with zipfile.ZipFile(tmp, "w", compression=zipfile.ZIP_DEFLATED) as zf:
                zf.writestr("map.jpg", self.image)
                zf.writestr("points.json", json.dumps({"format": PROJECT_VERSION,
                                                       "points": self.points}, ensure_ascii=False))
            os.replace(tmp, target)
        finally:
            if os.path.exists(tmp):
                os.unlink(tmp)

    @classmethod
    def load(cls, path):
        try:
            with zipfile.ZipFile(path) as zf:
                if len(zf.namelist()) != 2 or set(zf.namelist()) != {"map.jpg", "points.json"}:
                    raise ValueError("проект должен содержать только map.jpg и points.json")
                if zf.getinfo("map.jpg").file_size > MAX_IMAGE_BYTES or zf.getinfo("points.json").file_size > MAX_PROJECT_BYTES:
                    raise ValueError("проект слишком большой")
                image = zf.read("map.jpg")
                raw = json.loads(zf.read("points.json").decode("utf-8"))
            if not isinstance(raw, dict) or raw.get("format") != PROJECT_VERSION or not isinstance(raw.get("points"), list):
                raise ValueError("неверная версия или структура проекта")
            return cls(image, raw["points"])
        except (OSError, zipfile.BadZipFile, UnicodeError, json.JSONDecodeError) as e:
            raise ValueError(f"проект повреждён: {e}") from e


def prepare_package(project, offset, size, label, out):
    """Собрать в out только совместимые с прошивкой map/preview/pois."""
    if not isinstance(project, MapProject):
        raise ValueError("сначала открой карту")
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    (out / "map.jpg").write_bytes(project.image)
    poi_pack.convert_map(str(out / "map.jpg"), str(out / "map.rgb565"), str(out / "map_preview.rgb565"))
    rows = [{"type": p["type"], "name": p["name"],
             "x": poi_pack.to_promille(p["x"], WIDTH - 1),
             "y": poi_pack.to_promille(p["y"], HEIGHT - 1),
             "sender": p["sender"], "comment": p["comment"]} for p in project.points]
    poi_pack.write_csv(rows, str(out / "pois.csv"))
    files = {}
    for name in data_update.FILES["map"] + data_update.FILES["points"]:
        body = (out / name).read_bytes()
        files[name] = {"sha256": hashlib.sha256(body).hexdigest(), "size": len(body)}
    m = {"kind": "map-data", "chip": "esp32s3", "name": "Редактор карты",
         "fs_offset": offset, "fs_size": size, "fs_label": label.lower(), "files": files}
    return data_update.validate_manifest(m, out)


def upload_project(project, port, baud, progress=None, backup_dir=None):
    """Без ZIP прошивки: узнать раздел с ПДА, затем использовать штатный безопасный путь."""
    project = project.snapshot()  # неизменяемая копия на время долгой записи
    base = flasher.global_argv({"chip": "esp32s3"}, port, baud)
    first = base[:-2] + ["--after", "no-reset"]
    with tempfile.TemporaryDirectory(prefix="hz-editor-") as tmp:
        part = Path(tmp) / "partitions.bin"
        if progress:
            progress("Чтение таблицы разделов с ПДА...")
        data_update._esptool(first + ["read-flash", "0x8000", "0x1000", str(part)], "Чтение таблицы")
        offset, size, label = parse_partitions_bin(part)
        if progress:
            progress("Подготовка карты и точек...")
        m = prepare_package(project, offset, size, label, Path(tmp) / "data")
        # Первое чтение оставило ПДА в загрузчике. Дальше тот же защищённый
        # алгоритм: повторная сверка раздела, backup, patch, запись, readback.
        return data_update.update_device(m, Path(tmp) / "data", {"map", "points"},
                                         port, baud, progress=progress, backup_dir=backup_dir,
                                         initial_no_reset=True)
