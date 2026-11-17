"""Отдельная консоль журнала HorizonMapStudio.exe (Windows --windowed).

Не запускает второй процесс и не меняет HorizonFlasher. Создаётся только
перед записью, остаётся на экране до закрытия редактора. Потоки esptool
подключены к настоящему CONOUT$, а не к None (как в PyInstaller --windowed).
"""
import os
import sys


class ProjectConsole:
    def __init__(self):
        self.out = None
        self.err = None

    def open(self):
        """Вернуть видимый поток; при неудаче остановить запись ДО чтения/записи ПДА."""
        if os.name != 'nt':
            return sys.stdout  # запуск из исходников на другой ОС
        if self.out is not None:
            return self.out
        if sys.stdout is not None and sys.stderr is not None:
            return sys.stdout  # уже запущено из видимой консоли

        import ctypes
        from ctypes import wintypes
        kernel = ctypes.windll.kernel32
        kernel.GetConsoleWindow.restype = wintypes.HWND
        # У оконного PyInstaller обычно нет консоли. Если она уже есть,
        # используем её; AllocConsole тогда вернул бы ERROR_ACCESS_DENIED.
        new_console = not kernel.GetConsoleWindow()
        if new_console and not kernel.AllocConsole():
            raise OSError('Не удалось открыть консоль журнала; запись не начата')
        try:
            kernel.SetConsoleOutputCP(65001)
            out = open('CONOUT$', 'w', encoding='utf-8', errors='replace', buffering=1)
            err = open('CONOUT$', 'w', encoding='utf-8', errors='replace', buffering=1)
        except OSError as exc:
            if 'out' in locals():
                out.close()
            if new_console:
                kernel.FreeConsole()
            raise OSError(f'Не удалось подключить вывод к консоли: {exc}; запись не начата') from exc
        if new_console:
            # Закрытие X у консоли завершило бы и редактор во время записи ФС.
            # Оставляем окно журнала до закрытия основного окна; его можно свернуть.
            user = ctypes.windll.user32
            user.GetSystemMenu.argtypes = (wintypes.HWND, wintypes.BOOL)
            user.GetSystemMenu.restype = wintypes.HMENU
            user.DeleteMenu.argtypes = (wintypes.HMENU, wintypes.UINT, wintypes.UINT)
            user.DrawMenuBar.argtypes = (wintypes.HWND,)
            hwnd = kernel.GetConsoleWindow()
            menu = user.GetSystemMenu(hwnd, False)
            if menu:
                user.DeleteMenu(menu, 0xF060, 0)  # SC_CLOSE, MF_BYCOMMAND
                user.DrawMenuBar(hwnd)
        self.out, self.err = out, err
        sys.stdout, sys.stderr = out, err
        return out

    def line(self, text):
        stream = self.open()
        if stream:
            print(text, file=stream, flush=True)


def run_project_upload(console, uploader, project, port, baud, status):
    """Этапы в консоль и GUI; никаких Tk-вызовов из рабочего потока."""
    console.line(f'=== Запись проекта на {port}, {baud} бод ===')
    def stage(text):
        console.line(text)
        status(text)
    try:
        result = uploader(project, port, baud, progress=stage)
    except Exception as exc:
        try:
            console.line(f'ОШИБКА: {exc}')
        except OSError:
            pass  # не затереть исходную ошибку записи с путём резервной копии
        raise
    backup, resolution, removed = result
    console.line(f'ГОТОВО. Карты до {resolution}×{resolution * 2 // 3}. Копия: {backup}')
    return result
