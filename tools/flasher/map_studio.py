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
        self.anchors = {}
        self.geo_mode = False
        self.geo_selected = None
        self.geo_pending = None
        self.active = 0
        self.next_id = 1
        self.events = queue.Queue()
        self.console = map_studio_console.ProjectConsole()
        super().__init__(parent, self._upload_project)
        self.title('HorizonMapStudio 1.13.0 — карты и геопривязка')
        self.after(100, self._poll)

    def _build(self):
        super()._build()
        # Отдельная полоса этажей между подсказкой и рабочим полем карты.
        main, status = self.winfo_children()[2:]
        self.main = main
        self.points_panel = main.grid_slaves(row=0, column=1)[0]
        self.geo_panel = self._build_geo_panel(main)
        self.canvas.configure(bg='#101510')
        self.geo_panel.grid_remove()
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
        self.mode_bar = ttk.Frame(self, padding=(12, 1, 12, 4))
        self.mode_bar.pack(fill=tk.X, before=main)
        ttk.Label(self.mode_bar, text='Режим карты:').pack(side=tk.LEFT, padx=(0, 8))
        self.points_mode_btn = ttk.Button(self.mode_bar, text='Стартовые точки',
                                          command=lambda: self.set_geo_mode(False))
        self.points_mode_btn.pack(side=tk.LEFT)
        self.geo_mode_btn = ttk.Button(self.mode_bar, text='Геопривязка',
                                       command=lambda: self.set_geo_mode(True))
        self.geo_mode_btn.pack(side=tk.LEFT, padx=6)
        self.points_mode_btn.state(['disabled'])
        ttk.Label(self.mode_bar, text='Привязки видны только здесь — на ПДА не передаются.',
                  foreground='#087b12').pack(side=tk.LEFT, padx=10)
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

    def _build_geo_panel(self, main):
        style = ttk.Style(self)
        style.configure('Geo.TFrame', background='#101510')
        style.configure('Geo.TLabel', background='#101510', foreground='#d6eadd')
        style.configure('GeoTitle.TLabel', background='#101510', foreground='#09b80f')
        style.configure('Geo.TLabelframe', background='#101510', foreground='#09b80f')
        style.configure('Geo.TLabelframe.Label', background='#101510', foreground='#09b80f')
        panel = ttk.Frame(main, style='Geo.TFrame', padding=10)
        panel.columnconfigure(0, weight=1)
        panel.rowconfigure(2, weight=1)
        ttk.Label(panel, text='ГЕОПРИВЯЗКА · текущая вкладка',
                  font=('Arial', 11, 'bold'), style='GeoTitle.TLabel').grid(row=0, column=0, sticky='w')
        self.geo_count = ttk.Label(panel, text='0/20 опорных точек', style='Geo.TLabel')
        self.geo_count.grid(row=1, column=0, sticky='w', pady=(4, 0))
        self.geo_list = tk.Listbox(panel, height=6, exportselection=False, selectmode=tk.SINGLE,
                                   activestyle='none', bg='#172318', fg='#d6eadd', selectbackground='#09b80f',
                                   selectforeground='#071007', relief=tk.FLAT, borderwidth=0)
        self.geo_list.grid(row=2, column=0, sticky='nsew', pady=(5, 10))
        self.geo_list.bind('<<ListboxSelect>>', self._geo_from_list)
        card = ttk.LabelFrame(panel, text='Опорная точка · WGS84', padding=9, style='Geo.TLabelframe')
        card.grid(row=3, column=0, sticky='ew')
        card.columnconfigure(1, weight=1)
        self.geo_name = tk.StringVar()
        self.geo_x = tk.StringVar()
        self.geo_y = tk.StringVar()
        self.geo_lat = tk.StringVar()
        self.geo_lon = tk.StringVar()
        for row, (caption, var) in enumerate((('Название', self.geo_name), ('X · пиксель', self.geo_x),
                                               ('Y · пиксель', self.geo_y), ('Широта · °', self.geo_lat),
                                               ('Долгота · °', self.geo_lon))):
            ttk.Label(card, text=caption, style='Geo.TLabel').grid(row=row, column=0, sticky='w', pady=3)
            entry = tk.Entry(card, textvariable=var, width=22, bg='#182619', fg='#e3f4e6',
                             insertbackground='#09b80f', relief=tk.FLAT, highlightthickness=1,
                             highlightcolor='#09b80f', highlightbackground='#344d36')
            entry.grid(row=row, column=1, sticky='ew', padx=(8, 0), pady=3)
            if row == 3:
                self.geo_lat_entry = entry
        ttk.Label(card, text='Десятичные градусы: 50.7753, 6.0839.\n'
                  'Юг и запад — со знаком минус.', justify=tk.LEFT, style='Geo.TLabel').grid(
                      row=5, column=0, columnspan=2, sticky='w', pady=(5, 2))
        buttons = ttk.Frame(card, style='Geo.TFrame')
        buttons.grid(row=6, column=0, columnspan=2, sticky='w', pady=(8, 0))
        ttk.Button(buttons, text='Сохранить привязку', command=self.geo_apply).pack(side=tk.LEFT)
        ttk.Button(buttons, text='Удалить', command=self.geo_delete).pack(side=tk.LEFT, padx=6)
        ttk.Label(panel, text='Щёлкни по карте, чтобы выбрать место. Зелёные ромбы '
                  'показываются только в редакторе. Для будущей калибровки отметь хотя бы '
                  '3 разнесённые опоры с точными координатами; GPS пока не поддерживается.',
                  wraplength=330, justify=tk.LEFT, style='Geo.TLabel').grid(row=4, column=0, sticky='ew', pady=(12, 0))
        return panel

    def set_geo_mode(self, enabled):
        if self.busy or self.geo_mode == enabled or (self.project and not self._commit_form()):
            return
        self.geo_mode = enabled
        if enabled:
            self.points_panel.grid_remove()
            self.geo_panel.grid(row=0, column=1, sticky='nsew')
            self.points_mode_btn.state(['!disabled'])
            self.geo_mode_btn.state(['disabled'])
            self._geo_refresh()
        else:
            self.geo_panel.grid_remove()
            self.points_panel.grid()
            self.geo_mode_btn.state(['!disabled'])
            self.points_mode_btn.state(['disabled'])
        self._draw_points()
        self.set_status('Геопривязка: выбери место на карте и введи широту/долготу. '
                        'Опоры сохраняются только в .hzmap, не отправляются на ПДА.' if enabled
                        else 'Стартовые точки: щёлкни по карте, чтобы добавить игровую метку.')

    def _current_anchors(self):
        return self.anchors.setdefault(self.tabs[self.active][0], []) if self.tabs else []

    def _geo_fill(self, index=None):
        anchor = self._current_anchors()[index] if index is not None else None
        self.geo_name.set(anchor['name'] if anchor else '')
        self.geo_x.set(str(anchor['x'] if anchor else self.geo_pending[0] if self.geo_pending else ''))
        self.geo_y.set(str(anchor['y'] if anchor else self.geo_pending[1] if self.geo_pending else ''))
        self.geo_lat.set(str(anchor['lat']) if anchor else '')
        self.geo_lon.set(str(anchor['lon']) if anchor else '')

    def _geo_refresh(self):
        self._syncing_geo = True
        self.geo_list.delete(0, tk.END)
        anchors = self._current_anchors()
        for i, a in enumerate(anchors, 1):
            self.geo_list.insert(tk.END, f"{i:02d}  {a['name']}  ·  {a['lat']:.6f}, {a['lon']:.6f}")
        if self.geo_selected is not None:
            self.geo_list.selection_set(self.geo_selected)
            self.geo_list.see(self.geo_selected)
        self.geo_count.configure(text=f'{len(anchors)}/{multi_map.MAX_ANCHORS} опорных точек')
        self._syncing_geo = False
        self._draw_points()

    def _geo_commit(self, explicit=False):
        if not self.project:
            return False
        if (self.geo_selected is None and not self.geo_name.get().strip() and
                not self.geo_lat.get().strip() and not self.geo_lon.get().strip() and not explicit):
            return True
        try:
            try:
                anchor = {'name': self.geo_name.get().strip(),
                          'x': int(self.geo_x.get()), 'y': int(self.geo_y.get()),
                          'lat': float(self.geo_lat.get().strip().replace(',', '.')),
                          'lon': float(self.geo_lon.get().strip().replace(',', '.'))}
            except ValueError as exc:
                raise ValueError('Укажи целые пиксели X/Y и широту/долготу в десятичных градусах.') from exc
            anchors = self._current_anchors()
            updated = anchors.copy()
            if self.geo_selected is None:
                updated.append(anchor)
            else:
                updated[self.geo_selected] = anchor
            updated = multi_map.validate_anchors(updated, self.project.width, self.project.height)
            if updated != anchors:
                self.anchors[self.tabs[self.active][0]] = updated
                self.geo_selected = len(updated) - 1 if self.geo_selected is None else self.geo_selected
                self.geo_pending = None
                self.dirty = True
                self._geo_fill(self.geo_selected)
                self._geo_refresh()
                self.set_status('Привязка добавлена в проект. Сохрани .hzmap, чтобы не потерять её.')
            return True
        except ValueError as exc:
            messagebox.showerror('Геопривязка', str(exc), parent=self)
            return False

    def _commit_form(self, explicit=False):
        return self._geo_commit(explicit) if self.geo_mode else super()._commit_form(explicit)

    def geo_apply(self):
        if not self.busy:
            self._geo_commit(explicit=True)

    def geo_delete(self):
        if self.busy or self.geo_selected is None:
            return
        if not messagebox.askyesno('Удалить привязку', 'Удалить выбранную опорную точку?', parent=self):
            return
        del self._current_anchors()[self.geo_selected]
        self.geo_selected = self.geo_pending = None
        self._geo_fill()
        self._geo_refresh()
        self.dirty = True
        self.set_status('Привязка удалена. Сохрани .hzmap, чтобы закрепить изменение.')

    def _geo_select(self, index):
        if index is not None and index == self.geo_selected:
            return True
        if not self._geo_commit():
            self._geo_refresh()
            return False
        self.geo_selected = index
        self.geo_pending = None
        self._geo_fill(index)
        self._geo_refresh()
        return True

    def _geo_from_list(self, _event):
        if not self._syncing_geo and self.geo_list.curselection():
            index = self.geo_list.curselection()[0]
            if self._geo_select(index):
                anchor = self._current_anchors()[index]
                self.canvas.xview_moveto(max(0, (anchor['x'] * self.scale -
                    self.canvas.winfo_width() / 2) / (self.project.width * self.scale)))
                self.canvas.yview_moveto(max(0, (anchor['y'] * self.scale -
                    self.canvas.winfo_height() / 2) / (self.project.height * self.scale)))

    def _draw_points(self):
        self.canvas.delete('geo')
        super()._draw_points()
        if not self.geo_mode or not self.project or not self.tabs:
            return
        self.canvas.delete('points')
        for i, a in enumerate(self._current_anchors()):
            x, y = a['x'] * self.scale, a['y'] * self.scale
            r = 11 if i == self.geo_selected else 8
            self.canvas.create_polygon(x, y-r, x+r, y, x, y+r, x-r, y,
                                       fill='#09b80f', outline='white' if i == self.geo_selected else '#101c10',
                                       width=2, tags='geo')
            self.canvas.create_text(x, y, text=str(i + 1), fill='black',
                                    font=('Arial', 9, 'bold'), tags='geo')
        if self.geo_pending:
            x, y = (v * self.scale for v in self.geo_pending)
            self.canvas.create_line(x-11, y, x+11, y, fill='#09b80f', width=2, tags='geo')
            self.canvas.create_line(x, y-11, x, y+11, fill='#09b80f', width=2, tags='geo')

    def press(self, event):
        if not self.geo_mode:
            return super().press(event)
        if self.busy:
            return
        xy = self._map_pos(event)
        if xy is None:
            return
        radius = max(12, round(14 / self.scale))
        hits = [((a['x'] - xy[0]) ** 2 + (a['y'] - xy[1]) ** 2, i)
                for i, a in enumerate(self._current_anchors())]
        index = min((p for p in hits if p[0] <= radius ** 2), default=(0, None))[1]
        if self._geo_select(index):
            if index is None:
                self.geo_pending = xy
                self._geo_fill()
                self._draw_points()
                self.set_status(f'Опора: пиксель X={xy[0]}, Y={xy[1]}. Введи координаты и сохрани.')

    def motion(self, event):
        if not self.geo_mode:
            super().motion(event)

    def release(self, event):
        if not self.geo_mode:
            super().release(event)

    def double_click(self, event):
        if not self.geo_mode:
            return super().double_click(event)
        self.press(event)
        self.geo_lat_entry.focus_set()

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
        self._geo_refresh()

    def _set_all(self, project, path=None):
        self.tabs = project.tabs
        self.anchors = project.anchors
        self.geo_selected = self.geo_pending = None
        self.next_id = project.next_id
        self.active = 0
        self._geo_fill()
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
        self.geo_selected = self.geo_pending = None
        self._geo_fill()
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

    def _discard_ok(self):
        return (not self.project or self._commit_form()) and super()._discard_ok()

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
        if self.busy or (self.project and not self._commit_form()) or (not self.tabs and not self._discard_ok()):
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
                                       'Заменить карту, стартовые точки и геопривязки ТЕКУЩЕЙ вкладки? Остальные вкладки останутся.', parent=self):
                return
            id_, title, _ = self.tabs[self.active]
            self.tabs[self.active] = (id_, title, page)
            self.anchors[id_] = []
            self.geo_selected = self.geo_pending = None
            self._geo_fill()
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
        id_, title, _ = self.tabs[self.active]
        if not messagebox.askyesno('Удаление этажа', f'Удалить «{title}», его точки и геопривязки? При отправке проекта '
                                   'свои и принятые точки этой вкладки на ПДА тоже будут удалены. '
                                   'Чаты и точки остальных вкладок останутся. Продолжить?', parent=self):
            return
        self.tabs.pop(self.active)
        self.anchors.pop(id_, None)
        self.geo_selected = self.geo_pending = None
        self.active = min(self.active, len(self.tabs) - 1)
        self._geo_fill()
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
            self.geo_selected = self.geo_pending = None
            self._geo_fill()
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
            multi_map.MultiMapProject(self.tabs, self.next_id, self.anchors).save(path)
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
            project = multi_map.MultiMapProject(self.tabs, self.next_id, self.anchors)
        except ValueError as exc:
            messagebox.showerror('Проект', str(exc), parent=self)
            return
        if not messagebox.askyesno('Запись карт',
                'Геопривязки остаются только в .hzmap и НЕ передаются на ПДА. Нужна прошивка 1.12.0.ZI или новее на ПДА. Все вкладки и стартовые точки будут заменены. '
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
        if self.project and not self.busy and not self._commit_form():
            return
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
