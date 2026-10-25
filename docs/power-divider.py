# -*- coding: utf-8 -*-
"""Принципиальная схема: батарея + тумблер + делитель замера -> J15.
Генератор картинки для docs/POWER_SUPPLY.md. Запуск: python3 docs/power-divider.py"""
import schemdraw
import schemdraw.elements as elm

schemdraw.use('matplotlib')

d = schemdraw.Drawing(fontsize=11)


def T(x, y, s, fs=11, **k):
    if 'ha' in k:
        k['halign'] = k.pop('ha')
    d.add(elm.Label().at((x, y)).label(s, fontsize=fs, **k))


def W(a, b, c='black', w=None):
    d.add(elm.Line().at(a).to(b).color(c) if w is None
          else elm.Line().at(a).to(b).color(c).linewidth(w))


def box(x1, y1, x2, y2, fill='#F2F2F2', **k):
    # x1<x2, y1<y2; углы Rect относительные -> ставим через .at()
    # .right() обязателен: Rect наследует theta предыдущего элемента!
    d.add(elm.Rect(corner1=(0, 0), corner2=(x2 - x1, y1 - y2),
                   fill=fill, **k).at((x1, y2)).right())


RED, BLK, GRN = '#CC0000', 'black', '#007700'

# --- Заголовок ---
T(5.6, 6.95, 'Питание ПДА «Горизонт»: батарея, тумблер, делитель замера', 16, ha='center')

# --- Батарея ---
d.add(elm.Battery().at((0, 0)).to((0, 5)))
T(0.32, 4.68, '+', 14, color=RED)
T(0.32, 0.10, '−', 14)
T(0, 5.62, 'БАТАРЕЯ\n1S Li-ion', 11, ha='center')

# --- Тумблер ---
W((0, 5), (1.2, 5), RED)
d.add(elm.Switch().at((1.2, 5)).right().length(1.6))
W((2.8, 5), (3.4, 5), RED)
T(2.0, 5.62, 'тумблер', 11, ha='center')

# --- Клемма A ---
box(3.4, 4.55, 4.6, 5.45)
T(4.0, 5.0, 'A\n221-413', 10, ha='center')
W((4.6, 5), (10.0, 5), RED)          # красный к J3+
W((4.0, 4.55), (4.0, 4.4), RED)      # вниз к B
T(7.2, 5.28, 'красный провод', 9, ha='center', color=RED)

# --- Клемма B ---
box(3.4, 3.6, 4.6, 4.4)
T(4.0, 4.0, 'B\n221-412', 10, ha='center')

# --- Делитель ---
d.add(elm.ResistorIEC().at((4.0, 3.6)).to((4.0, 2.65)))
T(3.35, 3.12, 'R1 · 100к', 10, ha='right')
box(3.4, 1.95, 4.6, 2.65)
T(4.0, 2.3, 'C\n221-413', 10, ha='center')
d.add(elm.ResistorIEC().at((4.0, 1.95)).to((4.0, 1.05)))
T(4.8, 1.72, 'R2 · 100к', 10, ha='left')
box(3.4, 0.3, 4.6, 1.0)
T(4.0, 0.65, 'D\n221-413', 10, ha='center')

# --- Конденсатор 100н (опция, пунктир) ---
W((3.4, 1.95), (3.05, 1.95), BLK)
d.add(elm.Capacitor2().at((3.05, 1.95)).to((3.05, 1.05)))
W((3.05, 1.05), (3.4, 1.05), BLK)
d.add(elm.Dot().at((3.05, 1.95)))
d.add(elm.Dot().at((3.05, 1.05)))
T(2.85, 1.5, '100 нФ\n(по желанию)', 10, ha='right')

# --- Замер к J15 ---
W((4.6, 2.3), (6.6, 2.3), GRN)
T(5.6, 2.62, 'замер = VBAT / 2', 10, ha='center', color=GRN)

# --- Чёрный провод (земля) ---
W((0, 0), (9.8, 0), BLK)
W((4.0, 0.3), (4.0, 0), BLK)
d.add(elm.Dot().at((4.0, 0)))
d.add(elm.Dot().at((9.8, 0)))
T(2.0, 0.32, 'чёрный провод', 9, ha='center')

# --- J15 ---
box(6.6, 0.2, 9.6, 3.6, fill='white')
T(8.1, 3.9, 'J15 · HY2.0-4P', 11, ha='center')
for y in (3.1, 1.5, 0.7):
    W((6.6, y), (6.25, y), BLK)
    d.add(elm.Dot(open=True).at((6.25, y)))
T(6.85, 3.1, '1 · GPIO18 · н/п', 10, ha='left')
T(6.85, 2.3, '2 · GPIO17 · замер', 10, ha='left', color=GRN)
T(6.85, 1.5, '3 · 3V3 · НЕ ТРОГАТЬ!', 10, ha='left', color=RED)
T(6.85, 0.7, '4 · GND · н/п', 10, ha='left')

# --- J3 ---
box(10.0, 4.0, 11.6, 5.6, fill='white')
T(10.8, 5.85, 'J3 · PH2.0-2P', 11, ha='center')
T(10.15, 5.0, '+ VBAT', 11, ha='left')
T(10.15, 4.4, '− GND', 11, ha='left')
W((10.0, 4.4), (9.8, 4.4), BLK)
W((9.8, 4.4), (9.8, 0), BLK)

# --- Рамка панели ---
box(6.0, -0.35, 12.2, 6.15, fill=None, ls='--', lw=1.2)
T(9.1, -0.62, 'ПАНЕЛЬ CrowPanel — ничего не паяем, только втыкаем', 10, ha='center')

# --- Подвал ---
T(4.5, -1.15, 'Замер = VBAT / 2   →   4.2 В = 2.10 В · 4.0 В = 2.00 В · 3.7 В = 1.85 В · 3.3 В = 1.65 В\n'
              'Утечка делителя 21 мкА (только при ON) · Отвод после тумблера: при OFF на GPIO17 — 0 В',
  10.5, ha='center')

import os
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'power-divider.png')
d.save(OUT, transparent=False, dpi=200)
print('saved', OUT)
