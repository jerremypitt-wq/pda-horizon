#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Отдельный редактор этажей: проект v2 и безопасная запись LittleFS.

Флешер/его редактор остаются без изменений. Вкладки имеют постоянные ID 0..255;
порядок — только порядок строк tabs.csv. Удаление вкладки удаляет её точки
my/net на устройстве; чаты, настройки, точки остальных вкладок не меняются.
"""
import hashlib
import io
import json
import math
import os
import shutil
import sys
import tempfile
import zipfile
from datetime import datetime
from pathlib import Path

from PIL import Image
from littlefs.errors import LittleFSError

import data_update
import editor_data
import flasher
from make_package import parse_partitions_bin

MAX_TABS = 4
MAX_TAB_ID = 255
MAX_POINTS = 50
MAX_ANCHORS = 20  # per tab; editor-only, never POI
RESOLUTIONS = (720, 600, 480)
RESERVE = 256 * 1024


def validate_name(name):
    if (not isinstance(name, str) or not name.strip() or len(name) > 24 or
            len(name.encode('utf-8')) > 64 or any(c in name for c in '|\r\n')):
        raise ValueError('Название вкладки: 1–24 символа, не более 64 байт, без | и переноса строки')
    return name.strip()


def validate_anchors(anchors, width, height):
    """Image pixels + WGS84 decimal degrees; no calibration inferred here."""
    if not isinstance(anchors, list) or len(anchors) > MAX_ANCHORS:
        raise ValueError('Привязок: не более 20 на вкладку')
    result, pixels, coordinates = [], set(), set()
    for anchor in anchors:
        if not isinstance(anchor, dict) or set(anchor) != {'name', 'x', 'y', 'lat', 'lon'}:
            raise ValueError('Неверный формат точки привязки')
        name = anchor['name']
        if (not isinstance(name, str) or not name.strip() or len(name) > 30 or
                any(c in name for c in '|\r\n')):
            raise ValueError('Имя привязки: от 1 до 30 символов, без | и переноса строки')
        x, y = anchor['x'], anchor['y']
        if type(x) is not int or type(y) is not int or not (0 <= x < width and 0 <= y < height):
            raise ValueError('Пиксели привязки должны находиться внутри JPG')
        lat, lon = anchor['lat'], anchor['lon']
        if (type(lat) not in (int, float) or type(lon) not in (int, float) or
                not math.isfinite(lat) or not math.isfinite(lon) or
                not -90 <= lat <= 90 or not -180 <= lon <= 180):
            raise ValueError('Широта: −90…90; долгота: −180…180 (десятичные градусы)')
        if (x, y) in pixels or (lat, lon) in coordinates:
            raise ValueError('Привязки не могут совпадать по пикселям или геокоординатам')
        pixels.add((x, y))
        coordinates.add((lat, lon))
        result.append({'name': name.strip(), 'x': x, 'y': y, 'lat': float(lat), 'lon': float(lon)})
    return result


def names(tab):
    i = tab if tab else ''
    return f'map{i}.rgb565', f'map{i}_preview.rgb565'


class MultiMapProject:
    def __init__(self, tabs, next_id=None, anchors=None):
        # (id, title, MapProject), order is user-controlled and IDs never change on reorder
        if not isinstance(tabs, list) or not 1 <= len(tabs) <= MAX_TABS:
            raise ValueError('Нужно от 1 до 4 вкладок')
        ids = set()
        self.tabs = []
        for id_, title, page in tabs:
            if type(id_) is not int or not 0 <= id_ <= MAX_TAB_ID or id_ in ids:
                raise ValueError('ID вкладки должен быть уникальным числом от 0 до 255')
            if not isinstance(page, editor_data.MapProject):
                raise ValueError('Вкладка без карты')
            ids.add(id_)
            self.tabs.append((id_, validate_name(title), page.snapshot()))
        if sum(len(page.points) for _, _, page in self.tabs) > MAX_POINTS:
            raise ValueError('Всего не более 50 стартовых точек')
        if anchors is None:
            anchors = {}
        if not isinstance(anchors, dict) or set(anchors) - ids:
            raise ValueError('Привязки ссылаются на неизвестную вкладку')
        self.anchors = {id_: validate_anchors(anchors.get(id_, []), page.width, page.height)
                        for id_, _, page in self.tabs}
        self.next_id = next_id if next_id is not None else max(ids) + 1
        if type(self.next_id) is not int or self.next_id <= max(ids) or self.next_id > MAX_TAB_ID + 1:
            raise ValueError('Неверный следующий ID вкладки')

    def snapshot(self):
        return MultiMapProject(self.tabs, self.next_id, self.anchors)

    def save(self, path):
        project = self.snapshot()  # validate public mutable lists again before writing
        target = Path(path)
        target.parent.mkdir(parents=True, exist_ok=True)
        fd, temp = tempfile.mkstemp(prefix='.hzmap-', suffix='.tmp', dir=target.parent)
        os.close(fd)
        try:
            with zipfile.ZipFile(temp, 'w', compression=zipfile.ZIP_DEFLATED) as zf:
                manifest = {'format': 2, 'next_id': project.next_id, 'tabs': []}
                for id_, title, page in project.tabs:
                    zf.writestr(f'map{id_}.jpg', page.image)
                    tab = {'id': id_, 'name': title, 'points': page.points}
                    if project.anchors[id_]:
                        tab['anchors'] = project.anchors[id_]
                    manifest['tabs'].append(tab)
                zf.writestr('project.json', json.dumps(manifest, ensure_ascii=False))
            os.replace(temp, target)
        finally:
            if os.path.exists(temp):
                os.unlink(temp)

    @classmethod
    def load(cls, path):
        try:
            with zipfile.ZipFile(path) as zf:
                entries = zf.namelist()
                if len(entries) != len(set(entries)) or len(entries) > MAX_TABS + 1:
                    raise ValueError('Повторяющиеся или лишние файлы в проекте')
                if set(entries) == {'map.jpg', 'points.json'}:  # старый проект флешера
                    return cls([(0, 'Карта', editor_data.MapProject.load(path))])
                if 'project.json' not in entries or zf.getinfo('project.json').file_size > 1024 * 1024:
                    raise ValueError('Нет проекта или он слишком большой')
                raw = json.loads(zf.read('project.json').decode('utf-8'))
                if not isinstance(raw, dict) or set(raw) not in ({'format', 'tabs'}, {'format', 'tabs', 'next_id'}) or raw['format'] != 2 or not isinstance(raw['tabs'], list):
                    raise ValueError('Неизвестная версия проекта')
                expected = {'project.json'}
                tabs = []
                anchors = {}
                for tab in raw['tabs']:
                    if not isinstance(tab, dict) or set(tab) not in ({'id', 'name', 'points'}, {'id', 'name', 'points', 'anchors'}) or type(tab['id']) is not int or not 0 <= tab['id'] <= MAX_TAB_ID:
                        raise ValueError('Неверная вкладка')
                    name = f"map{tab['id']}.jpg"
                    if name not in entries or zf.getinfo(name).file_size > editor_data.MAX_IMAGE_BYTES:
                        raise ValueError('Карта отсутствует или слишком большая')
                    expected.add(name)
                    tabs.append((tab['id'], tab['name'], editor_data.MapProject(zf.read(name), tab['points'])))
                    anchors[tab['id']] = tab.get('anchors', [])
                if set(entries) != expected:
                    raise ValueError('Лишние файлы в проекте')
                return cls(tabs, raw.get('next_id'), anchors)
        except (OSError, zipfile.BadZipFile, UnicodeError, json.JSONDecodeError) as exc:
            raise ValueError(f'Проект повреждён: {exc}') from exc


def prepare_files(project, resolution):
    """Конвертировать все вкладки в память; 480×320 не нуждается в отдельном обзоре."""
    project = project.snapshot()
    if resolution not in RESOLUTIONS:
        raise ValueError('Неверное разрешение')
    files = {}
    rows = []
    files['tabs.csv'] = ''.join(f'{id_}|{title}\n' for id_, title, _ in project.tabs).encode()
    for id_, _, page in project.tabs:
        with Image.open(io.BytesIO(page.image)) as src:
            im = src.convert('RGB')
        width = min(page.width, resolution)
        height = width * 2 // 3
        if im.size != (width, height):
            im = im.resize((width, height), Image.Resampling.LANCZOS)
        full, preview = names(id_)
        files[full] = rgb565(im)
        if width > 480:
            files[preview] = rgb565(im.resize((480, 320), Image.Resampling.LANCZOS))
        for index, point in enumerate(page.points, 1):
            # B001..B050 остаются у старой карты; F<ID>-NN переживают перестановку.
            pid = f'B{index:03d}' if id_ == 0 else f'F{id_}-{index:02d}'
            rows.append(f"{pid}|{point['type']}|{point['name']}|"
                        f"{editor_data.poi_pack.to_promille(point['x'], page.width - 1)}|"
                        f"{editor_data.poi_pack.to_promille(point['y'], page.height - 1)}|"
                        f"{point['sender']}|{point['comment']}|{id_}\n")
    files['pois.csv'] = ''.join(rows).encode('utf-8')
    return files


def rgb565(im):
    """RGB565 LE; те же каналы и порядок байтов, что в poi_pack."""
    import struct
    rgb = im.tobytes()
    result = bytearray(len(rgb) // 3 * 2)
    for i in range(0, len(rgb), 3):
        r, g, b = rgb[i:i + 3]
        struct.pack_into('<H', result, i // 3 * 2, ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))
    return bytes(result)


def tab_ids(raw):
    if raw is None:
        return {0}  # прошивка до вкладок
    ids = set()
    for row in raw.decode('utf-8').splitlines():
        parts = row.split('|')
        if (len(parts) != 2 or not parts[0].isdigit() or str(int(parts[0])) != parts[0] or
                not 0 <= int(parts[0]) <= MAX_TAB_ID or not parts[1] or int(parts[0]) in ids):
            raise ValueError('На ПДА повреждён tabs.csv; запись остановлена')
        ids.add(int(parts[0]))
    if not ids or len(ids) > MAX_TABS:
        raise ValueError('На ПДА нет действительных вкладок')
    return ids


def patch_image(original, size, files):
    """Обновить только карты/исходные точки. При удалении вкладки вычистить её my/net."""
    if len(original) != size:
        raise ValueError('ФС прочитана не полностью')
    fs, ctx = data_update.mount_existing(original, size)  # mount=False, никогда не форматировать
    removed = set()
    touched = {'/hzmap/' + name for name in files}
    touched.update('/hzmap/' + name for id_ in range(MAX_TABS) for name in names(id_))
    touched.update(('/hzmap/my.csv', '/hzmap/net.csv'))
    preserved = _preserved(original, size, touched)
    try:
        fs.makedirs('/hzmap', exist_ok=True)
        def read_optional(path):
            try:
                with fs.open(path, 'rb') as f:
                    return f.read()
            except FileNotFoundError:
                return None
        old_ids = tab_ids(read_optional('/hzmap/tabs.csv'))
        new_ids = tab_ids(files['tabs.csv'])
        removed = old_ids - new_ids
        # Полученные/свои точки УДАЛЁННЫХ вкладок не переживают обновление.
        for filename, tab_index in (('my.csv', 11), ('net.csv', 8)):
            path = '/hzmap/' + filename
            body = read_optional(path)
            if body is None or not removed:
                continue
            kept = []
            for line in body.decode('utf-8').splitlines(keepends=True):
                fields = line.rstrip('\r\n').split('|')
                try:
                    tab = int(fields[tab_index]) if len(fields) > tab_index else 0
                except ValueError as exc:
                    raise ValueError(f'Повреждён {filename}; запись отменена') from exc
                if tab not in removed:
                    kept.append(line)
            if len(kept) != len(body.decode('utf-8').splitlines()):
                touched.add(path)
                with fs.open(path, 'wb') as f:
                    f.write(''.join(kept).encode())
        # Удалить старые карты отсутствующих вкладок и неиспользуемые обзоры.
        candidates = {name for id_ in old_ids | new_ids for name in names(id_)}
        for name in candidates - set(files):
            path = '/hzmap/' + name
            if read_optional(path) is not None:
                fs.remove(path)
                touched.add(path)
        for name, data in files.items():
            path = '/hzmap/' + name
            touched.add(path)
            # Удалить старый файл перед записью, чтобы большие карты не занимали
            # два места одновременно. Оригинал остаётся в backup до записи.
            if read_optional(path) is not None:
                fs.remove(path)
            with fs.open(path, 'wb') as f:
                f.write(data)
        # Не съедать всё место для чатов и личных точек.
        if size - fs.used_block_count * data_update.BLOCK < RESERVE:
            raise ValueError('Мало свободного места для пользовательских данных')
    finally:
        fs.unmount()
    result = bytes(ctx.buffer)
    checkfs, _ = data_update.mount_existing(result, size)
    try:
        if data_update.snapshot_unselected(checkfs, touched) != preserved:
            raise ValueError('Посторонние данные изменились')
        for name, body in files.items():
            with checkfs.open('/hzmap/' + name, 'rb') as f:
                if hashlib.sha256(f.read()).digest() != hashlib.sha256(body).digest():
                    raise ValueError(f'Не совпадает контрольная сумма {name}')
    finally:
        checkfs.unmount()
    return result, removed


def _preserved(original, size, touched):
    fs, _ = data_update.mount_existing(original, size)
    try:
        return data_update.snapshot_unselected(fs, touched)
    finally:
        fs.unmount()


def upload_project(project, port, baud, progress=None, backup_dir=None):
    """Запись из EXE без консоли: esptool всегда получает живые stdout/stderr.

    PyInstaller --windowed выставляет оба потока в None. Общий flasher.py
    менять не надо: на время записи только этого редактора направляем вывод
    в временный текстовый файл (у него есть write/flush/fileno/encoding),
    а при ошибке показываем хвост лога. Восстанавливаем потоки даже при сбое.
    """
    old_out, old_err = sys.stdout, sys.stderr
    with tempfile.TemporaryFile(mode='w+t', encoding='utf-8', errors='replace') as log:
        if old_out is None:
            sys.stdout = log
        if old_err is None:
            sys.stderr = log
        try:
            return _upload_project(project, port, baud, progress, backup_dir)
        except Exception as exc:
            if old_out is None or old_err is None:
                log.flush()
                log.seek(0)
                from collections import deque
                tail = ''.join(deque(log, maxlen=12))[-1500:].strip()
                if tail:
                    raise RuntimeError(f'{exc}\nПоследний вывод esptool:\n{tail}') from exc
            raise
        finally:
            sys.stdout, sys.stderr = old_out, old_err


def _upload_project(project, port, baud, progress, backup_dir):
    """Раздел узнать с ПДА; backup ДО правки; перебор качества; запись ФС+readback."""
    project = project.snapshot()
    root = Path(backup_dir) if backup_dir else Path.home() / 'Documents' / 'HorizonBackups'
    root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryFile(dir=root):
        pass
    base = flasher.global_argv({'chip': 'esp32s3'}, port, baud)
    keep = base[:-2] + ['--after', 'no-reset']
    stay = ['no-reset' if a == 'default-reset' else a for a in keep]
    finish = ['no-reset' if a == 'default-reset' else a for a in base]
    with tempfile.TemporaryDirectory(prefix='hz-studio-') as tmp:
        part, original, prepared, verified = [Path(tmp) / name for name in ('partitions.bin', 'original.bin', 'prepared.bin', 'verified.bin')]
        if progress: progress('Чтение таблицы разделов...')
        data_update._esptool(keep + ['read-flash', '0x8000', '0x1000', str(part)], 'Чтение таблицы')
        off, size, label = parse_partitions_bin(part)
        if label.lower() not in ('spiffs', 'littlefs') or size < 4 * data_update.BLOCK:
            raise ValueError('Неверный раздел данных')
        if progress: progress('Чтение пользовательских данных...')
        data_update._esptool(stay + ['read-flash', hex(off), hex(size), str(original)], 'Чтение ФС')
        if original.stat().st_size != size:
            raise ValueError('Данные прочитаны не полностью')
        # Если монтирование не удалось — НЕ создавать backup с ложным обещанием сохранности.
        fs, _ = data_update.mount_existing(original.read_bytes(), size)
        fs.unmount()
        backup = root / ('hz-littlefs-' + datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '.bin')
        shutil.copyfile(original, backup)
        print(f'Резервная копия: {backup}')
        try:
            last_error = None
            for resolution in RESOLUTIONS:
                if progress: progress(f'Подбор размера карт: до {resolution}×{resolution * 2 // 3}...')
                files = prepare_files(project, resolution)
                try:
                    image, removed = patch_image(original.read_bytes(), size, files)
                    prepared.write_bytes(image)
                    break
                except (OSError, ValueError, LittleFSError) as exc:
                    if 'Мало свободного места' not in str(exc) and 'No space' not in str(exc) and 'NOSPC' not in str(exc):
                        raise
                    last_error = exc
            else:
                raise ValueError(f'Карты не помещаются с резервом {RESERVE // 1024} КиБ: {last_error}')
            if progress: progress('Запись только раздела данных...')
            data_update._esptool(stay + ['write-flash', hex(off), str(prepared)], 'Запись ФС')
            if progress: progress('Повторное чтение и проверка...')
            data_update._esptool(finish + ['read-flash', hex(off), hex(size), str(verified)], 'Проверка ФС')
            if verified.read_bytes() != prepared.read_bytes():
                raise ValueError('Образ после записи отличается от подготовленного')
        except Exception as exc:
            raise RuntimeError(f'{exc}. Резервная копия: {backup}. Не отключай ПДА до выяснения причины.') from exc
        return str(backup), resolution, removed
