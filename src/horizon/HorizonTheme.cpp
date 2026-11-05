#include "horizon_target.h" // целевые флаги сборки (вкус: standalone/прошивка)
// ПДА «Горизонт» - палитры и стили.
#ifdef HORIZON_UI

#include "horizon/HorizonTheme.h"
#include "horizon/HorizonConfig.h"

namespace {
const HorizonPalette kPalettes[] = {
    // STALKER_DARK - фосфорный янтарь на тёмном (по умолчанию)
    {.bg = 0x0D0F0C,
     .panel = 0x181B16,
     .panel2 = 0x10130E,
     .border = 0x4A4433,
     .accent = 0xD7A021,
     .accentText = 0x14120A,
     .text = 0xE8E0C8,
     .textDim = 0x8A8474,
     .incoming = 0x232A1E,
     .outgoing = 0x4A3A10,
     .danger = 0xC0392B,
     .ok = 0x7DBB4E,
     .headerBg = 0x1E221A,
     .tileEnd = 0x23261E},
    // LIGHT - светлая дневная
    {.bg = 0xD8D2C0,
     .panel = 0xECE7D4,
     .panel2 = 0xF7F3E6,
     .border = 0x8A8474,
     .accent = 0x5A4A1A,
     .accentText = 0xF7F3E6,
     .text = 0x1E1C14,
     .textDim = 0x5A5648,
     .incoming = 0xD5DCC4,
     .outgoing = 0xE8D9A8,
     .danger = 0xA93226,
     .ok = 0x3E7A2A,
     .headerBg = 0xC9C2AB,
     .tileEnd = 0xD5CFBA},
    // DUTY - Долг: красное на чёрном
    {.bg = 0x0E0C0C,
     .panel = 0x1C1512,
     .panel2 = 0x120E0D,
     .border = 0x5A2A22,
     .accent = 0xC0392B,
     .accentText = 0xF5E8E0,
     .text = 0xEAD9CC,
     .textDim = 0x8A6F63,
     .incoming = 0x2A1B18,
     .outgoing = 0x4A231C,
     .danger = 0xE74C3C,
     .ok = 0x7DBB4E,
     .headerBg = 0x241614,
     .tileEnd = 0x2A1A16},
    // FREEDOM - Свобода: жёлто-зелёное
    {.bg = 0x101208,
     .panel = 0x1B1E12,
     .panel2 = 0x14160C,
     .border = 0x5A5A2A,
     .accent = 0xB8C11A,
     .accentText = 0x14160A,
     .text = 0xEDEDC0,
     .textDim = 0x8A8A6A,
     .incoming = 0x222A18,
     .outgoing = 0x3E4216,
     .danger = 0xC0392B,
     .ok = 0x6ABE30,
     .headerBg = 0x222412,
     .tileEnd = 0x262A16},
    // LONERS - Одиночки: тёмно-зелёный хаки
    {.bg = 0x141712,
     .panel = 0x232A1E,
     .panel2 = 0x1A2018,
     .border = 0x5A6248,
     .accent = 0x8A9A5B,
     .accentText = 0x14170F,
     .text = 0xDDE4C8,
     .textDim = 0x8A9274,
     .incoming = 0x2A3324,
     .outgoing = 0x4A4A2A,
     .danger = 0xC0392B,
     .ok = 0x9ACD32,
     .headerBg = 0x2A3324,
     .tileEnd = 0x2E3628},
    // CYBERPUNK - чёрный фон и ровный зелёный #09B80F вместо бирюзового неона.
    {.bg = 0x000000,
     .panel = 0x000000,
     .panel2 = 0x000000,
     .border = 0x08770C,
     .accent = 0x09B80F,
     .accentText = 0x000000,
     .text = 0x09B80F,
     .textDim = 0x07940B,
     .incoming = 0x031704,
     .outgoing = 0x062409,
     .danger = 0xEF5360,
     .ok = 0x09B80F,
     .headerBg = 0x000000,
     .tileEnd = 0x000000},
};
} // namespace

const HorizonPalette &HorizonTheme::palette(uint8_t id)
{
    if (id >= (uint8_t)(sizeof(kPalettes) / sizeof(kPalettes[0])))
        id = 0;
    return kPalettes[id];
}

const HorizonPalette &HorizonTheme::current(void)
{
    return palette(HorizonConfig::instance().theme);
}

void HorizonTheme::styleScreen(lv_obj_t *obj)
{
    const HorizonPalette &p = current();
    lv_obj_set_style_bg_color(obj, hzColor(p.bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, hzColor(p.text), LV_PART_MAIN);
    lv_obj_set_style_text_font(obj, fontNormal(), LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
}

void HorizonTheme::stylePanel(lv_obj_t *obj)
{
    const HorizonPalette &p = current();
    lv_obj_set_style_bg_color(obj, hzColor(p.panel), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, hzColor(p.border), LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 4, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, hzColor(p.text), LV_PART_MAIN);
    lv_obj_set_style_text_font(obj, fontNormal(), LV_PART_MAIN);
}

void HorizonTheme::styleField(lv_obj_t *obj)
{
    const HorizonPalette &p = current();
    lv_obj_set_style_bg_color(obj, hzColor(p.panel2), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, hzColor(p.border), LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 3, LV_PART_MAIN);
    // Однострочные поля и кнопки списков: скроллбару тут не место.
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_text_color(obj, hzColor(p.text), LV_PART_MAIN);
    lv_obj_set_style_text_font(obj, fontNormal(), LV_PART_MAIN);
    // курсор/выделение textarea
    lv_obj_set_style_bg_color(obj, hzColor(p.accent), LV_PART_CURSOR);
    lv_obj_set_style_bg_color(obj, hzColor(p.accent), LV_PART_SELECTED);
    lv_obj_set_style_text_color(obj, hzColor(p.accentText), LV_PART_SELECTED);
}

void HorizonTheme::styleButton(lv_obj_t *obj, bool accent)
{
    const HorizonPalette &p = current();
    if (accent) {
        lv_obj_set_style_bg_color(obj, hzColor(p.accent), LV_PART_MAIN);
        lv_obj_set_style_text_color(obj, hzColor(p.accentText), LV_PART_MAIN);
        lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    } else {
        lv_obj_set_style_bg_color(obj, hzColor(p.panel), LV_PART_MAIN);
        lv_obj_set_style_text_color(obj, hzColor(p.text), LV_PART_MAIN);
        lv_obj_set_style_border_color(obj, hzColor(p.border), LV_PART_MAIN);
        lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    }
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 4, LV_PART_MAIN);
    lv_obj_set_style_text_font(obj, fontNormal(), LV_PART_MAIN);
    // нажатое состояние - инверсия
    lv_style_selector_t pressed = (lv_style_selector_t)LV_PART_MAIN | LV_STATE_PRESSED; // GCC14: enum|enum разных типов
    lv_obj_set_style_bg_color(obj, hzColor(accent ? p.text : p.accent), pressed);
    lv_obj_set_style_text_color(obj, hzColor(accent ? p.panel : p.accentText), pressed);
}

void HorizonTheme::styleDangerButton(lv_obj_t *obj)
{
    const HorizonPalette &p = current();
    lv_obj_set_style_bg_color(obj, hzColor(p.danger), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 4, LV_PART_MAIN);
    lv_obj_set_style_text_font(obj, fontNormal(), LV_PART_MAIN);
}

void HorizonTheme::styleTile(lv_obj_t *obj)
{
    const HorizonPalette &p = current();
    // Плитки однотонные, без градиента.
    lv_obj_set_style_bg_color(obj, hzColor(p.panel), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    // акцентная верхняя кромка «под прибор»
    lv_obj_set_style_border_color(obj, hzColor(p.border), LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(obj, (lv_border_side_t)(LV_BORDER_SIDE_FULL & ~LV_BORDER_SIDE_TOP), LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 6, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, hzColor(p.text), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN);
}

void HorizonTheme::styleHeader(lv_obj_t *obj)
{
    const HorizonPalette &p = current();
    lv_obj_set_style_bg_color(obj, hzColor(p.headerBg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, hzColor(p.accent), LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 2, LV_PART_MAIN);
    lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 4, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, hzColor(p.text), LV_PART_MAIN);
    lv_obj_set_style_text_font(obj, fontNormal(), LV_PART_MAIN);
}

void HorizonTheme::styleLabel(lv_obj_t *obj, bool dim)
{
    const HorizonPalette &p = current();
    lv_obj_set_style_text_color(obj, hzColor(dim ? p.textDim : p.text), LV_PART_MAIN);
    lv_obj_set_style_text_font(obj, fontNormal(), LV_PART_MAIN);
}

void HorizonTheme::styleList(lv_obj_t *obj)
{
    const HorizonPalette &p = current();
    lv_obj_set_style_bg_color(obj, hzColor(p.bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 2, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, hzColor(p.text), LV_PART_MAIN);
    lv_obj_set_style_text_font(obj, fontNormal(), LV_PART_MAIN);
}

void HorizonTheme::styleKeyboard(lv_obj_t *obj)
{
    const HorizonPalette &p = current();
    lv_obj_set_style_bg_color(obj, hzColor(p.headerBg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, hzColor(p.accent), LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 2, LV_PART_MAIN);
    lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_row(obj, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_column(obj, 3, LV_PART_MAIN);
    // кнопки матрицы
    lv_obj_set_style_bg_color(obj, hzColor(p.panel), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_color(obj, hzColor(p.border), LV_PART_ITEMS);
    lv_obj_set_style_border_width(obj, 1, LV_PART_ITEMS);
    lv_obj_set_style_radius(obj, 3, LV_PART_ITEMS);
    lv_obj_set_style_text_color(obj, hzColor(p.text), LV_PART_ITEMS);
    lv_obj_set_style_text_font(obj, fontNormal(), LV_PART_ITEMS);
}

#endif // HORIZON_UI
