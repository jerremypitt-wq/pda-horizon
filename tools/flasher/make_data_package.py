#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Собрать ZIP карты/стартовых точек БЕЗ прошивки для HorizonFlasher.

python tools/flasher/make_data_package.py --partitions firmware-hz/.pio/build/elecrow-adv-35-tft/partitions.bin --game games/zabytye-istorii --out horizon-map-1.8.5.zip
"""
import argparse
import hashlib
import json
import os
import sys
import tempfile
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "build"))
import poi_pack  # noqa: E402
from make_package import parse_partitions_bin  # noqa: E402

NAMES = ("map.rgb565", "map_preview.rgb565", "pois.csv")


def main(argv=None):
    ap = argparse.ArgumentParser(description="ZIP обновления карты/точек без прошивки")
    ap.add_argument("--partitions", required=True, help="partitions.bin именно от этой прошивки ПДА")
    ap.add_argument("--game", required=True, help="папка с map.jpg и pois.txt")
    ap.add_argument("--out", required=True, help="имя выходного ZIP")
    ap.add_argument("--poi-coordinates", choices=("480x320", "960x640"), default="480x320",
                    help="координаты pois.txt (старые точки — 480x320)")
    args = ap.parse_args(argv)
    offset, size, label = parse_partitions_bin(args.partitions)
    if not offset or not size or label.lower() not in ("littlefs", "spiffs") or offset % 4096 or size % 4096:
        ap.error("в partitions.bin нет подходящего раздела LittleFS (метка littlefs/spiffs)")
    with tempfile.TemporaryDirectory(prefix="hz-pack-") as tmp:
        code = poi_pack.main(["--game", args.game, "--out", tmp,
                              "--poi-coordinates", args.poi_coordinates])
        if code:
            return code
        files = {}
        for name in NAMES:
            data = (Path(tmp) / name).read_bytes()
            files[name] = {"sha256": hashlib.sha256(data).hexdigest(), "size": len(data)}
        manifest = {"kind": "map-data", "chip": "esp32s3", "name": Path(args.out).stem,
                    "fs_offset": offset, "fs_size": size, "fs_label": label.lower(), "files": files}
        out = Path(args.out).resolve()
        out.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(out, "w", compression=zipfile.ZIP_DEFLATED) as zf:
            zf.writestr("data.json", json.dumps(manifest, ensure_ascii=False, indent=2))
            for name in NAMES:
                zf.write(Path(tmp) / name, name)
    print(f"Готов пакет данных: {out} (раздел {offset:#x}, {size} Б)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
