#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Проверки poi_pack.py (конвертер игрового пакета). Нужен Pillow."""
import importlib.util
import os
import sys
import tempfile

TESTS = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.abspath(os.path.join(TESTS, ".."))
REPO = os.path.abspath(os.path.join(BUILD_DIR, "..", ".."))
TMP = os.path.join(tempfile.gettempdir(), "hztest-poi")
PASS = []


def check(name, cond, extra=""):
    assert cond, f"FAIL: {name} {extra}"
    PASS.append(name)
    print(f"  ok: {name}")


def load_mod(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


try:
    from PIL import Image
except ImportError:
    print("Нет Pillow - тесты poi пропущены (pip install pillow).")
    sys.exit(0)

poi = load_mod("hzpoi", os.path.join(BUILD_DIR, "poi_pack.py"))

print("== poi_pack: разбор pois.txt ==")


def write_txt(body):
    d = tempfile.mkdtemp(dir=TMP)
    p = os.path.join(d, "pois.txt")
    with open(p, "w", encoding="utf-8") as f:
        f.write(body)
    return p


os.makedirs(TMP, exist_ok=True)
p = write_txt("1. База, Бар, x185, y55, Мастер, Место отдыха.\n"
              "2. Схрон, Схрон 3, x0, y319, Разведка, \n")
rows = poi.parse_pois_txt(p)
check("parse-count", len(rows) == 2, str(rows))
check("parse-fields", rows[0]["type"] == 1 and rows[0]["name"] == "Бар"
      and rows[0]["sender"] == "Мастер" and rows[1]["type"] == 4, str(rows))
check("parse-promille", rows[0]["x"] == round(185 * 1000 / 479)
      and rows[1]["x"] == 0 and rows[1]["y"] == 1000, str(rows))
for bad, tag in (
        ("1. Замок, А, x1, y1, М, К\n", "тип"),
        ("1. База, " + "Я" * 21 + ", x1, y1, М, К\n", "имя"),
        ("1. База, А, x480, y1, М, К\n", "X"),
        ("1. База, А, x1, y320, М, К\n", "Y"),
        ("1. База, А, x1, y1, М, " + "Я" * 151 + "\n", "коммент"),
        ("1. База, А|Б, x1, y1, М, К\n", "палка"),
        ("мусор без формата\n", "формат")):
    try:
        poi.parse_pois_txt(write_txt(bad))
        check("reject-" + tag, False, "no PoiError")
    except poi.PoiError as e:
        check("reject-" + tag, True, str(e)[:40])
try:
    poi.parse_pois_txt(os.path.join(TMP, "нет-такого.txt"))
    check("reject-missing", False, "no PoiError")
except poi.PoiError:
    check("reject-missing", True)

print("== poi_pack: карта и сквозной прогон ==")
gd = os.path.join(TMP, "game")
os.makedirs(gd, exist_ok=True)
Image.new("RGB", (960, 640), (10, 20, 30)).save(os.path.join(gd, "map.jpg"))
with open(os.path.join(gd, "pois.txt"), "w", encoding="utf-8") as f:
    f.write("1. Опасность, Воронка, x479, y0, Мастер, Не подходить.\n")
out = os.path.join(TMP, "out")
check("main-exit", poi.main(["--game", gd, "--out", out]) == 0)
check("main-map", os.path.getsize(os.path.join(out, "map.rgb565")) == 960 * 640 * 2)
check("main-preview", os.path.getsize(os.path.join(out, "map_preview.rgb565")) == 480 * 320 * 2)
with open(os.path.join(out, "pois.csv"), encoding="utf-8") as f:
    csv = f.read()
check("main-csv", csv == "B001|2|Воронка|1000|0|Мастер|Не подходить.\n", csv)
gd2 = os.path.join(TMP, "game2")
os.makedirs(gd2, exist_ok=True)
Image.new("RGB", (100, 100)).save(os.path.join(gd2, "map.jpg"))
with open(os.path.join(gd2, "pois.txt"), "w", encoding="utf-8") as f:
    f.write("1. База, А, x1, y1, М, К\n")
check("main-badmap", poi.main(["--game", gd2, "--out", os.path.join(TMP, "out2")]) == 1)

print("== poi_pack: настоящий пакет ЗИ ==")
zigame = os.path.join(REPO, "games", "zabytye-istorii")
ziout = os.path.join(TMP, "ziout")
check("zi-exit", poi.main(["--game", zigame, "--out", ziout]) == 0)
with open(os.path.join(ziout, "pois.csv"), encoding="utf-8") as f:
    lines = f.read().splitlines()
check("zi-count", len(lines) == 3, str(lines))
check("zi-first", lines[0].startswith("B001|1|Бар|386|172|Мастер|"), lines[0])
check("zi-map", os.path.getsize(os.path.join(ziout, "map.rgb565")) == 960 * 640 * 2)
check("zi-preview", os.path.getsize(os.path.join(ziout, "map_preview.rgb565")) == 480 * 320 * 2)
check("new-coordinates", poi.parse_pois_txt(write_txt("1. База, А, x959, y639, М, К\n"), 960, 640)[0]["x"] == 1000)

print(f"\nВСЕГО ПРОШЛО: {len(PASS)}")
