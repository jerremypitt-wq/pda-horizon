#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""HorizonMapStudio: отдельное окно редактирования вкладок и отправки на ПДА.

Запуск: python map_studio.py; сборка: build_map_studio.bat.
Существующие HorizonFlasher.exe и встроенный однослойный редактор не меняются.
"""
import queue
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, simpledialog, ttk

import editor_data
import flasher
import map_editor
import map_studio_console
import multi_map


class MapStudio(map_editor.MapEditor):
    def __init__(self, parent):
        self.tabs = []
        self.active = 0
        self.next_id = 1
        self.events = queue.Queue()
        self.console = map_studio_console.ProjectConsole()
        super().__init__(parent, self._upload_project)
        self.title('HorizonMapStudio 1.12.2 — карты и этажи')
        self.after(100, self._poll)

    def _build(self):
        super()._build()
        # Отдельная полоса этажей между подсказкой и рабочим полем карты.
        main, status = self.winfo_children()[2:]
        self.tabs_bar = ttk.Frame(self, padding=(8, 2, 8, 6))
        self.tabs_bar.pack(fill=tk.X, before=main)
        ttk.Label(self.tabs_bar, text='Вкладка:').pack(side=tk.LEFT)
        self.tab_choice = ttk.Combobox(self.tabs_bar, state='readonly', width=23)
        self.tab_choice.pack(side=tk.LEFT, padx=5)
        self.tab_choice.bind('<<ComboboxSelected>>', self._choose)
        for text, callback in (('+ JPG', self.add_tab), ('Переименовать', self.rename_tab),
                               ('Удалить', self.delete_tab), ('↑', lambda: self.move_tab(-1)),
                               ('↓', lambda: self.move_tab(1))):
            ttk.Button(self.tabs_bar, text=text, command=callback).pack(side=tk.LEFT, padx=2)
        self.port_bar = ttk.Frame(self, padding=(8, 1, 8, 4))
        self.port_bar.pack(fill=tk.X, before=status)
        ttk.Label(self.port_bar, text='Порт ПДА (верхний J2):').pack(side=tk.LEFT)
        self.port = ttk.Combobox(self.port_bar, state='readonly', width=36, values=('Авто',))
        self.port.set('Авто')
        self.port.pack(side=tk.LEFT, padx=5)
        ttk.Button(self.port_bar, text='Обновить порты', command=self.refresh_ports).pack(side=tk.LEFT)
        ttk.Label(self.port_bar, text='Скорость:').pack(side=tk.LEFT, padx=(16, 2))
        self.baud = ttk.Combobox(self.port_bar, state='readonly', width=10,
                                  values=('921600', '460800', '115200'))
        self.baud.set('921600')
        self.baud.pack(side=tk.LEFT)
        self.refresh_ports()
        self.upload_btn.configure(text='ЗАПИСАТЬ ВСЕ ВКЛАДКИ НА ПДА')

    def refresh_ports(self):
        ports, error = flasher.list_serial_ports()
        values = ['Авто'] + [f'{p.device}  {p.description}' for p in ports]
        old = self.port.get()
        self.port.configure(values=values)
        self.port.set(old if old in values else 'Авто')
        if error:
            self.set_status(error)

    def _refresh_tabs(self):
        self.tab_choice.configure(values=[f'{i + 1}. {name}' for i, (_, name, _) in enumerate(self.tabs)])
        self.tab_choice.current(self.active) if self.tabs else self.tab_choice.set('')
        self._refresh_list()

    def _set_all(self, project, path=None):
        self.tabs = project.tabs
        self.next_id = project.next_id
        self.active = 0
        self._set_project(self.tabs[0][2], path)
        self._refresh_tabs()
        self.dirty = path is None

    def _switch(self, index):
        if self.busy or not 0 <= index < len(self.tabs):
            return False
        if index == self.active:
            return True
        if not self._commit_form():
            self.tab_choice.current(self.active)
            return False
        dirty, path = self.dirty, self.project_path
        self.active = index
        self._set_project(self.tabs[index][2], path)
        self.dirty = dirty
        self._refresh_tabs()
        return True

    def _choose(self, _event):
        self._switch(self.tab_choice.current())

    def _refresh_list(self):
        super()._refresh_list()
        if self.tabs:
            total = sum(len(page.points) for _, _, page in self.tabs)
            self.count_label.configure(text=f'Точки вкладки {len(self.project.points)}; всего {total}/50')

    def open_project(self):
        if self.busy or not self._discard_ok():
            return
        path = filedialog.askopenfilename(parent=self, title='Проект карты',
                                          filetypes=[('Проект Горизонта', '*.hzmap'), ('Все файлы', '*.*')])
        if path:
            try:
                self._set_all(multi_map.MultiMapProject.load(path), path)
            except (OSError, ValueError) as exc:
                messagebox.showerror('Проект', str(exc), parent=self)

    def open_jpg(self):
        if self.busy or (not self.tabs and not self._discard_ok()):
            return
        path = filedialog.askopenfilename(parent=self, title='Открыть JPG (3:2)',
                                          filetypes=[('JPEG', '*.jpg *.jpeg')])
        if not path:
            return
        try:
            page = editor_data.MapProject.from_jpg(path)
        except (OSError, ValueError) as exc:
            messagebox.showerror('Карта', str(exc), parent=self)
            return
        if not self.tabs:
            self._set_all(multi_map.MultiMapProject([(0, 'Карта', page)]))
        else:
            if not messagebox.askyesno('Заменить карту вкладки',
                                       'Заменить карту и стартовые точки ТЕКУЩЕЙ вкладки? Остальные вкладки останутся.', parent=self):
                return
            id_, title, _ = self.tabs[self.active]
            self.tabs[self.active] = (id_, title, page)
            self._set_project(page, self.project_path)
            self.dirty = True
            self._refresh_tabs()

    def add_tab(self):
        if self.busy:
            return
        if not self.tabs:
            self.open_jpg()
            return
        if len(self.tabs) >= multi_map.MAX_TABS:
            messagebox.showwarning('Лимит', 'Не более четырёх вкладок.', parent=self)
            return
        if not self._commit_form():
            return
        path = filedialog.askopenfilename(parent=self, title='JPG для нового этажа',
                                          filetypes=[('JPEG', '*.jpg *.jpeg')])
        if not path:
            return
        title = simpledialog.askstring('Новая вкладка', 'Название (например: Здание, 1 этаж):',
                                       parent=self)
        if title is None:
            return
        try:
            page = editor_data.MapProject.from_jpg(path)
            title = multi_map.validate_name(title)
            if self.next_id > multi_map.MAX_TAB_ID:
                raise ValueError('Исчерпан запас ID вкладок; начни новый проект')
            id_ = self.next_id
            self.next_id += 1
            self.tabs.append((id_, title, page))
            self.dirty = True
            self._switch(len(self.tabs) - 1)
        except (OSError, ValueError) as exc:
            messagebox.showerror('Вкладка', str(exc), parent=self)

    def rename_tab(self):
        if self.busy or not self.tabs:
            return
        id_, title, page = self.tabs[self.active]
        name = simpledialog.askstring('Название вкладки', 'Новое название:', initialvalue=title, parent=self)
        if name is not None:
            try:
                self.tabs[self.active] = (id_, multi_map.validate_name(name), page)
                self.dirty = True
                self._refresh_tabs()
            except ValueError as exc:
                messagebox.showerror('Название', str(exc), parent=self)

    def delete_tab(self):
        if self.busy or len(self.tabs) < 2:
            return
        if not self._commit_form():
            return
        _, title, _ = self.tabs[self.active]
        if not messagebox.askyesno('Удаление этажа', f'Удалить «{title}» и его точки? При отправке проекта '
                                   'свои и принятые точки этой вкладки на ПДА тоже будут удалены. '
                                   'Чаты и точки остальных вкладок останутся. Продолжить?', parent=self):
            return
        self.tabs.pop(self.active)
        self.active = min(self.active, len(self.tabs) - 1)
        self._set_project(self.tabs[self.active][2], self.project_path)
        self.dirty = True
        self._refresh_tabs()

    def move_tab(self, direction):
        if self.busy or not self.tabs or not self._commit_form():
            return
        nxt = self.active + direction
        if 0 <= nxt < len(self.tabs):
            self.tabs[self.active], self.tabs[nxt] = self.tabs[nxt], self.tabs[self.active]
            self.active = nxt
            self.dirty = True
            self._refresh_tabs()

    def save_project(self):
        if self.busy or not self.tabs or not self._commit_form():
            return False
        path = self.project_path or filedialog.asksaveasfilename(parent=self, title='Сохранить проект',
                    defaultextension='.hzmap', filetypes=[('Проект Горизонта', '*.hzmap')])
        if not path:
            return False
        try:
            multi_map.MultiMapProject(self.tabs, self.next_id).save(path)
            self.project_path = path
            self.dirty = False
            self.set_status(f'Проект сохранён: {path}')
            return True
        except (OSError, ValueError) as exc:
            messagebox.showerror('Проект', str(exc), parent=self)
            return False

    def upload(self):
        if self.busy or not self.tabs or not self._commit_form():
            return
        try:
            project = multi_map.MultiMapProject(self.tabs, self.next_id)
        except ValueError as exc:
            messagebox.showerror('Проект', str(exc), parent=self)
            return
        if not messagebox.askyesno('Запись карт',
                'Нужна прошивка 1.12.0.ZI или новее на ПДА. Все вкладки и стартовые точки будут заменены. '
                'Точки удалённых вкладок на ПДА будут УДАЛЕНЫ. Остальные пользовательские данные '
                'сохранятся; до записи создаётся резервная копия. ПДА подключён к верхнему J2 и в режиме BOOT?',
                parent=self):
            return
        self.on_upload(project.snapshot())

    def _upload_project(self, project):
        choice = self.port.get().split(' ')[0]
        if choice == 'Авто':
            ports, error = flasher.list_serial_ports()
            if error:
                messagebox.showerror('Порт', error, parent=self)
                return
            devices = [p for p in ports if p.device]
            if len(devices) != 1:
                messagebox.showerror('Порт', 'Выбери порт J2 вручную (найдено: %d).' % len(devices), parent=self)
                return
            choice = devices[0].device
        baud = int(self.baud.get())  # Tkinter-поля читать только из GUI-потока
        self.set_busy(True)
        def job():
            try:
                # До любого доступа к ПДА: на Windows открыть видимую консоль.
                result = map_studio_console.run_project_upload(
                    self.console, multi_map.upload_project, project, choice, baud,
                    lambda msg: self.events.put(('status', msg)))
                self.events.put(('ok', result))
            except Exception as exc:
                self.events.put(('error', str(exc)))
        threading.Thread(target=job, daemon=True).start()

    def close_editor(self):
        super().close_editor()
        if not self.winfo_exists():
            self.master.destroy()

    def _poll(self):
        try:
            while True:
                kind, data = self.events.get_nowait()
                if kind == 'status':
                    self.set_status(data)
                else:
                    self.set_busy(False)
                    if kind == 'ok':
                        backup, resolution, removed = data
                        self.set_status(f'Записано. Карты до {resolution}×{resolution * 2 // 3}. Копия: {backup}')
                        messagebox.showinfo('Готово', f'Карты записаны. Резервная копия: {backup}', parent=self)
                    else:
                        messagebox.showerror('Запись не выполнена', data, parent=self)
        except queue.Empty:
            pass
        self.after(100, self._poll)


def main():
    root = tk.Tk()
    root.withdraw()
    studio = MapStudio(root)
    root.mainloop()


if __name__ == '__main__':
    main()
