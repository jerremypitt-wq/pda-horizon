#pragma once
#include "horizon_target.h" // целевые флаги сборки (вкус: standalone/прошивка)
// ПДА «Горизонт» - цветовые темы в духе S.T.A.L.K.E.R. и хелперы стилей LVGL.

#ifdef HORIZON_UI

#if defined(ARCH_ESP32) || defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h> // раньше lvgl.h: IRAM_ATTR для LV_ATTRIBUTE_FAST_MEM
#endif

#include "lvgl.h"
#include <stdint.h>

// Шрифты device-ui с кириллицей (generated/ui_320x240)
#include "fonts.h"

struct HorizonPalette {
    uint32_t bg;          // фон экрана
    uint32_t panel;       // фон панелей
    uint32_t panel2;      // фон вложенных панелей/полей ввода
    uint32_t border;      // рамки
    uint32_t accent;      // акцент (заголовки, кнопки, бейджи)
    uint32_t accentText;  // текст на акценте
    uint32_t text;        // основной текст
    uint32_t textDim;     // приглушённый текст
    uint32_t incoming;    // пузыри входящих
    uint32_t outgoing;    // пузыри исходящих
    uint32_t danger;      // опасность/удаление
    uint32_t ok;          // успех/онлайн
    uint32_t headerBg;    // фон верхней панели
    uint32_t tileEnd;     // второй цвет градиента плиток
};

class HorizonTheme
{
  public:
    enum Id : uint8_t {
        STALKER_DARK = 0, // тёмная (по умолчанию)
        LIGHT = 1,        // светлая
        DUTY = 2,         // Долг (красно-чёрная)
        FREEDOM = 3,      // Свобода (жёлто-зелёная)
        LONERS = 4,       // Одиночки (хаки)
    };

    static const HorizonPalette &palette(uint8_t id);
    static const HorizonPalette &current(void);

    // Применить базовый стиль к экрану/панели/кнопке/полю
    static void styleScreen(lv_obj_t *obj);
    static void stylePanel(lv_obj_t *obj);
    static void styleField(lv_obj_t *obj);
    static void styleButton(lv_obj_t *obj, bool accent);
    static void styleDangerButton(lv_obj_t *obj);
    static void styleTile(lv_obj_t *obj);
    static void styleHeader(lv_obj_t *obj);
    static void styleLabel(lv_obj_t *obj, bool dim);
    static void styleList(lv_obj_t *obj);
    static void styleKeyboard(lv_obj_t *obj);

    static const lv_font_t *fontSmall(void) { return &ui_font_montserrat_12; }
    static const lv_font_t *fontNormal(void) { return &ui_font_montserrat_14; }
    static const lv_font_t *fontMedium(void) { return &ui_font_montserrat_16; }
    static const lv_font_t *fontBig(void) { return &ui_font_montserrat_20; }
};

inline lv_color_t hzColor(uint32_t hex)
{
    return lv_color_hex(hex);
}

#endif // HORIZON_UI
