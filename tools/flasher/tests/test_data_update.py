"""Integration checks with a real LittleFS image and a mocked ESP32 flash bus."""
import hashlib
import json
import os
import subprocess
import struct
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
import data_update  # noqa: E402
import flasher  # noqa: E402
import package as zpkg  # noqa: E402
from littlefs import LittleFS, UserContext  # noqa: E402

SIZE = 0x360000
OFFSET = 0xC90000


class DataUpdateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.tmp.name)
        cls.pkg = cls.root / "pack"
        cls.pkg.mkdir()
        cls.new = {"map.rgb565": b"M" * (720 * 480 * 2),
                   "map_preview.rgb565": b"V" * (480 * 320 * 2),
                   "pois.csv": "B001|1|Бар|386|172|Мастер|\n".encode()}
        files = {}
        for name, value in cls.new.items():
            (cls.pkg / name).write_bytes(value)
            files[name] = {"sha256": hashlib.sha256(value).hexdigest(), "size": len(value)}
        cls.manifest = {"kind": "map-data", "chip": "esp32s3", "fs_offset": OFFSET,
                        "fs_size": SIZE, "fs_label": "spiffs", "name": "test", "files": files}
        (cls.pkg / "data.json").write_text(json.dumps(cls.manifest), encoding="utf-8")
        ctx = UserContext(SIZE)
        fs = LittleFS(context=ctx, mount=False, block_size=4096,
                      block_count=SIZE // 4096, read_size=16, prog_size=16,
                      cache_size=512, lookahead_size=128)
        fs.format()
        fs.mount()
        fs.makedirs("/hzmap", exist_ok=True)
        fs.makedirs("/chats", exist_ok=True)
        cls.old = {"/hzmap/map.rgb565": b"old-map", "/hzmap/pois.csv": b"old-points",
                   "/hzmap/my.csv": b"my points", "/hzmap/net.csv": b"net points",
                   "/hzmap/del.csv": b"tombstones", "/chats/private.txt": b"private chat",
                   "/settings.txt": b"settings"}
        for path, value in cls.old.items():
            with fs.open(path, "wb") as f:
                f.write(value)
        fs.unmount()
        cls.original = bytes(ctx.buffer)
        cls.partition = struct.pack("<HBBII16sI", 0x50AA, 1, 0x82,
                                    OFFSET, SIZE, b"spiffs" + b"\0" * 10, 0)
        cls.partition += b"\xff" * (4096 - len(cls.partition))

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_patch_selective_and_preserve_unknown_files(self):
        for selected in ({"map"}, {"points"}, {"map", "points"}):
            with self.subTest(selected=selected):
                image = data_update.patch_image(self.original, SIZE, self.pkg, selected)
                fs, _ = data_update.mount_existing(image, SIZE)
                try:
                    for path, value in self.old.items():
                        name = path.rsplit("/", 1)[-1]
                        expected = self.new[name] if (name in self.new and
                                     ((name.startswith("map") and "map" in selected) or
                                      (name == "pois.csv" and "points" in selected))) else value
                        with fs.open(path, "rb") as f:
                            self.assertEqual(f.read(), expected, path)
                    if "map" in selected:
                        with fs.open("/hzmap/map_preview.rgb565", "rb") as f:
                            self.assertEqual(f.read(), self.new["map_preview.rgb565"])
                finally:
                    fs.unmount()
                self.assertEqual(len(image), SIZE)

    def test_map_geometry_rejected_before_write(self):
        import copy
        for bad in (720 * 480 * 2 + 2, 960 * 640 * 2, 480 * 320 * 2 - 12):
            m = copy.deepcopy(self.manifest)
            m["files"]["map.rgb565"]["size"] = bad
            with self.subTest(size=bad), self.assertRaisesRegex(ValueError, "размер"):
                data_update.validate_manifest(m, self.pkg)

    def test_invalid_image_does_not_format(self):
        broken = b"\xff" * SIZE
        with self.assertRaises(Exception):
            data_update.patch_image(broken, SIZE, self.pkg, {"map"})
        self.assertEqual(broken, b"\xff" * SIZE)

    def test_validation_and_zip(self):
        import zipfile
        data_update.validate_manifest(self.manifest, self.pkg)
        zpath = self.root / "sample.zip"
        with zipfile.ZipFile(zpath, "w") as z:
            for path in self.pkg.iterdir():
                z.write(path, path.name)
        with zpkg.ZipPackage(str(zpath)) as pack:
            self.assertTrue(pack.ok, pack.error)
            self.assertEqual(pack.manifest["kind"], "map-data")
        orig = (self.pkg / "pois.csv").read_bytes()
        (self.pkg / "pois.csv").write_bytes(b"tampered")
        with self.assertRaises(ValueError):
            data_update.validate_manifest(self.manifest, self.pkg)
        (self.pkg / "pois.csv").write_bytes(orig)

    def test_builder_uses_real_map_and_partition_size(self):
        repo = HERE.parents[1]
        part = self.root / "build-partitions.bin"
        part.write_bytes(self.partition)
        out = self.root / "made.zip"
        command = [sys.executable, str(HERE / "make_data_package.py"),
                   "--partitions", str(part), "--game",
                   str(repo / "games" / "zabytye-istorii"), "--out", str(out)]
        res = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
        with zpkg.ZipPackage(str(out)) as pack:
            self.assertTrue(pack.ok, pack.error)
            self.assertEqual(pack.manifest["fs_size"], SIZE)
            self.assertEqual(pack.manifest["fs_label"], "spiffs")
            self.assertEqual(pack.manifest["files"]["map.rgb565"]["size"], 720 * 480 * 2)
            self.assertEqual((Path(pack.pkgdir) / "pois.csv").read_text(encoding="utf-8").splitlines()[0].split("|")[3], "386")

    def test_bad_image_backed_up_but_not_written(self):
        calls = []
        old_run = flasher.run_esptool

        def run(argv):
            calls.append(argv)
            self.assertNotIn("write-flash", argv)
            if "read-flash" in argv:
                parts = argv[argv.index("read-flash") + 1:]
                Path(parts[-1]).write_bytes(self.partition if parts[0] == "0x8000" else b"\xff" * SIZE)
            return 0, None

        flasher.run_esptool = run
        try:
            backdir = self.root / "failed-backups"
            with self.assertRaisesRegex(RuntimeError, "Резервная копия"):
                data_update.update_device(self.manifest, self.pkg, {"map"},
                                          "COM8", 921600, backup_dir=backdir)
            self.assertEqual(len(calls), 2)
            backups = list(backdir.glob("*.bin"))
            self.assertEqual(len(backups), 1)
            self.assertEqual(backups[0].read_bytes(), b"\xff" * SIZE)
        finally:
            flasher.run_esptool = old_run

    def test_mock_flash_backup_and_partition_guard(self):
        calls = []
        flash = bytearray(self.original)
        old_run = flasher.run_esptool

        def run(argv):
            cmd = "read-flash" if "read-flash" in argv else "write-flash"
            calls.append((cmd, argv))
            parts = argv[argv.index(cmd) + 1:]
            if cmd == "read-flash":
                off, size, path = int(parts[0], 0), int(parts[1], 0), parts[2]
                Path(path).write_bytes(self.partition if off == 0x8000 else flash[:size])
            else:
                off, path = int(parts[0], 0), parts[1]
                self.assertEqual(off, OFFSET)
                flash[:] = Path(path).read_bytes()
            return 0, None

        flasher.run_esptool = run
        try:
            back = data_update.update_device(self.manifest, self.pkg, {"points"},
                                              "COM8", 921600, backup_dir=self.root / "backup")
            self.assertEqual(Path(back).read_bytes(), self.original)
            self.assertEqual([c[0] for c in calls], ["read-flash", "read-flash", "write-flash", "read-flash"])
            self.assertIn("no-reset", calls[2][1])
            self.assertEqual(calls[2][1][calls[2][1].index("--before") + 1], "no-reset")
            self.assertEqual(calls[3][1][calls[3][1].index("--after") + 1], "hard-reset")
            self.assertNotEqual(bytes(flash), self.original)
            fs, _ = data_update.mount_existing(bytes(flash), SIZE)
            try:
                with fs.open("/chats/private.txt", "rb") as f:
                    self.assertEqual(f.read(), b"private chat")
                with fs.open("/hzmap/pois.csv", "rb") as f:
                    self.assertEqual(f.read(), self.new["pois.csv"])
            finally:
                fs.unmount()
            calls.clear()
            self.partition = self.partition[:4] + (OFFSET + 4096).to_bytes(4, "little") + self.partition[8:]
            with self.assertRaisesRegex(ValueError, "не совпадает"):
                data_update.update_device(self.manifest, self.pkg, {"map"},
                                          "COM8", 921600, backup_dir=self.root / "backup")
            self.assertEqual(len(calls), 1)  # none of the write commands
        finally:
            flasher.run_esptool = old_run


if __name__ == "__main__":
    unittest.main()
