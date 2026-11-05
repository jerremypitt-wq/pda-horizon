#!/usr/bin/env python3
# ПДА «Горизонт» — конвертер логотипа заставки.
# assets/horizon-logo.png (тёмный лайн-арт на белом) -> альфа-маска A8 ->
# src/horizon/HorizonLogo.h + src/horizon/HorizonLogo.c (lv_image_dsc_t, LVGL v9).
# Фона у картинки НЕТ (только контур вышки): на заставке маска красится
# в акцент текущей темы через image_recolor (см. HorizonUI::showBoot).
# Перегенерация:  python3 bin/make_logo.py
import os
from PIL import Image

SRC = os.path.join(os.path.dirname(__file__), '..', 'assets', 'horizon-logo.png')
DST_C = os.path.join(os.path.dirname(__file__), '..', 'src', 'horizon', 'HorizonLogo.c')
DST_H = os.path.join(os.path.dirname(__file__), '..', 'src', 'horizon', 'HorizonLogo.h')
TARGET_H = 132   # высота лого на заставке 480x320/320x480 («не сильно большим»)
MARGIN = 24      # поля вокруг контента, px исходника
GAIN = 1.35      # усиление тонких линий после даунскейла

img = Image.open(SRC).convert('RGBA')
# Своя прозрачность у PNG есть - берём её; иначе маска из яркости
# (тёмные пиксели - контур, белый фон - дыра).
if img.getchannel('A').getextrema()[0] < 250:
    work = img.getchannel('A')
    mask = work.point(lambda v: 255 if v > 128 else 0)
    invert = False
else:
    work = img.convert('L')
    mask = work.point(lambda v: 255 if v < 128 else 0)
    invert = True
l, t, r, b = mask.getbbox()
l = max(0, l - MARGIN); t = max(0, t - MARGIN)
r = min(img.width, r + MARGIN); b = min(img.height, b + MARGIN)
work = work.crop((l, t, r, b))
w = max(1, round(work.width * TARGET_H / work.height))
work = work.resize((w, TARGET_H), Image.LANCZOS)
px = bytearray(work.tobytes())
for i in range(len(px)):
    v = 255 - px[i] if invert else px[i]
    px[i] = min(255, int(v * GAIN))  # усиление тонких линий
img_out = Image.frombytes('L', work.size, bytes(px))
img_out.save('/tmp/hz_logo_preview.png')

w, h = img_out.size
raw = bytes(px)  # A8: один байт альфы на пиксель
assert len(raw) == w * h

with open(DST_H, 'w', encoding='utf-8') as f:
    f.write('''#pragma once
// Логотип заставки ПДА «Горизонт» (сгенерировано bin/make_logo.py, не править).
#ifdef HORIZON_UI
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
extern const lv_image_dsc_t hzLogo;
#ifdef __cplusplus
}
#endif
#endif // HORIZON_UI
''')

with open(DST_C, 'w', encoding='utf-8') as f:
    f.write('// Логотип заставки ПДА «Горизонт» (%dx%d A8 альфа-маска, bin/make_logo.py).\n' % (w, h))
    f.write('#ifdef HORIZON_UI\n\n#include "horizon/HorizonLogo.h"\n\n')
    f.write('LV_ATTRIBUTE_MEM_ALIGN const uint8_t hzLogo_map[] = {\n')
    for i in range(0, len(raw), 12):
        f.write('    ' + ', '.join('0x%02X' % b for b in raw[i:i+12]) + ',\n')
    f.write('};\n\nconst lv_image_dsc_t hzLogo = {\n')
    f.write('    .header.magic = LV_IMAGE_HEADER_MAGIC,\n')
    f.write('    .header.cf = LV_COLOR_FORMAT_A8,\n')
    f.write('    .header.flags = 0,\n')
    f.write('    .header.w = %d,\n    .header.h = %d,\n    .header.stride = %d,\n' % (w, h, w))
    f.write('    .data_size = %d,\n    .data = hzLogo_map,\n};\n\n#endif // HORIZON_UI\n' % len(raw))
print('logo %dx%d (%d bytes A8) -> %s' % (w, h, len(raw), DST_C))
