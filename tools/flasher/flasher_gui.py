#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""HorizonFlasher GUI: окно «ZIP-пакет + порт + ПРОШИТЬ».

Запуск из исходников (любой Python 3.10+ с tkinter + requirements.txt):
  python flasher_gui.py
Полевая сборка: build_exe.bat -> dist/HorizonFlasher.exe.

Ядро прошивки переиспользуется из flasher.py (манифест, порты, esptool),
ZIP-пакет - из package.py. Сама запись идёт в фоновом потоке, окно живое.
"""
import os
import queue
import re
import sys
import threading
import tkinter as tk
import traceback
from tkinter import filedialog, messagebox, scrolledtext, ttk

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

import flasher  # noqa: E402
import package as zpkg  # noqa: E402

APP_TITLE = "HorizonFlasher — прошивка ПДА «Горизонт»"
AUTO_PORT = "Авто"
PCT_RE = re.compile(r"\(\s*(\d{1,3})\s*%\)")
LOG_LINE_CAP = 5000


def parse_progress(text):
    """Вытащить последний процент вида '( 42 %)' из куска лога esptool."""
    vals = PCT_RE.findall(text or "")
    if not vals:
        return None
    try:
        return max(0, min(100, int(vals[-1])))
    except ValueError:
        return None


def auto_pick(ports):
    """Выбрать порт как CLI, но без вопроса в консоль: сначала J2 (303A), затем мосты."""
    devs = [p for p in (ports or []) if p.device]
    if not devs:
        return None, "COM-порты не найдены"
    pref = [p for p in devs if (p.vid or 0) in flasher.VID_PREFERRED]
    cands = flasher.preferred_first(pref) or devs
    note = "Порт определён автоматически: " + flasher.describe(cands[0])
    if len(cands) > 1:
        note += f" (ещё {len(cands) - 1} шт. - можно выбрать вручную)"
    if (cands[0].vid or 0) != flasher.VID_ESP_JTAG:
        note += ". " + flasher.HINT_J2
    return cands[0].device, note


class QueueWriter:
    """Файлоподобный приёмник stdout/stderr -> очередь GUI."""

    def __init__(self, q):
        self.q = q

    def write(self, s):
        if s:
            self.q.put(("log", str(s)))

    def flush(self):
        pass


class FlashWorker(threading.Thread):
    """Запись прошивки в фоне. Протокол очереди: log/stage/progress/done."""

    def __init__(self, q, manifest, port_choice, baud_choice, erase_all, skip_fs):
        super().__init__(daemon=True)
        self.q = q
        self.manifest = manifest
        self.port_choice = port_choice
        self.baud_choice = baud_choice
        self.erase_all = erase_all
        self.skip_fs = skip_fs

    def fail(self, msg):
        self.q.put(("done", False, msg))

    def run(self):
        old_out, old_err = sys.stdout, sys.stderr
        sys.stdout = sys.stderr = QueueWriter(self.q)
        try:
            self.go()
        except Exception:  # noqa: BLE001 - всё в лог, окну коротко
            self.q.put(("log", traceback.format_exc()))
            self.fail("Внутренняя ошибка (подробности в логе)")
        finally:
            sys.stdout, sys.stderr = old_out, old_err

    def go(self):
        m = self.manifest
        self.q.put(("stage", "Подготовка..."))
        if self.port_choice == AUTO_PORT:
            ports, err = flasher.list_serial_ports()
            if err:
                self.fail(err)
                return
            port, note = auto_pick(ports)
            if not port:
                self.fail(
                    "Не найдено COM-портов. Проверь: кабель воткнут в J2 "
                    "(OTG, верхний USB-C — разъём прошивки), кабель с данными "
                    "(не charge-only)."
                )
                return
            print(note)
        else:
            port = self.port_choice.split(" ")[0]
        try:
            baud = int(self.baud_choice) if self.baud_choice else int(m.get("baud", 921600))
        except ValueError:
            self.fail(f"плохая скорость: {self.baud_choice}")
            return

        print(f"{flasher.APP}: прошивка ПДА «Горизонт»")
        print(f"Пакет: {m.get('name', '?')}")
        print(f"Прошивка: v{m.get('fw_version', '?')}")
        print(f"Порт: {port}, скорость: {baud}")
        print("Состав:")
        for p in m["parts"]:
            tag = " [FS]" if p["tag"] == "fs" else ""
            print(f"  {p['offset']:#08x}  {os.path.basename(p['file'])}{tag}")

        if self.erase_all:
            self.q.put(("stage", "Стирание всего чипа..."))
            self.q.put(("progress", "marquee"))
            code, err = flasher.run_esptool(
                flasher.global_argv(m, port, baud) + ["erase-flash"]
            )
            if err or code != 0:
                self.fail(flasher.fail_text("Стирание не удалось", code, err))
                return

        self.q.put(("stage", "Запись прошивки..."))
        self.q.put(("progress", 0))
        code, err = flasher.run_esptool(
            flasher.build_write_argv(m, port, baud, self.skip_fs)
        )
        if err or code != 0:
            self.fail(flasher.fail_text("Запись не удалась", code, err))
            return
        self.q.put(("progress", 100))
        self.q.put(("done", True, "ГОТОВО: прошивка записана, устройство перезапущено."))


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(APP_TITLE)
        self.minsize(660, 600)
        try:
            self.state("zoomed")
        except tk.TclError:
            pass
        self.q = queue.Queue()
        self.pkg = None
        self.pkg_baud = 921600
        self.worker = None
        self.write_stage = False
        self._build()
        self.refresh_ports()
        self.after(100, self.poll)
        self.protocol("WM_DELETE_WINDOW", self.on_close)

    # --- построение окна ---

    def _build(self):
        root = ttk.Frame(self, padding=10)
        root.pack(fill=tk.BOTH, expand=True)
        root.columnconfigure(1, weight=1)

        ttk.Label(root, text="ZIP-пакет прошивки:").grid(row=0, column=0, sticky=tk.W)
        self.zip_var = tk.StringVar(value="(не выбран)")
        ttk.Entry(root, textvariable=self.zip_var, state="readonly").grid(
            row=0, column=1, sticky=tk.EW, padx=(6, 6)
        )
        self.browse_btn = ttk.Button(root, text="Обзор...", command=self.browse_zip)
        self.browse_btn.grid(row=0, column=2)

        self.pkg_info = tk.Label(
            root, text="1. Выбери ZIP-пакет прошивки (кнопка «Обзор...»).",
            justify=tk.LEFT, anchor=tk.W, bg="#f0f0f0", relief=tk.SUNKEN,
            padx=6, pady=6, font=("TkDefaultFont", 9),
        )
        self.pkg_info.grid(row=1, column=0, columnspan=3, sticky=tk.EW, pady=(6, 0))

        ttk.Label(root, text="Порт:").grid(row=2, column=0, sticky=tk.W, pady=(8, 0))
        self.port_combo = ttk.Combobox(root, state="readonly", values=[AUTO_PORT])
        self.port_combo.set(AUTO_PORT)
        self.port_combo.grid(row=2, column=1, sticky=tk.EW, padx=(6, 6), pady=(8, 0))
        self.refresh_btn = ttk.Button(root, text="Обновить", command=self.refresh_ports)
        self.refresh_btn.grid(row=2, column=2, pady=(8, 0))

        ttk.Label(root, text="Скорость:").grid(row=3, column=0, sticky=tk.W, pady=(6, 0))
        self.baud_combo = ttk.Combobox(root, state="readonly")
        self.baud_combo.grid(row=3, column=1, sticky=tk.W, padx=(6, 0), pady=(6, 0))
        self._update_bauds()

        self.erase_var = tk.BooleanVar(value=False)
        self.skipfs_var = tk.BooleanVar(value=False)
        self.erase_chk = ttk.Checkbutton(
            root, text="Стереть весь чип перед записью (чистое устройство)",
            variable=self.erase_var,
        )
        self.erase_chk.grid(row=4, column=0, columnspan=3, sticky=tk.W, pady=(6, 0))
        self.skipfs_chk = ttk.Checkbutton(
            root, text="Не трогать файловую систему (сохранить данные)",
            variable=self.skipfs_var,
        )
        self.skipfs_chk.grid(row=5, column=0, columnspan=3, sticky=tk.W)

        self.flash_btn = ttk.Button(
            root, text="ПРОШИТЬ", command=self.start_flash, state=tk.DISABLED
        )
        self.flash_btn.grid(row=6, column=0, columnspan=3, sticky=tk.EW, pady=(8, 0),
                            ipady=6)

        self.bar = ttk.Progressbar(root, mode="determinate", maximum=100, value=0)
        self.bar.grid(row=7, column=0, columnspan=3, sticky=tk.EW, pady=(8, 0))
        self.status_var = tk.StringVar(value="Ожидание.")
        ttk.Label(root, textvariable=self.status_var).grid(
            row=8, column=0, columnspan=3, sticky=tk.W
        )

        self.log = scrolledtext.ScrolledText(root, wrap=tk.WORD, state=tk.DISABLED,
                                             height=16, font=("Consolas", 9))
        self.log.grid(row=9, column=0, columnspan=3, sticky=tk.NSEW, pady=(6, 0))
        root.rowconfigure(9, weight=1)
        self.savelog_btn = ttk.Button(root, text="Сохранить лог...",
                                      command=self.save_log)
        self.savelog_btn.grid(row=10, column=2, sticky=tk.E, pady=(6, 0))

    # --- пакет ---

    def browse_zip(self):
        path = filedialog.askopenfilename(
            title="Выбери ZIP-пакет прошивки",
            filetypes=[("ZIP-пакеты", "*.zip"), ("Все файлы", "*.*")],
        )
        if not path:
            return
        self._close_pkg()
        pkg = zpkg.ZipPackage(path)
        if not pkg.ok:
            self.zip_var.set("(не выбран)")
            self.pkg_info.config(text=f"Пакет не подошёл: {pkg.error}")
            self.status_var.set("Ошибка пакета.")
            messagebox.showerror("Пакет", f"Пакет не подошёл:\n{pkg.error}")
            return
        self.pkg = pkg
        self.zip_var.set(path)
        self.pkg_info.config(text=pkg.summary())
        try:
            self.pkg_baud = int(pkg.manifest.get("baud", 921600))
        except ValueError:
            self.pkg_baud = 921600
        self._update_bauds()
        self.flash_btn.config(state=tk.NORMAL)
        self.status_var.set("2. Проверь порт и жми ПРОШИТЬ.")
        self.log_line(f"Выбран пакет: {path}\n{pkg.summary()}\n")

    def _close_pkg(self):
        if self.pkg is not None:
            self.pkg.close()
            self.pkg = None

    def _update_bauds(self):
        auto = f"Из пакета ({self.pkg_baud})"
        self.baud_combo.config(values=[auto, "921600", "460800", "230400", "115200"])
        self.baud_combo.set(auto)

    def baud_value(self):
        v = self.baud_combo.get()
        if v.startswith("Из пакета"):
            return ""
        return v

    # --- порты ---

    def refresh_ports(self):
        ports, err = flasher.list_serial_ports()
        if err:
            self.port_combo.config(values=[AUTO_PORT])
            self.port_combo.set(AUTO_PORT)
            self.status_var.set(f"Порты: {err}")
            return
        devs = [p for p in (ports or []) if p.device]
        values = [AUTO_PORT] + [flasher.describe(p) for p in devs]
        cur = self.port_combo.get()
        self.port_combo.config(values=values)
        self.port_combo.set(cur if cur in values else AUTO_PORT)
        if not devs:
            self.status_var.set("Порты: ничего не найдено (воткни плату в J2 (OTG) и жми Обновить).")
        else:
            self.status_var.set(f"Порты: найдено {len(devs)} шт.")

    # --- прошивка ---

    def start_flash(self):
        if self.worker is not None and self.worker.is_alive():
            return
        if self.pkg is None or not self.pkg.ok:
            messagebox.showerror("Пакет", "Сначала выбери ZIP-пакет прошивки.")
            return
        if self.erase_var.get() and self.skipfs_var.get():
            messagebox.showerror(
                "Опции",
                "«Стереть всё» + «Не трогать ФС» вместе нельзя: стирание снесёт "
                "данные в любом случае. Убери одну галочку.",
            )
            return
        self._set_busy(True)
        self.bar.config(mode="determinate")
        self.bar["value"] = 0
        self.write_stage = False
        self.status_var.set("Прошивка...")
        self.worker = FlashWorker(
            self.q, self.pkg.manifest, self.port_combo.get(), self.baud_value(),
            self.erase_var.get(), self.skipfs_var.get(),
        )
        self.worker.start()

    def poll(self):
        try:
            while True:
                kind, *rest = self.q.get_nowait()
                if kind == "log":
                    self.log_line(rest[0])
                    if self.write_stage:
                        pct = parse_progress(rest[0])
                        if pct is not None:
                            self.bar["value"] = pct
                            self.status_var.set(f"Запись прошивки... {pct}%")
                elif kind == "stage":
                    self.status_var.set(rest[0])
                    self.log_line(f"--- {rest[0]} ---\n")
                    self.write_stage = rest[0].startswith("Запись")
                elif kind == "progress":
                    v = rest[0]
                    if v == "marquee":
                        self.bar.config(mode="indeterminate")
                        self.bar.start(20)
                    else:
                        self.bar.stop()
                        self.bar.config(mode="determinate")
                        self.bar["value"] = v
                elif kind == "done":
                    ok, msg = rest
                    self.bar.stop()
                    self.bar.config(mode="determinate")
                    if ok:
                        self.bar["value"] = 100
                    self._set_busy(False)
                    self.status_var.set(msg)
                    self.log_line(msg + "\n")
                    if ok:
                        messagebox.showinfo("Готово", msg)
                    else:
                        messagebox.showerror("Ошибка", msg + "\n\nПодробности — в логе окна.")
        except queue.Empty:
            pass
        self.after(100, self.poll)

    def _set_busy(self, busy):
        st = tk.DISABLED if busy else tk.NORMAL
        self.browse_btn.config(state=st)
        self.refresh_btn.config(state=st)
        self.port_combo.config(state=tk.DISABLED if busy else "readonly")
        self.baud_combo.config(state=tk.DISABLED if busy else "readonly")
        self.erase_chk.config(state=st)
        self.skipfs_chk.config(state=st)
        self.flash_btn.config(state=tk.DISABLED if busy or self.pkg is None else tk.NORMAL)

    # --- лог и выход ---

    def log_line(self, s):
        self.log.config(state=tk.NORMAL)
        self.log.insert(tk.END, s)
        if int(self.log.index("end-1c").split(".")[0]) > LOG_LINE_CAP:
            self.log.delete("1.0", f"{LOG_LINE_CAP // 2}.0")
        self.log.see(tk.END)
        self.log.config(state=tk.DISABLED)

    def save_log(self):
        path = filedialog.asksaveasfilename(
            title="Сохранить лог",
            defaultextension=".txt",
            filetypes=[("Текстовые файлы", "*.txt"), ("Все файлы", "*.*")],
        )
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8") as f:
                f.write(self.log.get("1.0", tk.END))
            self.status_var.set(f"Лог сохранён: {path}")
        except OSError as e:
            messagebox.showerror("Лог", f"Не могу сохранить:\n{e}")

    def on_close(self):
        if self.worker is not None and self.worker.is_alive():
            messagebox.showwarning(
                "Прошивка идёт",
                "Сейчас идёт запись — окно закрыть нельзя (обрыв связи "
                "испортит прошивку). Дождись конца.",
            )
            return
        self._close_pkg()
        self.destroy()


def main():
    try:
        app = App()
        app.mainloop()
    except Exception:
        import tempfile
        tb = traceback.format_exc()
        print(tb, file=sys.stderr)
        try:
            fd, path = tempfile.mkstemp(prefix="hzflash-error-", suffix=".txt")
            with os.fdopen(fd, "w", encoding="utf-8") as f:
                f.write(tb)
        except OSError:
            path = "(лог записать не смог)"
        try:
            messagebox.showerror(
                "HorizonFlasher",
                f"Непредвиденная ошибка:\n{tb[-1500:]}\n\nПолный текст: {path}",
            )
        except Exception:
            pass
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
