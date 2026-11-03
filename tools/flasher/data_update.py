#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Безопасное обновление отдельных файлов LittleFS без записи прошивки.

Никогда не форматируем ФС. При любой ошибке монтирования или таблицы разделов
останавливаемся ДО записи. Образ с ПДА сохраняется вне временной папки.
"""
import hashlib
import os
import shutil
import tempfile
from datetime import datetime
from pathlib import Path

import flasher
from make_package import parse_partitions_bin

BLOCK = 4096  # ESP32 LittleFS erase block / mklittlefs default
FILES = {"map": ("map.rgb565", "map_preview.rgb565"), "points": ("pois.csv",)}


def validate_manifest(m, pkgdir):
    if m.get("kind") != "map-data" or m.get("chip") != "esp32s3":
        raise ValueError("не пакет карты/точек для ESP32-S3")
    off, size = m.get("fs_offset"), m.get("fs_size")
    if not isinstance(off, int) or not isinstance(size, int) or off < 0x10000 or size < BLOCK * 4 or off % BLOCK or size % BLOCK or off + size > 0x1000000:
        raise ValueError("неверные границы файловой системы")
    label = m.get("fs_label", "spiffs")
    if label not in ("spiffs", "littlefs"):
        raise ValueError("неверная метка раздела файловой системы")
    files = m.get("files")
    if not isinstance(files, dict) or set(files) != set(FILES["map"] + FILES["points"]):
        raise ValueError("пакет должен содержать карту, обзор и точки")
    for fn, spec in files.items():
        if not isinstance(spec, dict) or not isinstance(spec.get("sha256"), str) or len(spec["sha256"]) != 64:
            raise ValueError(f"неверная контрольная сумма {fn}")
        path = Path(pkgdir) / fn
        if not path.is_file() or path.stat().st_size != spec.get("size"):
            raise ValueError(f"нет файла {fn} или неверный размер")
        if hashlib.sha256(path.read_bytes()).hexdigest() != spec["sha256"]:
            raise ValueError(f"не совпала контрольная сумма {fn}")
    raw_size = files["map.rgb565"]["size"]
    # Размер RGB565 однозначно задаёт геометрию при строго 3:2: 12*k*k.
    # Новая заливка ограничена 720×480; прежние 960×640 уже на ПДА читаются,
    # но новые пакеты такого размера создавать/записывать не разрешаем.
    if (raw_size not in (12 * k * k for k in range(160, 241)) or
            files["map_preview.rgb565"]["size"] != 480 * 320 * 2):
        raise ValueError("неверный размер карты (3:2, 480×320–720×480) или обзора")
    # Точки всегда проверяются даже при загрузке одной карты: нельзя выдавать
    # пакет с повреждённой частью за проверенный.
    text = (Path(pkgdir) / "pois.csv").read_text(encoding="utf-8")
    lines = text.splitlines()
    if len(lines) > 50 or any(len(line.split("|")) != 7 or not line.startswith(f"B{i:03d}|") for i, line in enumerate(lines, 1)):
        raise ValueError("неверный формат стартовых точек")
    return m


def mount_existing(image, size):
    # mount=False КРИТИЧНО: LittleFS(context=...) по умолчанию форматирует
    # раздел, если mount() не удался. Для считанного устройства это недопустимо.
    from littlefs import LittleFS, UserContext
    ctx = UserContext(size)
    ctx.buffer[:] = image
    fs = LittleFS(context=ctx, mount=False, block_size=BLOCK, block_count=size // BLOCK,
                  read_size=16, prog_size=16, cache_size=512, lookahead_size=128)
    fs.mount()  # исключение: не шить, оставить резервную копию
    return fs, ctx


def snapshot_unselected(fs, replaced):
    """Хеши всех остальных файлов, включая чаты/неизвестные данные."""
    saved = {}
    for directory, _dirs, files in fs.walk("/"):
        for name in files:
            path = directory.rstrip("/") + "/" + name
            if path in replaced:
                continue
            with fs.open(path, "rb") as f:
                saved[path] = hashlib.sha256(f.read()).digest()
    return saved


def patch_image(image, size, pkgdir, selected):
    selected = set(selected)
    if not selected or not selected <= set(FILES):
        raise ValueError("выбери карту и/или точки")
    if len(image) != size:
        raise ValueError("размер прочитанного образа неверен")
    fs, ctx = mount_existing(image, size)
    replaced = {"/hzmap/" + name for group in selected for name in FILES[group]}
    try:
        preserved = snapshot_unselected(fs, replaced)
        # Работаем только с этими файлами. /hzmap/my.csv, net.csv, del.csv,
        # чаты, настройки, любые неизвестные файлы остаются внутри образа.
        fs.makedirs("/hzmap", exist_ok=True)
        expected = {}
        for group in ("map", "points"):
            if group not in selected:
                continue
            for name in FILES[group]:
                data = (Path(pkgdir) / name).read_bytes()
                with fs.open("/hzmap/" + name, "wb") as f:
                    f.write(data)
                expected[name] = hashlib.sha256(data).hexdigest()
        for name, digest in expected.items():
            with fs.open("/hzmap/" + name, "rb") as f:
                if hashlib.sha256(f.read()).hexdigest() != digest:
                    raise ValueError(f"проверка после замены не прошла: {name}")
    finally:
        fs.unmount()
    result = bytes(ctx.buffer)
    # Проверить повторным монтированием, не доверяя только открытому объекту.
    checkfs, _ = mount_existing(result, size)
    try:
        if snapshot_unselected(checkfs, replaced) != preserved:
            raise ValueError("пользовательские файлы изменились; запись запрещена")
        for name, digest in expected.items():
            with checkfs.open("/hzmap/" + name, "rb") as f:
                if hashlib.sha256(f.read()).hexdigest() != digest:
                    raise ValueError(f"проверка нового образа не прошла: {name}")
    finally:
        checkfs.unmount()
    return result


def _esptool(argv, stage):
    code, err = flasher.run_esptool(argv)
    if err or code != 0:
        raise RuntimeError(f"{stage}: {err or 'esptool код ' + str(code)}")


def update_device(m, pkgdir, selected, port, baud, progress=None, backup_dir=None,
                  initial_no_reset=False):
    """Читать -> копия -> изменить -> записать ТОЛЬКО ФС -> читать -> сравнить.

    Сбой после записи нельзя скрыть: резервная копия сохраняется, в ошибке
    печатается путь. ПДА должен оставаться в загрузчике до последнего чтения.
    """
    validate_manifest(m, pkgdir)
    selected = set(selected)
    if not selected or not selected <= set(FILES):
        raise ValueError("выбери карту и/или точки")
    root = Path(backup_dir) if backup_dir else Path.home() / "Documents" / "HorizonBackups"
    root.mkdir(parents=True, exist_ok=True)
    # Проверить доступ к папке ДО подключения/чтения.
    with tempfile.TemporaryFile(dir=root):
        pass
    name = datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    backup = root / ("hz-littlefs-" + name + ".bin")
    base = flasher.global_argv(m, port, baud)
    keep = base[:-2] + ["--after", "no-reset"]
    # После первого чтения ПДА остаётся в ROM bootloader; последующие
    # esptool-команды не должны аппаратно сбрасывать его перед подключением.
    stay = ["no-reset" if arg == "default-reset" else arg for arg in keep]
    if initial_no_reset:
        keep = stay  # редактор уже прочитал таблицу и оставил ПДА в загрузчике
    finish = ["no-reset" if arg == "default-reset" else arg for arg in base]
    with tempfile.TemporaryDirectory(prefix="hz-data-") as tmp:
        part = Path(tmp) / "partitions.bin"
        original = Path(tmp) / "original.bin"
        patched = Path(tmp) / "patched.bin"
        verify = Path(tmp) / "verify.bin"
        if progress:
            progress("Проверка таблицы разделов...")
        _esptool(keep + ["read-flash", "0x8000", "0x1000", str(part)], "Чтение таблицы")
        offset, size, label = parse_partitions_bin(part)
        if (offset, size) != (m["fs_offset"], m["fs_size"]) or label.lower() != m.get("fs_label", "spiffs"):
            raise ValueError("Раздел на ПДА не совпадает с пакетом (смещение/размер/тип). Запись запрещена.")
        if progress:
            progress("Чтение пользовательских данных...")
        _esptool(stay + ["read-flash", hex(offset), hex(size), str(original)], "Чтение ФС")
        if original.stat().st_size != size:
            raise ValueError("ФС прочитана не полностью; запись запрещена")
        shutil.copyfile(original, backup)  # копия вне temp, ДО изменения и записи
        print(f"Резервная копия данных: {backup}")
        try:
            if progress:
                progress("Обновление файлов в копии ФС...")
            patched.write_bytes(patch_image(original.read_bytes(), size, pkgdir, selected))
            if progress:
                progress("Запись только раздела данных...")
            _esptool(stay + ["write-flash", hex(offset), str(patched)], "Запись ФС")
            if progress:
                progress("Повторное чтение и сравнение...")
            _esptool(finish + ["read-flash", hex(offset), hex(size), str(verify)], "Проверка ФС")
            if verify.read_bytes() != patched.read_bytes():
                raise RuntimeError("Образ после записи отличается от подготовленного")
        except Exception as e:
            raise RuntimeError(f"{e}. Резервная копия: {backup}. Не отключай ПДА до выяснения причины.") from e
    return str(backup)
