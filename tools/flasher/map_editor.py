#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Редактор общей карты и стартовых точек внутри HorizonFlasher.exe."""
import io
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from PIL import Image, ImageTk

import editor_data


class MapEditor(tk.Toplevel):
    """Один JPG, максимум 50 общих стартовых точек; чаты/личные точки не трогает."""

    def __init__(self, parent, on_upload):
        super().__init__(parent)
        self.title("Горизонт — редактор карты и стартовых точек")
        width = max(900, min(1180, self.winfo_screenwidth() - 60))
        height = max(620, min(790, self.winfo_screenheight() - 100))
        self.geometry(f"{width}x{height}")
        self.minsize(900, 620)
        self.project = None
        self.project_path = None
        self.dirty = False
        self.busy = False
        self.on_upload = on_upload
        self.scale = 0.75
        self.photo = None
        self.source = None
        self.selected = None
        self.pending = None
        self.drag_index = None
        self.dragged = False
        self._syncing_list = False
        self._build()
        self.protocol("WM_DELETE_WINDOW", self.close_editor)

    def _build(self):
        bar = ttk.Frame(self, padding=8)
        bar.pack(fill=tk.X)
        ttk.Button(bar, text="Открыть JPG", command=self.open_jpg).pack(side=tk.LEFT, padx=3)
        ttk.Button(bar, text="Открыть проект", command=self.open_project).pack(side=tk.LEFT, padx=3)
        ttk.Button(bar, text="Сохранить проект", command=self.save_project).pack(side=tk.LEFT, padx=3)
        ttk.Button(bar, text="−", width=3, command=lambda: self.zoom(-1)).pack(side=tk.LEFT, padx=8)
        ttk.Button(bar, text="+", width=3, command=lambda: self.zoom(1)).pack(side=tk.LEFT)
        self.upload_btn = ttk.Button(bar, text="ЗАЛИТЬ КАРТУ И ТОЧКИ НА ПДА", command=self.upload)
        self.upload_btn.pack(side=tk.RIGHT, padx=3)

        ttk.Label(self, text="Левая кнопка: выбрать точку; пустая карта: место новой точки. "
                  "Двойной щелчок: перейти к её заполнению. Масштаб 0,5 — размер экрана ПДА. Перетяни точку, чтобы изменить место; "
                  "правая кнопка: двигать карту. Стартовые точки — только общие.",
                  wraplength=1050).pack(fill=tk.X, padx=12)
        main = ttk.Frame(self, padding=8)
        main.pack(fill=tk.BOTH, expand=True)
        main.columnconfigure(0, weight=4)
        main.columnconfigure(1, weight=2)
        main.rowconfigure(0, weight=1)

        frame = ttk.Frame(main)
        frame.grid(row=0, column=0, sticky="nsew", padx=(0, 8))
        frame.rowconfigure(0, weight=1)
        frame.columnconfigure(0, weight=1)
        self.canvas = tk.Canvas(frame, bg="#252525", highlightthickness=0, cursor="crosshair")
        self.canvas.grid(row=0, column=0, sticky="nsew")
        sx = ttk.Scrollbar(frame, orient=tk.HORIZONTAL, command=self.canvas.xview)
        sy = ttk.Scrollbar(frame, orient=tk.VERTICAL, command=self.canvas.yview)
        self.canvas.configure(xscrollcommand=sx.set, yscrollcommand=sy.set)
        sx.grid(row=1, column=0, sticky="ew")
        sy.grid(row=0, column=1, sticky="ns")
        self.canvas.bind("<ButtonPress-1>", self.press)
        self.canvas.bind("<B1-Motion>", self.motion)
        self.canvas.bind("<ButtonRelease-1>", self.release)
        self.canvas.bind("<Double-Button-1>", self.double_click)
        self.canvas.bind("<ButtonPress-3>", lambda e: self.canvas.scan_mark(e.x, e.y))
        self.canvas.bind("<B3-Motion>", lambda e: self.canvas.scan_dragto(e.x, e.y, gain=1))
        self.canvas.bind("<Control-MouseWheel>", lambda e: self.zoom(1 if e.delta > 0 else -1))

        right = ttk.Frame(main)
        right.grid(row=0, column=1, sticky="nsew")
        right.columnconfigure(0, weight=1)
        self.count_label = ttk.Label(right, text="Стартовые точки (0/50)")
        self.count_label.grid(row=0, column=0, sticky="w")
        self.listbox = tk.Listbox(right, height=9, exportselection=False)
        self.listbox.grid(row=1, column=0, sticky="nsew", pady=(3, 12))
        right.rowconfigure(1, weight=1)
        self.listbox.bind("<<ListboxSelect>>", self.from_list)

        form = ttk.LabelFrame(right, text="Карточка стартовой точки", padding=9)
        form.grid(row=2, column=0, sticky="ew")
        form.columnconfigure(1, weight=1)
        self.name = tk.StringVar()
        self.sender = tk.StringVar(value="Мастер")
        self.x = tk.StringVar(value="0")
        self.y = tk.StringVar(value="0")
        self.type_name = tk.StringVar(value=editor_data.poi_pack.TYPES[0])
        for row, (caption, var) in enumerate((("Название", self.name), ("Тип", self.type_name),
                                              ("Автор", self.sender), ("X (0–959)", self.x),
                                              ("Y (0–639)", self.y))):
            ttk.Label(form, text=caption).grid(row=row, column=0, sticky="w", pady=3)
            if caption == "Тип":
                widget = ttk.Combobox(form, textvariable=var, state="readonly",
                                       values=editor_data.poi_pack.TYPES)
            else:
                widget = ttk.Entry(form, textvariable=var)
            widget.grid(row=row, column=1, sticky="ew", padx=(8, 0), pady=3)
            if caption == "Название":
                self.name_entry = widget
        ttk.Label(form, text="Комментарий (до 150 символов)").grid(row=5, column=0,
                                                                      columnspan=2, sticky="w")
        self.comment = tk.Text(form, width=24, height=4, wrap=tk.WORD)
        self.comment.grid(row=6, column=0, columnspan=2, sticky="ew")
        buttons = ttk.Frame(form)
        buttons.grid(row=7, column=0, columnspan=2, sticky="ew", pady=8)
        self.apply_btn = ttk.Button(buttons, text="Добавить / сохранить точку", command=self.apply)
        self.apply_btn.pack(side=tk.LEFT, padx=2)
        ttk.Button(buttons, text="Удалить точку", command=self.delete).pack(side=tk.LEFT, padx=2)
        self.status = tk.StringVar(value="Открой JPG 960×640 или сохранённый проект.")
        ttk.Label(self, textvariable=self.status, wraplength=1050).pack(fill=tk.X, padx=12, pady=6)

    def set_status(self, text):
        self.status.set(text)

    def set_busy(self, busy):
        self.busy = busy
        self.upload_btn.config(state=tk.DISABLED if busy else tk.NORMAL)

    def _discard_ok(self):
        return not self.dirty or messagebox.askyesno("Несохранённый проект",
                                                    "Несохранённые изменения пропадут. Продолжить?", parent=self)

    def _set_project(self, project, path=None):
        self.project = project
        self.project_path = path
        self.dirty = path is None
        self.selected = None
        self.pending = None
        self.source = Image.open(io.BytesIO(project.image)).convert("RGB")
        self._fill_form(None)
        self._redraw_image()
        self._refresh_list()
        self.set_status(f"Карта 960×640 открыта. Точек: {len(project.points)}. "
                        "Щёлкни по пустому месту и заполни карточку справа.")

    def open_jpg(self):
        if self.busy or not self._discard_ok():
            return
        path = filedialog.askopenfilename(parent=self, title="Открой карту 960×640",
                                          filetypes=[("JPEG", "*.jpg *.jpeg"), ("Все файлы", "*.*")])
        if path:
            try:
                self._set_project(editor_data.MapProject.from_jpg(path))
            except (OSError, ValueError) as e:
                messagebox.showerror("Карта", str(e), parent=self)

    def open_project(self):
        if self.busy or not self._discard_ok():
            return
        path = filedialog.askopenfilename(parent=self, title="Открой проект карты",
                                          filetypes=[("Проект Горизонта", "*.hzmap"), ("Все файлы", "*.*")])
        if path:
            try:
                self._set_project(editor_data.MapProject.load(path), path)
            except (OSError, ValueError) as e:
                messagebox.showerror("Проект", str(e), parent=self)

    def save_project(self):
        if not self.project or not self._commit_form():
            return False
        path = self.project_path or filedialog.asksaveasfilename(
            parent=self, title="Сохранить редактируемый проект", defaultextension=".hzmap",
            filetypes=[("Проект Горизонта", "*.hzmap")])
        if not path:
            return False
        try:
            self.project.save(path)
            self.project_path = path
            self.dirty = False
            self.set_status(f"Проект сохранён: {path}")
            return True
        except OSError as e:
            messagebox.showerror("Проект", f"Не удалось сохранить: {e}", parent=self)
            return False

    def _redraw_image(self):
        self.canvas.delete("all")
        if self.source is None:
            return
        w, h = round(editor_data.WIDTH * self.scale), round(editor_data.HEIGHT * self.scale)
        resized = self.source.resize((w, h), Image.Resampling.LANCZOS)
        self.photo = ImageTk.PhotoImage(resized, master=self.canvas)
        self.canvas.create_image(0, 0, image=self.photo, anchor="nw")
        self.canvas.configure(scrollregion=(0, 0, w, h))
        self._draw_points()

    def _draw_points(self):
        self.canvas.delete("points")
        if not self.project:
            return
        for index, p in enumerate(self.project.points):
            sx, sy = p["x"] * self.scale, p["y"] * self.scale
            r = 9 if index == self.selected else 7
            self.canvas.create_oval(sx-r, sy-r, sx+r, sy+r,
                                    fill=editor_data.COLORS[p["type"]],
                                    outline="white" if index == self.selected else "black",
                                    width=3 if index == self.selected else 2, tags="points")
        if self.selected is not None:
            p = self.project.points[self.selected]
            self.canvas.create_text(p["x"] * self.scale, p["y"] * self.scale - 17,
                                    text=p["name"], anchor="s", fill="white",
                                    font=("Arial", 11, "bold"), tags="points")
        if self.pending is not None:
            sx, sy = (n * self.scale for n in self.pending)
            self.canvas.create_line(sx-9, sy, sx+9, sy, fill="white", width=2, tags="points")
            self.canvas.create_line(sx, sy-9, sx, sy+9, fill="white", width=2, tags="points")

    def _refresh_list(self):
        self._syncing_list = True
        self.listbox.delete(0, tk.END)
        if self.project:
            for i, p in enumerate(self.project.points, 1):
                self.listbox.insert(tk.END, f"B{i:03d}  {p['name']}  ({p['x']}, {p['y']})")
            if self.selected is not None:
                self.listbox.selection_set(self.selected)
                self.listbox.see(self.selected)
        self.count_label.config(text=f"Стартовые точки ({len(self.project.points) if self.project else 0}/50)")
        self._syncing_list = False
        self._draw_points()

    def _fill_form(self, index):
        p = self.project.points[index] if index is not None and self.project else None
        self.name.set(p["name"] if p else "")
        self.type_name.set(editor_data.poi_pack.TYPES[p["type"]] if p else editor_data.poi_pack.TYPES[0])
        self.sender.set(p["sender"] if p else "Мастер")
        self.x.set(str(p["x"] if p else (self.pending[0] if self.pending else 0)))
        self.y.set(str(p["y"] if p else (self.pending[1] if self.pending else 0)))
        self.comment.delete("1.0", tk.END)
        if p:
            self.comment.insert("1.0", p["comment"])

    def _form_point(self):
        try:
            x, y = int(self.x.get()), int(self.y.get())
            typ = editor_data.poi_pack.TYPES.index(self.type_name.get())
        except (ValueError, TypeError) as e:
            raise ValueError("координаты должны быть целыми числами, а тип — выбран из списка") from e
        return editor_data.validate_point({"x": x, "y": y, "type": typ,
                                           "name": self.name.get().strip(),
                                           "sender": self.sender.get().strip(),
                                           "comment": self.comment.get("1.0", "end-1c").strip()})

    def _commit_form(self, explicit=False):
        if not self.project:
            return False
        if self.selected is None and not self.name.get().strip() and not explicit:
            return True  # пустая карточка новой точки: нечего сохранять
        try:
            point = self._form_point()
            if self.selected is None:
                self.project.add(point)
                self.selected = len(self.project.points) - 1
                self.pending = None
            elif point != self.project.points[self.selected]:
                self.project.replace(self.selected, point)
            else:
                return True
            self.dirty = True
            self._refresh_list()
            self.set_status(f"Точка B{self.selected+1:03d} сохранена. Проект можно сохранить или залить на ПДА.")
            return True
        except ValueError as e:
            messagebox.showerror("Точка", str(e), parent=self)
            return False

    def apply(self):
        self._commit_form(explicit=True)

    def delete(self):
        if not self.project or self.selected is None or self.busy:
            return
        if not messagebox.askyesno("Удалить стартовую точку", "Удалить выбранную точку из проекта?",
                                   parent=self):
            return
        del self.project.points[self.selected]
        self.selected = None
        self.pending = None
        self.dirty = True
        self._fill_form(None)
        self._refresh_list()

    def _select(self, index):
        if index == self.selected:
            return True
        if not self._commit_form():
            self._refresh_list()  # вернуть подсветку при ошибке в форме
            return False
        self.selected = index
        self.pending = None
        self._fill_form(index)
        self._refresh_list()
        return True

    def from_list(self, _event):
        if self._syncing_list or not self.project or not self.listbox.curselection():
            return
        self._select(self.listbox.curselection()[0])

    def _map_pos(self, event):
        if not self.project:
            return None
        px = round(self.canvas.canvasx(event.x) / self.scale)
        py = round(self.canvas.canvasy(event.y) / self.scale)
        if 0 <= px < editor_data.WIDTH and 0 <= py < editor_data.HEIGHT:
            return px, py
        return None

    def _hit(self, xy):
        if not xy or not self.project:
            return None
        radius = max(12, round(14 / self.scale))
        near = [(abs(p["x"] - xy[0]) ** 2 + abs(p["y"] - xy[1]) ** 2, i)
                for i, p in enumerate(self.project.points)]
        return min((pair for pair in near if pair[0] <= radius * radius), default=(0, None))[1]

    def press(self, event):
        if self.busy:
            return
        xy = self._map_pos(event)
        if not xy:
            return
        index = self._hit(xy)
        self.drag_index = None
        self.dragged = False
        if index is not None:
            if self._select(index):
                self.drag_index = index
        elif self._select(None):
            self.pending = xy
            self._fill_form(None)
            self._refresh_list()
            self.set_status(f"Место: x{xy[0]}, y{xy[1]}. Заполни карточку и нажми «Добавить / сохранить». ")

    def motion(self, event):
        if self.busy or self.drag_index is None or not self.project:
            return
        xy = self._map_pos(event)
        if not xy:
            return
        p = self.project.points[self.drag_index]
        if (p["x"], p["y"]) != xy:
            p["x"], p["y"] = xy
            self.x.set(str(xy[0]))
            self.y.set(str(xy[1]))
            self.dragged = True
            self.dirty = True
            self._draw_points()

    def release(self, _event):
        if self.dragged:
            self._refresh_list()
        self.drag_index = None
        self.dragged = False

    def double_click(self, event):
        if self.busy:
            return
        xy = self._map_pos(event)
        if xy and self._hit(xy) is None and self._select(None):
            self.pending = xy
            self._fill_form(None)
            self._refresh_list()
            self.name_entry.focus_set()

    def zoom(self, direction):
        steps = (0.5, 0.75, 1.0, 1.5, 2.0)
        i = min(range(len(steps)), key=lambda idx: abs(steps[idx] - self.scale))
        nxt = max(0, min(len(steps)-1, i + direction))
        if nxt == i or not self.project:
            return
        cx = (self.canvas.canvasx(0) + self.canvas.winfo_width() / 2) / self.scale
        cy = (self.canvas.canvasy(0) + self.canvas.winfo_height() / 2) / self.scale
        self.scale = steps[nxt]
        self._redraw_image()
        full_w, full_h = editor_data.WIDTH * self.scale, editor_data.HEIGHT * self.scale
        self.canvas.xview_moveto(max(0, (cx * self.scale - self.canvas.winfo_width()/2) / full_w))
        self.canvas.yview_moveto(max(0, (cy * self.scale - self.canvas.winfo_height()/2) / full_h))

    def upload(self):
        if self.busy or not self.project or not self._commit_form():
            return
        if not messagebox.askyesno("Отправить на ПДА",
                                   "Записать карту и ВСЕ стартовые точки из этого проекта на ПДА? "
                                   "Чаты, свои и принятые точки останутся. Для карты нужна прошивка 1.8.5.ZI или новее. "
                                   "Перед записью программа сохранит резервную копию.", parent=self):
            return
        self.on_upload(self.project.snapshot())

    def close_editor(self):
        if self.busy:
            messagebox.showwarning("Идёт запись", "Дождись окончания записи на ПДА.", parent=self)
            return
        if self._discard_ok():
            self.destroy()
