"""Вкладки: совместимость v1, стабильные ID, удаление и сохранность ФС."""
import io
import json
import zipfile
import struct
import sys
from unittest import mock
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
from littlefs import LittleFS, UserContext
from littlefs.errors import LittleFSError  # noqa: E402
from PIL import Image  # noqa: E402
import editor_data  # noqa: E402
import multi_map  # noqa: E402
import data_update  # noqa: E402
import flasher  # noqa: E402

SIZE = 0x360000


def image(color=(30, 80, 120)):
    buf = io.BytesIO()
    Image.new('RGB', (480, 320), color).save(buf, format='JPEG')
    return buf.getvalue()


def point(name):
    return {'x': 123, 'y': 56, 'name': name, 'type': 1,
            'sender': 'Мастер', 'comment': 'Этаж'}


class MultiMapTests(unittest.TestCase):
    def test_project_v1_and_reorder(self):
        page = editor_data.MapProject(image(), [point('Старая')])
        with tempfile.TemporaryDirectory() as tmp:
            old = Path(tmp) / 'old.hzmap'
            page.save(old)
            project = multi_map.MultiMapProject.load(old)
            self.assertEqual(project.tabs[0][0], 0)
            project = multi_map.MultiMapProject([
                (2, '2 этаж', editor_data.MapProject(image((20, 40, 60)), [point('Второй')])),
                (0, '1 этаж', page)])
            path = Path(tmp) / 'new.hzmap'
            project.save(path)
            loaded = multi_map.MultiMapProject.load(path)
            self.assertEqual([t[:2] for t in loaded.tabs], [(2, '2 этаж'), (0, '1 этаж')])
            files = multi_map.prepare_files(loaded, 480)
            self.assertEqual(files['tabs.csv'].decode(), '2|2 этаж\n0|1 этаж\n')
            self.assertNotIn('map_preview.rgb565', files)
            self.assertIn(b'F2-01|1|', files['pois.csv'])
            self.assertIn(b'B001|1|', files['pois.csv'])
            # Удалённый этаж 2 не получает прежний ID после новой вкладки.
            retired = multi_map.MultiMapProject([(0, 'Улица', page)], next_id=3)
            retired_path = Path(tmp) / 'retired.hzmap'
            retired.save(retired_path)
            self.assertEqual(multi_map.MultiMapProject.load(retired_path).next_id, 3)
            new_floor = multi_map.MultiMapProject([(0, 'Улица', page),
                (3, 'Новый этаж', editor_data.MapProject(image()))], next_id=4)
            self.assertIn('map3.rgb565', multi_map.prepare_files(new_floor, 480))
            with self.assertRaisesRegex(ValueError, '50'):
                multi_map.MultiMapProject([(0, 'A', editor_data.MapProject(image(), [point('x')] * 30)),
                                           (1, 'B', editor_data.MapProject(image(), [point('x')] * 21))])
            with self.assertRaises(ValueError):
                multi_map.MultiMapProject.load(Path(tmp) / 'absent.hzmap')

    def test_geo_anchors_roundtrip_and_never_sent_to_device(self):
        page = editor_data.MapProject(image(), [point('Game POI')])
        anchors = {0: [{'name': 'Ратуша', 'x': 100, 'y': 40,
                        'lat': 50.775345, 'lon': 6.083887},
                       {'name': 'Юго-запад', 'x': 350, 'y': 220,
                        'lat': -12.5, 'lon': -70.25}]}
        project = multi_map.MultiMapProject([(0, 'Улица', page)], anchors=anchors)
        reference_files = multi_map.prepare_files(multi_map.MultiMapProject([(0, 'Улица', page)]), 480)
        self.assertEqual(multi_map.prepare_files(project, 480), reference_files)
        self.assertNotIn('Ратуша'.encode(), b''.join(reference_files.values()))
        anchors[0][0]['lat'] = 0  # constructor copies deeply
        self.assertEqual(project.anchors[0][0]['lat'], 50.775345)
        clone = project.snapshot()
        clone.anchors[0][0]['name'] = 'Копия'
        self.assertEqual(project.anchors[0][0]['name'], 'Ратуша')
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'geo.hzmap'
            project.save(path)
            loaded = multi_map.MultiMapProject.load(path)
            self.assertEqual(loaded.anchors, project.anchors)
            self.assertEqual(loaded.tabs[0][2].points, page.points)
            with zipfile.ZipFile(path) as zf:
                manifest = json.loads(zf.read('project.json'))
                self.assertEqual(manifest['format'], 2)
                self.assertEqual(manifest['tabs'][0]['anchors'], project.anchors[0])
            self.assertEqual(multi_map.prepare_files(loaded, 480), reference_files)
            project.anchors[0][0]['lat'] = float('nan')
            with self.assertRaises(ValueError):
                project.save(path)
            self.assertEqual(multi_map.MultiMapProject.load(path).anchors, loaded.anchors)
            # Previous v2 projects (without anchors) and v1 remain readable.
            legacy_v2 = multi_map.MultiMapProject([(0, 'Улица', page)])
            legacy_v2.save(path)
            with zipfile.ZipFile(path) as zf:
                self.assertNotIn('anchors', json.loads(zf.read('project.json'))['tabs'][0])
            self.assertEqual(multi_map.MultiMapProject.load(path).anchors, {0: []})
            page.save(path)
            self.assertEqual(multi_map.MultiMapProject.load(path).anchors, {0: []})

    def test_geo_anchors_validate_and_per_tab_lifecycle(self):
        page = editor_data.MapProject(image())
        base = {'name': 'A', 'x': 0, 'y': 319, 'lat': 90.0, 'lon': -180.0}
        for change in ({'x': True}, {'x': 480}, {'y': -1}, {'lat': 91},
                       {'lon': -181}, {'lat': float('nan')}, {'lon': float('inf')},
                       {'name': ''}, {'name': 'A|B'}, {'lat': '50.1'}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                multi_map.MultiMapProject([(0, 'A', page)], anchors={0: [dict(base, **change)]})
        with self.assertRaisesRegex(ValueError, 'совпадать'):
            multi_map.MultiMapProject([(0, 'A', page)], anchors={0: [base, base]})
        with self.assertRaises(ValueError):
            multi_map.MultiMapProject([(0, 'A', page)], anchors={5: []})
        project = multi_map.MultiMapProject([(0, 'A', page), (3, 'B', page)], next_id=4,
                                             anchors={3: [base]})
        reordered = multi_map.MultiMapProject(list(reversed(project.tabs)), project.next_id,
                                                project.anchors)
        self.assertEqual(reordered.anchors[3], [base])
        deleted = multi_map.MultiMapProject([project.tabs[0]], project.next_id,
                                              {0: project.anchors[0]})
        self.assertEqual(deleted.anchors, {0: []})
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'invalid.hzmap'
            project.save(path)
            with zipfile.ZipFile(path) as zf:
                files = {name: zf.read(name) for name in zf.namelist()}
            manifest = json.loads(files['project.json'])
            manifest['tabs'][1]['anchors'][0]['lat'] = 'bad'
            files['project.json'] = json.dumps(manifest).encode()
            with zipfile.ZipFile(path, 'w') as zf:
                for name, value in files.items():
                    zf.writestr(name, value)
            with self.assertRaisesRegex(ValueError, 'Широта'):
                multi_map.MultiMapProject.load(path)

    def test_fs_delete_tab_and_preserve_other_data(self):
        ctx = UserContext(SIZE)
        fs = LittleFS(context=ctx, mount=False, block_size=4096,
                      block_count=SIZE // 4096, read_size=16, prog_size=16,
                      cache_size=512, lookahead_size=128)
        fs.format()
        fs.mount()
        fs.makedirs('/hzmap', exist_ok=True)
        old = {'/hzmap/tabs.csv': '0|Улица\n1|1 этаж\n2|2 этаж\n'.encode(),
               '/hzmap/my.csv': b'123-1|1|Keep|100|200|Me||1|0|0||0\n123-2|1|Gone|100|200|Me||1|0|0||1\n',
               '/hzmap/net.csv': b'456-1|1|Gone|100|200|Peer||0|1\n',
               '/hzmap/map1.rgb565': b'oldfloor',
               '/hzmap/map2.rgb565': b'oldfloor',
               '/chat.log': b'private chat', '/horizon.cfg': b'pin=1234'}
        for path, data in old.items():
            with fs.open(path, 'wb') as f:
                f.write(data)
        fs.unmount()
        original = bytes(ctx.buffer)
        project = multi_map.MultiMapProject([(0, 'Улица', editor_data.MapProject(image(), [point('A')])),
                                              (2, '2 этаж', editor_data.MapProject(image(), [point('B')]))])
        result, removed = multi_map.patch_image(original, SIZE, multi_map.prepare_files(project, 480))
        self.assertEqual(removed, {1})
        check, _ = data_update.mount_existing(result, SIZE)
        try:
            for path in ('/chat.log', '/horizon.cfg'):
                with check.open(path, 'rb') as f:
                    self.assertEqual(f.read(), old[path])
            with check.open('/hzmap/my.csv', 'rb') as f:
                body = f.read()
                self.assertIn(b'Keep', body)
                self.assertNotIn(b'Gone', body)
            with check.open('/hzmap/net.csv', 'rb') as f:
                self.assertEqual(f.read(), b'')
            with self.assertRaises(LittleFSError):
                check.stat('/hzmap/map1.rgb565')
            with check.open('/hzmap/pois.csv', 'rb') as f:
                self.assertEqual(len(f.read().splitlines()), 2)
        finally:
            check.unmount()

    def test_auto_resolution_upload_backup_and_readback(self):
        ctx = UserContext(SIZE)
        fs = LittleFS(context=ctx, mount=False, block_size=4096,
                      block_count=SIZE // 4096, read_size=16, prog_size=16,
                      cache_size=512, lookahead_size=128)
        fs.format()
        fs.mount()
        with fs.open('/chat.log', 'wb') as f:
            f.write(b'keep chat')
        fs.unmount()
        original = bytes(ctx.buffer)
        flash = bytearray(original)
        calls = []
        partition = struct.pack('<HBBII16sI', 0x50AA, 1, 0x82, 0xC90000, SIZE,
                                b'spiffs' + b'\0' * 10, 0) + b'\xff' * (4096 - 32)
        def mock_esptool(argv):
            cmd = 'read-flash' if 'read-flash' in argv else 'write-flash'
            calls.append(cmd)
            args = argv[argv.index(cmd) + 1:]
            if cmd == 'read-flash':
                offset, size = int(args[0], 0), int(args[1], 0)
                Path(args[2]).write_bytes(partition if offset == 0x8000 else flash[:size])
            else:
                self.assertEqual(int(args[0], 0), 0xC90000)
                flash[:] = Path(args[1]).read_bytes()
            return 0, None
        big = io.BytesIO()
        Image.new('RGB', (960, 640), (60, 110, 170)).save(big, format='JPEG')
        project = multi_map.MultiMapProject([(i, f'Этаж {i}', editor_data.MapProject(big.getvalue()))
                                             for i in range(4)])
        old_run = flasher.run_esptool
        flasher.run_esptool = mock_esptool
        try:
            with tempfile.TemporaryDirectory() as tmp:
                backup, resolution, removed = multi_map.upload_project(
                    project, 'COM8', 921600, backup_dir=Path(tmp) / 'backup')
                self.assertEqual(Path(backup).read_bytes(), original)
                self.assertIn(resolution, (480, 600))  # 720 x 4 не помещаются
                self.assertEqual(removed, set())
                self.assertEqual(calls, ['read-flash', 'read-flash', 'write-flash', 'read-flash'])
                check, _ = data_update.mount_existing(bytes(flash), SIZE)
                try:
                    with check.open('/chat.log', 'rb') as f:
                        self.assertEqual(f.read(), b'keep chat')
                    for i in range(4):
                        with check.open('/hzmap/' + multi_map.names(i)[0], 'rb') as f:
                            self.assertEqual(len(f.read()), resolution * resolution * 4 // 3)
                finally:
                    check.unmount()
        finally:
            flasher.run_esptool = old_run

    def test_windowed_exe_without_stdout_or_stderr(self):
        """PyInstaller --windowed даёт None; esptool всё равно должен писать лог."""
        import esptool

        def fake_esptool(argv):
            print('Reading partition table...')
            sys.stderr.write('esptool diagnostic\n')

        def run_read(*_args):
            return flasher.run_esptool(['read-flash'])

        original_out, original_err = sys.stdout, sys.stderr
        with mock.patch.object(esptool, 'main', fake_esptool), \
             mock.patch.object(multi_map, '_upload_project', run_read):
            with mock.patch.object(sys, 'stdout', None), mock.patch.object(sys, 'stderr', None):
                self.assertEqual(multi_map.upload_project(None, 'COM8', 921600), (0, None))
                self.assertIsNone(sys.stdout)
                self.assertIsNone(sys.stderr)
        self.assertIs(sys.stdout, original_out)
        self.assertIs(sys.stderr, original_err)

        def fail_after_log(*_args):
            print('readback diagnostic')
            raise ValueError('Тестовая ошибка')

        with mock.patch.object(multi_map, '_upload_project', fail_after_log):
            with mock.patch.object(sys, 'stdout', None), mock.patch.object(sys, 'stderr', None):
                with self.assertRaisesRegex(RuntimeError, 'readback diagnostic') as ctx:
                    multi_map.upload_project(None, 'COM8', 921600)
                self.assertIn('Тестовая ошибка', str(ctx.exception))
                self.assertIsNone(sys.stdout)
                self.assertIsNone(sys.stderr)

    def test_legacy_project_and_corrupt_metadata(self):
        self.assertEqual(multi_map.tab_ids(None), {0})
        with self.assertRaises(ValueError):
            multi_map.tab_ids(b'0|A\n0|B\n')
        with self.assertRaises(ValueError):
            multi_map.validate_name('X|Y')


if __name__ == '__main__':
    unittest.main()
