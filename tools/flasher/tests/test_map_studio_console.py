"""Windows --windowed: открытие живой консоли перед записью (без ПДА)."""
import io
import ctypes
from ctypes import wintypes  # preload before simulating Windows on Linux
import os
import sys
import types
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import map_studio_console  # noqa: E402


class Function:
    def __init__(self, fn):
        self.fn = fn

    def __call__(self, *args):
        return self.fn(*args)


class ConsoleTests(unittest.TestCase):
    def test_windowed_console_is_created_once_and_receives_log(self):
        logs = []
        calls = []
        kernel = types.SimpleNamespace(
            GetConsoleWindow=Function(lambda: 123 if calls else 0),
            AllocConsole=Function(lambda: calls.append('alloc') or 1),
            FreeConsole=Function(lambda: calls.append('free')),
            SetConsoleOutputCP=Function(lambda n: calls.append(('cp', n))))
        user = types.SimpleNamespace(
            GetSystemMenu=Function(lambda *_: 456),
            DeleteMenu=Function(lambda *args: calls.append(('delete', args))),
            DrawMenuBar=Function(lambda *_: None))
        fake_windll = types.SimpleNamespace(kernel32=kernel, user32=user)
        original_out, original_err = sys.stdout, sys.stderr
        with mock.patch.object(map_studio_console.os, 'name', 'nt'), \
             mock.patch.object(ctypes, 'windll', fake_windll, create=True), \
             mock.patch('builtins.open', side_effect=lambda *_args, **_kw: logs.append(io.StringIO()) or logs[-1]), \
             mock.patch.object(sys, 'stdout', None), mock.patch.object(sys, 'stderr', None):
            console = map_studio_console.ProjectConsole()
            console.line('Чтение таблицы...')
            console.line('ГОТОВО')
            self.assertEqual(calls.count('alloc'), 1)
            self.assertEqual(len(logs), 2)
            self.assertIn('Чтение таблицы...', logs[0].getvalue())
            self.assertIn('ГОТОВО', logs[0].getvalue())
            self.assertIn(('cp', 65001), calls)
            self.assertTrue(any(c[0] == 'delete' for c in calls if isinstance(c, tuple)))
            self.assertIs(sys.stdout, console.out)
            self.assertIs(sys.stderr, console.err)
        sys.stdout, sys.stderr = original_out, original_err

    def test_failed_console_refuses_upload_before_device_access(self):
        kernel = types.SimpleNamespace(
            GetConsoleWindow=Function(lambda: 0),
            AllocConsole=Function(lambda: 0))
        with mock.patch.object(map_studio_console.os, 'name', 'nt'), \
             mock.patch.object(ctypes, 'windll', types.SimpleNamespace(kernel32=kernel), create=True), \
             mock.patch.object(sys, 'stdout', None), mock.patch.object(sys, 'stderr', None):
            with self.assertRaisesRegex(OSError, 'запись не начата'):
                map_studio_console.ProjectConsole().line('Начало')

    def test_stage_and_result_are_visible_in_console_and_gui(self):
        class Log:
            lines = []
            def line(self, text):
                self.lines.append(text)
        log = Log()
        statuses = []
        def upload(_project, _port, _baud, progress):
            progress('Чтение таблицы...')
            progress('Запись ФС...')
            return 'backup.bin', 480, set()
        result = map_studio_console.run_project_upload(
            log, upload, object(), 'COM4', 460800, statuses.append)
        self.assertEqual(result[0], 'backup.bin')
        self.assertEqual(statuses, ['Чтение таблицы...', 'Запись ФС...'])
        self.assertIn('ГОТОВО', log.lines[-1])
        self.assertIn('COM4', log.lines[0])
        def fail(_project, _port, _baud, progress):
            progress('Чтение таблицы...')
            raise ValueError('Порт не отвечает')
        with self.assertRaisesRegex(ValueError, 'Порт не отвечает'):
            map_studio_console.run_project_upload(log, fail, object(), 'COM4', 460800, statuses.append)
        self.assertIn('ОШИБКА', log.lines[-1])

    def test_existing_console_needs_no_new_window(self):
        out, err = io.StringIO(), io.StringIO()
        with mock.patch.object(map_studio_console.os, 'name', 'nt'), \
             mock.patch.object(sys, 'stdout', out), mock.patch.object(sys, 'stderr', err):
            console = map_studio_console.ProjectConsole()
            console.line('Этап')
            self.assertIn('Этап', out.getvalue())
            self.assertIsNone(console.out)


if __name__ == '__main__':
    unittest.main()
