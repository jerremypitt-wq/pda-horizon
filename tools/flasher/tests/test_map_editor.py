"""Редактор без дисплея: проект, геометрия, пакет и запись в эмуляцию ПДА."""
import io
import struct
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import data_update  # noqa: E402
import editor_data  # noqa: E402
import flasher  # noqa: E402
from littlefs import LittleFS, UserContext  # noqa: E402
from PIL import Image  # noqa: E402

SIZE = 0x360000
OFFSET = 0xC90000
PART = struct.pack("<HBBII16sI", 0x50AA, 1, 0x82, OFFSET, SIZE,
                   b"spiffs" + b"\0" * 10, 0) + b"\xff" * (4096 - 32)


def blank_jpg(size=(960, 640)):
    output = io.BytesIO()
    Image.new("RGB", size, (56, 92, 128)).save(output, format="JPEG")
    return output.getvalue()


def sample_point(x=370, y=110):
    return {"x": x, "y": y, "type": 1, "name": "Бар", "sender": "Мастер",
            "comment": "Тихое место"}


class MapEditorTests(unittest.TestCase):
    def test_project_save_load_and_validation(self):
        p = editor_data.MapProject(blank_jpg())
        p.add(sample_point())
        p.add(sample_point(959, 639))
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "test.hzmap"
            p.save(path)
            restored = editor_data.MapProject.load(path)
            self.assertEqual(restored.image, p.image)
            self.assertEqual(restored.points, p.points)
            restored.points[0]["name"] = "Другой"
            self.assertEqual(p.points[0]["name"], "Бар")
            restored.replace(0, sample_point(0, 0))
            self.assertEqual(restored.points[0]["x"], 0)
            legacy = editor_data.MapProject.from_jpg(ROOT / "games/zabytye-istorii/map.jpg")
            self.assertEqual(len(legacy.points), 3)
            self.assertEqual((legacy.points[0]["x"], legacy.points[0]["y"]), (370, 110))
        for wrong in ({**sample_point(), "x": 960}, {**sample_point(), "name": "X" * 21},
                      {**sample_point(), "comment": "a|b"}, {**sample_point(), "y": True},
                      {**sample_point(), "sender": "🚀" * 30}):
            with self.assertRaises(ValueError):
                p.add(wrong)
        with self.assertRaises(ValueError):
            editor_data.MapProject(blank_jpg((480, 320)))
        with self.assertRaises(ValueError):
            editor_data.MapProject(b"not a JPEG")
        with self.assertRaisesRegex(ValueError, "50"):
            p50 = editor_data.MapProject(blank_jpg(), [sample_point()] * 50)
            p50.add(sample_point())

    def test_rgb_package_csv_aligns_with_old_map(self):
        p = editor_data.MapProject(blank_jpg(), [sample_point(), sample_point(959, 639)])
        with tempfile.TemporaryDirectory() as tmp:
            m = editor_data.prepare_package(p, OFFSET, SIZE, "spiffs", tmp)
            self.assertEqual(m["kind"], "map-data")
            self.assertEqual(m["files"]["map.rgb565"]["size"], 960 * 640 * 2)
            self.assertEqual(m["files"]["map_preview.rgb565"]["size"], 480 * 320 * 2)
            rows = (Path(tmp) / "pois.csv").read_text(encoding="utf-8").splitlines()
            self.assertEqual(rows[0], "B001|1|Бар|386|172|Мастер|Тихое место")
            self.assertEqual(rows[1].split("|")[3:5], ["1000", "1000"])
            with self.assertRaisesRegex(ValueError, "границы"):
                editor_data.prepare_package(p, OFFSET, 1234, "spiffs", tmp)

    def test_one_click_upload_preserves_chats_and_my_points(self):
        ctx = UserContext(SIZE)
        fs = LittleFS(context=ctx, mount=False, block_size=4096,
                      block_count=SIZE // 4096, read_size=16, prog_size=16,
                      cache_size=512, lookahead_size=128)
        fs.format()
        fs.mount()
        fs.makedirs("/hzmap", exist_ok=True)
        for name, body in (("/hzmap/my.csv", b"my points"),
                           ("/hzmap/net.csv", b"received points"),
                           ("/hzmap/pois.csv", b"old baked"),
                           ("/hzmap/map.rgb565", b"old map"),
                           ("/chat.log", b"private chat")):
            with fs.open(name, "wb") as f:
                f.write(body)
        fs.unmount()
        original = bytes(ctx.buffer)
        flash = bytearray(original)
        calls = []
        old_run = flasher.run_esptool

        def mock_esptool(argv):
            cmd = "read-flash" if "read-flash" in argv else "write-flash"
            calls.append((cmd, argv))
            args = argv[argv.index(cmd) + 1:]
            if cmd == "read-flash":
                off, size, path = int(args[0], 0), int(args[1], 0), args[2]
                Path(path).write_bytes(PART if off == 0x8000 else flash[:size])
            else:
                self.assertEqual(int(args[0], 0), OFFSET)  # никогда не писать прошивку
                flash[:] = Path(args[1]).read_bytes()
            return 0, None

        flasher.run_esptool = mock_esptool
        try:
            with tempfile.TemporaryDirectory() as tmp:
                backup = editor_data.upload_project(
                    editor_data.MapProject(blank_jpg(), [sample_point()]),
                    "COM8", 921600, backup_dir=Path(tmp) / "backups")
                self.assertEqual(Path(backup).read_bytes(), original)
            self.assertEqual([name for name, _ in calls],
                             ["read-flash", "read-flash", "read-flash", "write-flash", "read-flash"])
            self.assertEqual(calls[1][1][calls[1][1].index("--before") + 1], "no-reset")
            checkfs, _ = data_update.mount_existing(bytes(flash), SIZE)
            try:
                for name, body in (("/hzmap/my.csv", b"my points"),
                                   ("/hzmap/net.csv", b"received points"),
                                   ("/chat.log", b"private chat")):
                    with checkfs.open(name, "rb") as f:
                        self.assertEqual(f.read(), body)
                with checkfs.open("/hzmap/pois.csv", "rb") as f:
                    self.assertTrue(f.read().startswith("B001|1|Бар|386|172|".encode()))
                with checkfs.open("/hzmap/map.rgb565", "rb") as f:
                    self.assertEqual(len(f.read()), 960 * 640 * 2)
            finally:
                checkfs.unmount()
        finally:
            flasher.run_esptool = old_run


if __name__ == "__main__":
    unittest.main()
