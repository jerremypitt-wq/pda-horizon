#!/usr/bin/env python3
# ПДА «Горизонт» — конвертер логотипа заставки.
# assets/horizon-logo.png (тёмный лайн-арт на белом) -> инверсия -> RGB565 ->
# src/horizon/HorizonLogo.h + src/horizon/HorizonLogo.c (lv_image_dsc_t, LVGL v9).
# Перегенерация:  python3 bin/make_logo.py
import os
from PIL import Image

SRC = os.path.join(os.path.dirname(__file__), '..', 'assets', 'horizon-logo.png')
DST_C = os.path.join(os.path.dirname(__file__), '..', 'src', 'horizon', 'HorizonLogo.c')
DST_H = os.path.join(os.path.dirname(__file__), '..', 'src', 'horizon', 'HorizonLogo.h')
TARGET_H = 132   # высота лого на заставке 480x320/320x480 («не сильно большим»)
MARGIN = 24      # поля вокруг контента, px исходника
GAIN = 1.35      # усиление тонких линий после даунскейла

img = Image.open(SRC).convert('L')
mask = img.point(lambda v: 255 if v < 128 else 0)
l, t, r, b = mask.getbbox()
l = max(0, l - MARGIN); t = max(0, t - MARGIN)
r = min(img.width, r + MARGIN); b = min(img.height, b + MARGIN)
img = img.crop((l, t, r, b))
w = max(1, round(img.width * TARGET_H / img.height))
img = img.resize((w, TARGET_H), Image.LANCZOS)
px = bytearray(img.tobytes())
for i in range(len(px)):
    px[i] = min(255, int((255 - px[i]) * GAIN))  # инверсия в светлое на чёрном
img_out = Image.frombytes('L', img.size, bytes(px))
img_out.save('/tmp/hz_logo_preview.png')

w, h = img_out.size
raw = bytearray()
for v in px:
    r5, g6, b5 = (v >> 3) & 31, (v >> 2) & 63, (v >> 3) & 31
    rgb565 = (r5 << 11) | (g6 << 5) | b5
    raw += bytes((rgb565 & 0xFF, rgb565 >> 8))
assert len(raw) == w * h * 2

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
    f.write('// Логотип заставки ПДА «Горизонт» (%dx%d RGB565, bin/make_logo.py).\n' % (w, h))
    f.write('#ifdef HORIZON_UI\n\n#include "horizon/HorizonLogo.h"\n\n')
    f.write('LV_ATTRIBUTE_MEM_ALIGN const uint8_t hzLogo_map[] = {\n')
    for i in range(0, len(raw), 12):
        f.write('    ' + ', '.join('0x%02X' % b for b in raw[i:i+12]) + ',\n')
    f.write('};\n\nconst lv_image_dsc_t hzLogo = {\n')
    f.write('    .header.magic = LV_IMAGE_HEADER_MAGIC,\n')
    f.write('    .header.cf = LV_COLOR_FORMAT_RGB565,\n')
    f.write('    .header.flags = 0,\n')
    f.write('    .header.w = %d,\n    .header.h = %d,\n    .header.stride = %d,\n' % (w, h, w * 2))
    f.write('    .data_size = %d,\n    .data = hzLogo_map,\n};\n\n#endif // HORIZON_UI\n' % len(raw))
print('logo %dx%d (%d bytes RGB565) -> %s' % (w, h, len(raw), DST_C))
