# -*- coding: utf-8 -*-
"""Питание + делитель замера батареи -> J15 (вариант с пайкой).
Генератор картинки для docs/POWER_SUPPLY.md. Запуск: python3 docs/power-divider.py
Нужны: pip install schemdraw matplotlib"""
import os
import schemdraw
import schemdraw.elements as elm

schemdraw.use('matplotlib')
d = schemdraw.Drawing(fontsize=11)


def T(x, y, s, fs=11, **k):
    if 'ha' in k:
        k['halign'] = k.pop('ha')
    d.add(elm.Label().at((x, y)).label(s, fontsize=fs, **k))


def W(a, b, c='black'):
    d.add(elm.Line().at(a).to(b).color(c))


def box(x1, y1, x2, y2, fill='white', **k):
    # x1<x2, y1<y2; углы Rect относительные + .right() сбрасывает наследованный theta
    d.add(elm.Rect(corner1=(0, 0), corner2=(x2 - x1, y1 - y2),
                   fill=fill, **k).at((x1, y2)).right())


def dot(x, y, c='black'):
    d.add(elm.Dot().at((x, y)).color(c))


RED, GRN = '#CC0000', '#007700'

# --- Заголовок ---
T(5.6, 6.95, 'Питание и замер батареи', 16, ha='center')

# --- Батарея и тумблер ---
d.add(elm.Battery().at((0, 0)).to((0, 5)))
T(0.32, 4.68, '+', 14, color=RED)
T(0.32, 0.10, '−', 14)
T(0, 5.62, '1S Li-ion', 11, ha='center')
W((0, 5), (1.2, 5), RED)
d.add(elm.Switch().at((1.2, 5)).right().length(1.6))
W((2.8, 5), (4.0, 5), RED)
T(2.0, 5.62, 'тумблер', 11, ha='center')

# --- Паяная точка S1: красный + R1 + провод к J3 ---
dot(4.0, 5.0, RED)
W((4.0, 5), (10.0, 5), RED)

# --- Делитель (пайка) ---
d.add(elm.Resistor().at((4.0, 5.0)).to((4.0, 3.0)))
T(3.45, 4.0, 'R1 100к', 11, ha='right')
dot(4.0, 3.0, GRN)  # S2: R1 + R2 + замер
d.add(elm.Resistor().at((4.0, 3.0)).to((4.0, 1.0)))
T(4.55, 2.0, 'R2 100к', 11, ha='left')
W((4.0, 1.0), (4.0, 0))  # вывод R2 к чёрному проводу
dot(4.0, 0)  # S3: R2 + чёрный провод

# --- Конденсатор параллельно R2 ---
W((4.0, 3.0), (3.0, 3.0))
d.add(elm.Capacitor2().at((3.0, 3.0)).to((3.0, 1.0)))
W((3.0, 1.0), (4.0, 1.0))
dot(3.0, 3.0)
dot(3.0, 1.0)
T(2.65, 2.0, '100н\n(опция)', 10, ha='right')

# --- Замер к J15 ---
W((4.0, 3.0), (6.6, 3.0), GRN)
T(5.3, 3.3, 'VBAT/2', 10, ha='center', color=GRN)

# --- Чёрный провод ---
W((0, 0), (9.8, 0))
dot(9.8, 0)

# --- J15 ---
box(6.6, 0.7, 9.6, 4.4)
T(8.1, 4.7, 'J15 · HY2.0', 11, ha='center')
for y in (3.9, 2.1, 1.2):
    W((6.6, y), (6.25, y))
    d.add(elm.Dot(open=True).at((6.25, y)))
T(6.85, 3.9, '1 · GPIO18 · н/п', 10, ha='left')
T(6.85, 3.0, '2 · GPIO17', 10, ha='left', color=GRN)
T(6.85, 2.1, '3 · 3V3', 10, ha='left')
T(6.85, 1.78, 'НЕ ТРОГАТЬ!', 10, ha='left', color=RED)
T(6.85, 1.2, '4 · GND · н/п', 10, ha='left')

# --- J3 ---
box(10.0, 4.0, 11.6, 5.6)
T(10.8, 5.85, 'J3 · PH2.0', 11, ha='center')
T(10.15, 5.0, '+ VBAT', 11, ha='left')
T(10.15, 4.4, '− GND', 11, ha='left')
W((10.0, 4.4), (9.8, 4.4))
W((9.8, 4.4), (9.8, 0))

# --- Рамка панели ---
box(6.0, -0.35, 12.2, 6.15, fill=None, ls='--', lw=1.2)
T(9.1, -0.62, 'панель — не паяем!', 10, ha='center')

# --- Подвал ---
T(4.6, -1.2, 'Замер = VBAT / 2:   4.2 В → 2.10 В · 3.7 В → 1.85 В · 3.3 В → 1.65 В',
  10.5, ha='center')

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'power-divider.png')
d.save(OUT, transparent=False, dpi=200)
print('saved', OUT)
