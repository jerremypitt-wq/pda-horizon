// ПДА «Горизонт» - досье персонажа и настройки.
#ifdef HORIZON_UI

#include "horizon/HorizonConfig.h"
#include "horizon/HorizonSound.h"
#include "horizon/HorizonStrings.h"
#include "horizon/HorizonTheme.h"
#include "horizon/HorizonUI.h"
#include "horizon/HorizonView.h"
#include "graphics/driver/DisplayDriver.h"
#include "util/ILog.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#if defined(ARCH_ESP32)
#include "Arduino.h"
#endif

using namespace HorizonStrings;

namespace {
constexpr int16_t kStatusH = 22;
constexpr int16_t kHeaderH = 40;

// Степпер [-] значение [+]: idBase+0 - минус, idBase+1 - плюс.
// Возвращает label значения (обновлять из колбэка).
lv_obj_t *makeStepper(lv_obj_t *parent, const char *title, int val, int idBase, lv_event_cb_t cb, void *self)
{
    const HorizonPalette &p = HorizonTheme::current();
    lv_obj_t *row = lv_obj_create(parent);
    HorizonTheme::stylePanel(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, 52);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row, 6, LV_PART_MAIN);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *t = lv_label_create(row);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_color(t, hzColor(p.textDim), LV_PART_MAIN);
    lv_obj_t *valLbl = nullptr;
    for (int k = 0; k < 3; k++) {
        if (k == 1) {
            valLbl = lv_label_create(row);
            char vb[8];
            snprintf(vb, sizeof(vb), "%02d", val);
            lv_label_set_text(valLbl, vb);
            lv_obj_set_style_text_font(valLbl, HorizonTheme::fontMedium(), LV_PART_MAIN);
            lv_obj_set_width(valLbl, 44);
            lv_obj_set_style_text_align(valLbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        } else {
            lv_obj_t *b = lv_btn_create(row);
            HorizonTheme::styleButton(b, false);
            lv_obj_set_size(b, 56, 38);
            lv_obj_t *bl = lv_label_create(b);
            lv_label_set_text(bl, k == 0 ? "-" : "+");
            lv_obj_set_style_text_font(bl, HorizonTheme::fontMedium(), LV_PART_MAIN);
            lv_obj_center(bl);
            lv_obj_set_user_data(b, (void *)(intptr_t)(idBase + (k == 0 ? 0 : 1)));
            lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, self);
        }
    }
    return valLbl;
}

// epoch полуночи даты сборки (__DATE__ вида "Mmm dd yyyy", UTC).
uint32_t buildEpoch(void)
{
    static const char *mons = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char m[4] = {0};
    int d = 1, y = 2026;
    if (sscanf(__DATE__, "%3s %d %d", m, &d, &y) != 3)
        return 1767225600UL; // 01.01.2026 - если парсер удивился
    int mi = 0;
    const char *p = strstr(mons, m);
    if (p)
        mi = (int)(p - mons) / 3 + 1;
    if (mi < 1 || mi > 12 || d < 1 || d > 31 || y < 2020 || y > 2100)
        return 1767225600UL;
    int yy = (mi <= 2) ? y - 1 : y; // days_from_civil (Hinnant)
    int64_t era = (yy >= 0 ? yy : yy - 399) / 400;
    unsigned yoe = (unsigned)(yy - era * 400);
    unsigned doy = (153 * (unsigned)(mi + (mi > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = era * 146097 + (int64_t)doe - 719468;
    return (uint32_t)(days * 86400);
}

// База для ручной установки времени: валидные часы ПДА, иначе сохранённые,
// иначе дата сборки прошивки. Часы/минуты степперов лягут поверх даты базы.
uint32_t manualTimeBase(HorizonView *view)
{
    uint32_t t = view->getActTime();
    if (t > 1000000)
        return t;
    uint32_t s = HorizonConfig::instance().savedEpoch;
    if (s > 1000000)
        return s;
    return buildEpoch();
}
} // namespace

// ---------------- общие хелперы ----------------

lv_obj_t *HorizonUI::sectionLabel(lv_obj_t *parent, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, HorizonTheme::fontSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(l, hzColor(HorizonTheme::current().accent), LV_PART_MAIN);
    lv_obj_set_width(l, lv_pct(100));
    return l;
}

lv_obj_t *HorizonUI::rowButton(lv_obj_t *parent, const char *title, const char *sub, int id)
{
    lv_obj_t *b = lv_btn_create(parent);
    HorizonTheme::styleButton(b, false);
    lv_obj_set_width(b, lv_pct(100));
    lv_obj_set_height(b, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(b, sub ? 56 : 46, LV_PART_MAIN);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(b, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(b, 1, LV_PART_MAIN);

    lv_obj_t *t = lv_label_create(b);
    lv_label_set_text(t, title);
    lv_obj_set_width(t, lv_pct(100));
    if (sub) {
        lv_obj_t *s = lv_label_create(b);
        lv_label_set_text(s, sub);
        lv_obj_set_style_text_font(s, HorizonTheme::fontSmall(), LV_PART_MAIN);
        lv_obj_set_style_text_color(s, hzColor(HorizonTheme::current().textDim), LV_PART_MAIN);
        lv_obj_set_width(s, lv_pct(100));
    }
    lv_obj_set_user_data(b, (void *)(intptr_t)id);
    lv_obj_add_event_cb(b, cbRow, LV_EVENT_CLICKED, this);
    return b;
}

static void styleDropdownList(lv_obj_t *dd)
{
    lv_obj_t *lst = lv_dropdown_get_list(dd);
    if (lst) {
        const HorizonPalette &p = HorizonTheme::current();
        HorizonTheme::stylePanel(lst);
        lv_obj_set_style_bg_color(lst, hzColor(p.accent), LV_PART_SELECTED);
        lv_obj_set_style_text_color(lst, hzColor(p.accentText), LV_PART_SELECTED);
    }
}

lv_obj_t *HorizonUI::channelDropdown(lv_obj_t *parent, uint8_t selected)
{
    lv_obj_t *dd = lv_dropdown_create(parent);
    HorizonTheme::styleField(dd);
    lv_dropdown_set_options(dd, "Канал 0\nКанал 1\nКанал 2\nКанал 3\nКанал 4\nКанал 5\nКанал 6\nКанал 7");
    lv_dropdown_set_selected(dd, selected > 7 ? 0 : selected);
    lv_obj_set_width(dd, lv_pct(100));
    styleDropdownList(dd);
    return dd;
}

lv_obj_t *HorizonUI::simpleKeyDropdown(lv_obj_t *parent, uint8_t selected)
{
    lv_obj_t *dd = lv_dropdown_create(parent);
    HorizonTheme::styleField(dd);
    char opts[160] = "";
    for (uint8_t i = 0; i < kSimpleKeyCount; i++) {
        strcat(opts, kSimpleKeys[i]);
        if (i + 1 < kSimpleKeyCount)
            strcat(opts, "\n");
    }
    lv_dropdown_set_options(dd, opts);
    lv_dropdown_set_selected(dd, selected < kSimpleKeyCount ? selected : 1);
    lv_obj_set_width(dd, lv_pct(100));
    styleDropdownList(dd);
    return dd;
}

// индекс simple-ключа по текущему состоянию канала радиомодуля
static uint8_t currentKeyIndex(HorizonView *view, uint8_t ch)
{
    const meshtastic_Channel *c = view->radioChannel(ch);
    if (!c || !c->has_settings)
        return 1;
    if (c->settings.psk.size == 0)
        return 0;
    if (c->settings.psk.size == 1 && c->settings.psk.bytes[0] < kSimpleKeyCount)
        return c->settings.psk.bytes[0];
    return 1; // свой длинный ключ - в списке нет, будет заменён
}

static bool hasCustomKey(HorizonView *view, uint8_t ch)
{
    const meshtastic_Channel *c = view->radioChannel(ch);
    if (!c || !c->has_settings)
        return false;
    if (c->settings.psk.size == 0)
        return false;
    if (c->settings.psk.size == 1)
        return false;
    return true;
}

void HorizonUI::cbRow(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    int id = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t *)lv_event_get_target(e));
    if (self->current != Screen::SETTINGS)
        return;
    switch (id) {
    case 0:
        self->buildSetChannels();
        break;
    case 1:
        self->buildSetDevice();
        break;
    case 2:
        self->buildSetDisplay();
        break;
    case 3:
        self->buildSetTime();
        break;
    case 4:
        self->buildSetSound();
        break;
    case 5:
        self->buildSetTools();
        break;
    case 6:
        self->buildSetPin();
        break;
    case 7:
        self->buildSetReset();
        break;
    case 8:
        self->buildSetAbout();
        break;
    default:
        break;
    }
}

// ---------------- досье ----------------

void HorizonUI::showDossier(void)
{
    buildDossier();
}

void HorizonUI::buildDossier(void)
{
    clearScreen(Screen::DOSSIER);
    const HorizonPalette &p = HorizonTheme::current();
    HorizonConfig &cfg = HorizonConfig::instance();
    addStatusBar(scr);
    addNavHeader(scr, "Досье", "Персонаж игрока");

    lv_obj_t *body = lv_obj_create(scr);
    lv_obj_set_pos(body, 0, kStatusH + kHeaderH + 2);
    lv_obj_set_size(body, lv_pct(100), H() - (kStatusH + kHeaderH + 2) - 56);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(body, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(body, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(body, 6, LV_PART_MAIN);

    // Карточка
    lv_obj_t *card = lv_obj_create(body);
    HorizonTheme::stylePanel(card);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_column(card, 12, LV_PART_MAIN);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Аватар: первая буква позывного
    char initial[8] = "?";
    if (cfg.callsign[0]) {
        unsigned char c0 = (unsigned char)cfg.callsign[0];
        size_t ilen = 1;
        if ((c0 & 0xE0) == 0xC0)
            ilen = 2;
        else if ((c0 & 0xF0) == 0xE0)
            ilen = 3;
        else if ((c0 & 0xF8) == 0xF0)
            ilen = 4;
        strncpy(initial, cfg.callsign, ilen);
        initial[ilen] = '\0';
    }
    lv_obj_t *ava = lv_obj_create(card);
    lv_obj_set_size(ava, 64, 64);
    lv_obj_set_style_bg_color(ava, hzColor(p.accent), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ava, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(ava, 6, LV_PART_MAIN);
    lv_obj_set_style_border_width(ava, 0, LV_PART_MAIN);
    lv_obj_remove_flag(ava, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *al = lv_label_create(ava);
    lv_label_set_text(al, initial);
    lv_obj_set_style_text_font(al, HorizonTheme::fontBig(), LV_PART_MAIN);
    lv_obj_set_style_text_color(al, hzColor(p.accentText), LV_PART_MAIN);
    lv_obj_center(al);

    lv_obj_t *col = lv_obj_create(card);
    // Ширина - точная, иначе проценты от flex-родителя не дают обёртке
    // границ и длинные имя/позывной режутся: аватар 64 + зазор 12 +
    // пады карточки 20 + пады body 16.
    lv_obj_set_width(col, W() - 112);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(col, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(col, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *call = lv_label_create(col);
    lv_label_set_text_fmt(call, "«%s»", cfg.callsign);
    lv_obj_set_style_text_font(call, HorizonTheme::fontMedium(), LV_PART_MAIN);
    lv_obj_set_width(call, lv_pct(100));
    lv_label_set_long_mode(call, LV_LABEL_LONG_WRAP);
    lv_obj_t *nm = lv_label_create(col);
    lv_label_set_text(nm, cfg.charName);
    lv_obj_set_style_text_color(nm, hzColor(p.textDim), LV_PART_MAIN);
    lv_obj_set_width(nm, lv_pct(100));
    lv_label_set_long_mode(nm, LV_LABEL_LONG_WRAP);

    struct Field {
        const char *k;
        const char *v;
    };
    char groupbuf[48];
    snprintf(groupbuf, sizeof(groupbuf), "%s", kGroups[cfg.group < kGroupCount ? cfg.group : 0]);
    Field fields[] = {
        {"Ранг", kRanks[cfg.rank < kRankCount ? cfg.rank : 0]},
        {"Группировка", groupbuf},
    };
    for (auto &f : fields) {
        lv_obj_t *row = lv_obj_create(body);
        HorizonTheme::stylePanel(row);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(row, 8, LV_PART_MAIN);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *k = lv_label_create(row);
        lv_label_set_text(k, f.k);
        lv_obj_set_style_text_color(k, hzColor(p.textDim), LV_PART_MAIN);
        lv_obj_t *v = lv_label_create(row);
        lv_label_set_text(v, f.v);
        lv_obj_set_width(v, lv_pct(62));
        lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    }

    lv_obj_t *edit = lv_btn_create(scr);
    HorizonTheme::styleButton(edit, true);
    lv_obj_set_size(edit, 64, 40);
    lv_obj_align(edit, LV_ALIGN_BOTTOM_LEFT, 8, -8);
    lv_obj_t *el = lv_label_create(edit);
    lv_label_set_text(el, kGlyphSettings); // ключ: правка досье под мастер-PIN
    lv_obj_set_style_text_font(el, HorizonTheme::fontNormal(), LV_PART_MAIN);
    lv_obj_center(el);
    lv_obj_add_event_cb(edit, cbDossierEdit, LV_EVENT_CLICKED, this);
}

void HorizonUI::cbDossierEdit(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    self->askPin("Мастер-PIN для досье:", [self]() { self->buildDossierEdit(); });
}

void HorizonUI::buildDossierEdit(void)
{
    clearScreen(Screen::DOSSIER_EDIT);
    HorizonConfig &cfg = HorizonConfig::instance();
    addStatusBar(scr);
    addNavHeader(scr, "Досье: правка", "Доступ мастера");

    lv_obj_t *body = lv_obj_create(scr);
    lv_obj_set_pos(body, 0, kStatusH + kHeaderH + 2);
    lv_obj_set_size(body, lv_pct(100), H() - (kStatusH + kHeaderH + 2) - 56);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(body, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(body, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(body, 4, LV_PART_MAIN);

    sectionLabel(body, "Имя персонажа");
    editName = lv_textarea_create(body);
    HorizonTheme::styleField(editName);
    lv_textarea_set_one_line(editName, true);
    lv_textarea_set_max_length(editName, 30);
    lv_textarea_set_text(editName, cfg.charName);
    lv_obj_set_width(editName, lv_pct(100));
    lv_obj_set_height(editName, 40);
    lv_obj_add_event_cb(editName, cbInputFocus, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(editName, cbInputFocus, LV_EVENT_DEFOCUSED, this);

    sectionLabel(body, "Позывной");
    editCall = lv_textarea_create(body);
    HorizonTheme::styleField(editCall);
    lv_textarea_set_one_line(editCall, true);
    lv_textarea_set_max_length(editCall, 20);
    lv_textarea_set_text(editCall, cfg.callsign);
    lv_obj_set_width(editCall, lv_pct(100));
    lv_obj_set_height(editCall, 40);
    lv_obj_add_event_cb(editCall, cbInputFocus, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(editCall, cbInputFocus, LV_EVENT_DEFOCUSED, this);

    sectionLabel(body, "Ранг");
    editRank = lv_dropdown_create(body);
    HorizonTheme::styleField(editRank);
    {
        char opts[80] = "";
        for (uint8_t i = 0; i < kRankCount; i++) {
            strcat(opts, kRanks[i]);
            if (i + 1 < kRankCount)
                strcat(opts, "\n");
        }
        lv_dropdown_set_options(editRank, opts);
    }
    lv_dropdown_set_selected(editRank, cfg.rank);
    lv_obj_set_width(editRank, lv_pct(100));
    styleDropdownList(editRank);

    sectionLabel(body, "Группировка");
    editGroup = lv_dropdown_create(body);
    HorizonTheme::styleField(editGroup);
    {
        char opts[160] = "";
        for (uint8_t i = 0; i < kGroupCount; i++) {
            strcat(opts, kGroups[i]);
            if (i + 1 < kGroupCount)
                strcat(opts, "\n");
        }
        lv_dropdown_set_options(editGroup, opts);
    }
    lv_dropdown_set_selected(editGroup, cfg.group);
    lv_obj_set_width(editGroup, lv_pct(100));
    styleDropdownList(editGroup);

    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_set_size(row, lv_pct(100), 52);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *cancel = lv_btn_create(row);
    HorizonTheme::styleButton(cancel, false);
    lv_obj_set_size(cancel, 130, 40);
    lv_obj_t *cl = lv_label_create(cancel);
    lv_label_set_text(cl, "Отмена");
    lv_obj_center(cl);
    lv_obj_add_event_cb(cancel, cbBack, LV_EVENT_CLICKED, this);

    lv_obj_t *save = lv_btn_create(row);
    HorizonTheme::styleButton(save, true);
    lv_obj_set_size(save, 130, 40);
    lv_obj_t *sl = lv_label_create(save);
    lv_label_set_text(sl, "Сохранить");
    lv_obj_center(sl);
    lv_obj_add_event_cb(save, cbDossierSave, LV_EVENT_CLICKED, this);
}

void HorizonUI::cbDossierSave(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    self->saveDossier();
}

void HorizonUI::saveDossier(void)
{
    HorizonConfig &cfg = HorizonConfig::instance();
    const char *nm = editName ? lv_textarea_get_text(editName) : "";
    const char *cs = editCall ? lv_textarea_get_text(editCall) : "";
    if (!nm || !*nm || !cs || !*cs) {
        toast("Заполни имя и позывной");
        return;
    }
    // Копирование с обрезкой по границе UTF-8 (30 символов кириллицы
    // не влезут в 40 байт); целые строки copyUtf8 не трогает.
    copyUtf8(cfg.charName, sizeof(cfg.charName), nm);
    copyUtf8(cfg.callsign, sizeof(cfg.callsign), cs);
    if (editRank)
        cfg.rank = (uint8_t)lv_dropdown_get_selected(editRank);
    if (editGroup)
        cfg.group = (uint8_t)lv_dropdown_get_selected(editGroup);
    cfg.save();
    view->applyOwnerFromDossier();
    view->announceDossier(); // разослать новый позывной/ранг сети
    toast("Досье сохранено");
    showDossier();
}

// ---------------- настройки: список ----------------

void HorizonUI::showSettings(void)
{
    buildSettings();
}

void HorizonUI::buildSettings(void)
{
    clearScreen(Screen::SETTINGS);
    addStatusBar(scr);
    addNavHeader(scr, "Настройки", "ПДА и радиомодуль");

    lv_obj_t *body = lv_obj_create(scr);
    lv_obj_set_pos(body, 0, kStatusH + kHeaderH + 2);
    lv_obj_set_size(body, lv_pct(100), H() - (kStatusH + kHeaderH + 2) - 2);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(body, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(body, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(body, 6, LV_PART_MAIN);

    HorizonConfig &cfg = HorizonConfig::instance();
    char chsub[64];
    snprintf(chsub, sizeof(chsub), "Общий %u · Группа %u · Объявл. %u", cfg.generalCh, cfg.groupCh, cfg.announceCh);
    rowButton(body, "Каналы чатов", chsub, 0);
    rowButton(body, "Устройство (LoRa)", "Регион, пресет, роль, радиомолчание", 1);
    rowButton(body, "Экран и тема", kThemes[cfg.theme], 2);
    char timesub[16];
    fmtTime(view->getActTime(), timesub, sizeof(timesub));
    rowButton(body, "Время", timesub, 3);
    rowButton(body, "Звук", cfg.silent ? "Бесшумный режим" : "Звук включён", 4);
    rowButton(body, "Инструменты Mesh", "Детектор, сканер, маршрут", 5);
    rowButton(body, "Мастер-пароль", "Смена PIN", 6);
    rowButton(body, "Сброс", "ПДА, NodeDB, заводской", 7);
    rowButton(body, "О ПДА", "Версия и состояние", 8);
}

// ---------------- настройки: время ----------------

void HorizonUI::buildSetTime(void)
{
    clearScreen(Screen::SET_TIME);
    addStatusBar(scr);
    addNavHeader(scr, "Время", "Часы ПДА");

    lv_obj_t *body = lv_obj_create(scr);
    lv_obj_set_pos(body, 0, kStatusH + kHeaderH + 2);
    lv_obj_set_size(body, lv_pct(100), H() - (kStatusH + kHeaderH + 2) - 2);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(body, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(body, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(body, 6, LV_PART_MAIN);

    sectionLabel(body, "Текущее время");
    timeCur = lv_label_create(body);
    lv_obj_set_style_text_font(timeCur, HorizonTheme::fontMedium(), LV_PART_MAIN);
    {
        char tb[16];
        fmtTime(view->getActTime(), tb, sizeof(tb));
        lv_label_set_text_fmt(timeCur, "Сейчас: %s", tb);
    }
    {
        char db[16];
        fmtDate(manualTimeBase(view), db, sizeof(db));
        lv_obj_t *dl = lv_label_create(body);
        lv_label_set_text_fmt(dl, "Дата: %s", db);
        lv_obj_set_style_text_font(dl, HorizonTheme::fontSmall(), LV_PART_MAIN);
        lv_obj_set_style_text_color(dl, hzColor(HorizonTheme::current().textDim), LV_PART_MAIN);
    }

    // Черновик - из текущих часов (или 12:00, если часы не идут).
    uint32_t t = view->getActTime();
    if (t > 1000000) {
        time_t tt = (time_t)t;
        struct tm tmv;
        gmtime_r(&tt, &tmv);
        timeH = tmv.tm_hour;
        timeM = tmv.tm_min;
    } else {
        timeH = 12;
        timeM = 0;
    }
    sectionLabel(body, "Установить время");
    timeHVal = makeStepper(body, "Часы", timeH, 0, cbTimeStep, this);
    timeMVal = makeStepper(body, "Минуты", timeM, 2, cbTimeStep, this);

    lv_obj_t *apply = lv_btn_create(body);
    HorizonTheme::styleButton(apply, true);
    lv_obj_set_width(apply, lv_pct(100));
    lv_obj_set_height(apply, 44);
    lv_obj_t *al = lv_label_create(apply);
    lv_label_set_text(al, "Применить");
    lv_obj_center(al);
    lv_obj_add_event_cb(apply, cbTimeApply, LV_EVENT_CLICKED, this);

    lv_obj_t *hint = lv_label_create(body);
    lv_label_set_text(hint, "Дата - из сети или прошивки. Часы сохраняются раз в минуту и переживают "
                            "перезагрузку; после долгого выключения их подправит сеть.");
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, HorizonTheme::fontSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(hint, hzColor(HorizonTheme::current().textDim), LV_PART_MAIN);
}

void HorizonUI::cbTimeStep(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    int id = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t *)lv_event_get_target(e));
    if (id == 0)
        self->timeH = (self->timeH + 23) % 24;
    else if (id == 1)
        self->timeH = (self->timeH + 1) % 24;
    else if (id == 2)
        self->timeM = (self->timeM + 59) % 60;
    else if (id == 3)
        self->timeM = (self->timeM + 1) % 60;
    else
        return;
    char vb[8];
    snprintf(vb, sizeof(vb), "%02d", id < 2 ? self->timeH : self->timeM);
    lv_obj_t *lbl = id < 2 ? self->timeHVal : self->timeMVal;
    if (lbl && lv_obj_is_valid(lbl))
        lv_label_set_text(lbl, vb);
}

void HorizonUI::cbTimeApply(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    uint32_t base = manualTimeBase(self->view);
    uint32_t epoch =
        base - (base % 86400) + (uint32_t)self->timeH * 3600 + (uint32_t)self->timeM * 60;
    self->view->setManualTime(epoch);
    self->toast("Время установлено");
    self->buildSetTime();
}

// ---------------- настройки: звук ----------------

void HorizonUI::buildSetSound(void)
{
    clearScreen(Screen::SET_SOUND);
    HorizonConfig &cfg = HorizonConfig::instance();
    addStatusBar(scr);
    addNavHeader(scr, "Звук", "Уведомления");

    lv_obj_t *body = lv_obj_create(scr);
    lv_obj_set_pos(body, 0, kStatusH + kHeaderH + 2);
    lv_obj_set_size(body, lv_pct(100), H() - (kStatusH + kHeaderH + 2) - 2);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(body, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(body, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(body, 6, LV_PART_MAIN);

    sectionLabel(body, "Режим");
    lv_obj_t *row = lv_obj_create(body);
    HorizonTheme::stylePanel(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, 52);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row, 8, LV_PART_MAIN);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *t = lv_label_create(row);
    lv_label_set_text(t, "Бесшумный режим");
    lv_obj_t *sw = lv_switch_create(row);
    if (cfg.silent)
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, cbSoundToggle, LV_EVENT_VALUE_CHANGED, this);

    sectionLabel(body, "Набор звуков");
    const char *sndNames[3] = {"Сталкер", "Двойной", "Одиночный"};
    for (int i = 0; i < 3; i++) {
        char title[48];
        snprintf(title, sizeof(title), "%s %s", cfg.soundSet == i ? "[x]" : "[ ]", sndNames[i]);
        lv_obj_t *b = rowButton(body, title, nullptr, i);
        lv_obj_remove_event_cb(b, cbRow);
        lv_obj_add_event_cb(b, cbSoundSet, LV_EVENT_CLICKED, this);
    }

    sectionLabel(body, "Проверка");
    lv_obj_t *test = lv_btn_create(body);
    HorizonTheme::styleButton(test, false);
    lv_obj_set_width(test, lv_pct(100));
    lv_obj_set_height(test, 44);
    lv_obj_t *tl = lv_label_create(test);
    lv_label_set_text(tl, "Проверить звук");
    lv_obj_center(tl);
    lv_obj_add_event_cb(test, cbSoundTest, LV_EVENT_CLICKED, this);

    lv_obj_t *hint = lv_label_create(body);
    lv_label_set_text(hint, "Бесшумный режим глушит бипы сообщений. Тосты на экране остаются.");
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, HorizonTheme::fontSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(hint, hzColor(HorizonTheme::current().textDim), LV_PART_MAIN);
}

void HorizonUI::cbSoundToggle(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    HorizonConfig::instance().silent = on;
    HorizonConfig::instance().save();
    self->toast(on ? "Бесшумный режим включён" : "Звук включён");
}

void HorizonUI::cbSoundSet(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    int id = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t *)lv_event_get_target(e));
    if (id < 0 || id > 2)
        return;
    HorizonConfig::instance().soundSet = (uint8_t)id;
    HorizonConfig::instance().save();
    if (!HorizonConfig::instance().silent)
        HorizonSound::instance().play(HorizonSound::MESSAGE); // сразу послушать
    self->buildSetSound(); // перерисовать галочки
}

void HorizonUI::cbSoundTest(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    if (HorizonConfig::instance().silent) {
        self->toast("Сейчас бесшумный режим");
        return;
    }
    HorizonSound::instance().play(HorizonSound::MESSAGE);
}

// ---------------- настройки: каналы ----------------

void HorizonUI::buildSetChannels(void)
{
    clearScreen(Screen::SET_CHANNELS);
    HorizonConfig &cfg = HorizonConfig::instance();
    addStatusBar(scr);
    addNavHeader(scr, "Каналы чатов", "Выбери раздел");

    lv_obj_t *body = lv_obj_create(scr);
    lv_obj_set_pos(body, 0, kStatusH + kHeaderH + 2);
    lv_obj_set_size(body, lv_pct(100), H() - (kStatusH + kHeaderH + 2) - 2);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(body, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(body, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(body, 6, LV_PART_MAIN);

    struct ChRow {
        const char *title;
        uint8_t ch;
    };
    ChRow rows[3] = {
        {"Общий чат", cfg.generalCh},
        {"Чат группировки", cfg.groupCh},
        {"Объявления", cfg.announceCh},
    };
    for (int i = 0; i < 3; i++) {
        char sub[64];
        snprintf(sub, sizeof(sub), "%s · %s", view->channelLabel(rows[i].ch),
                 view->channelIsLocked(rows[i].ch) ? kGlyphLock : kGlyphUnlock);
        lv_obj_t *b = rowButton(body, rows[i].title, sub, i);
        // rowButton вешает cbRow (работает только на экране SETTINGS) -
        // переназначить на выбор канала.
        lv_obj_remove_event_cb(b, cbRow);
        lv_obj_add_event_cb(b, cbSetChannelPick, LV_EVENT_CLICKED, this);
    }

    lv_obj_t *hint = lv_label_create(body);
    lv_label_set_text(hint, "Закрытый чат: одинаковый ключ группы на всех ПДА.");
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, HorizonTheme::fontSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(hint, hzColor(HorizonTheme::current().textDim), LV_PART_MAIN);
}

void HorizonUI::cbSetChannelPick(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    int id = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t *)lv_event_get_target(e));
    if (id < 0 || id > 2)
        return;
    self->setDetailKind = (uint8_t)id;
    self->buildSetChannelDetail();
}

void HorizonUI::buildSetChannelDetail(void)
{
    clearScreen(Screen::SET_CHANNEL_DETAIL);
    HorizonConfig &cfg = HorizonConfig::instance();
    static const char *titles[3] = {"Общий чат - канал", "Чат группировки - канал", "Объявления - канал"};
    static const char *defaults[3] = {"GENERAL", "GROUP", "INFO"};
    uint8_t kind = setDetailKind > 2 ? 0 : setDetailKind;
    uint8_t cur = kind == 0 ? cfg.generalCh : (kind == 1 ? cfg.groupCh : cfg.announceCh);
    addStatusBar(scr);
    addNavHeader(scr, titles[kind], "Номер, имя и ключ");

    lv_obj_t *body = lv_obj_create(scr);
    lv_obj_set_pos(body, 0, kStatusH + kHeaderH + 2);
    lv_obj_set_size(body, lv_pct(100), H() - (kStatusH + kHeaderH + 2) - 56);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(body, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(body, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(body, 4, LV_PART_MAIN);

    sectionLabel(body, "Номер канала");
    setDetailCh = channelDropdown(body, cur);
    char info[96];
    snprintf(info, sizeof(info), "Сейчас: %s %s", view->channelLabel(cur),
             view->channelIsLocked(cur) ? kGlyphLock : kGlyphUnlock);
    lv_obj_t *ci = lv_label_create(body);
    lv_label_set_text(ci, info);
    lv_obj_set_style_text_font(ci, HorizonTheme::fontSmall(), LV_PART_MAIN);

    sectionLabel(body, "Имя канала в эфире");
    setDetailName = lv_textarea_create(body);
    HorizonTheme::styleField(setDetailName);
    lv_textarea_set_one_line(setDetailName, true);
    lv_textarea_set_max_length(setDetailName, 11);
    {
        const meshtastic_Channel *c = view->radioChannel(cur);
        lv_textarea_set_text(setDetailName,
                             (c && c->has_settings && c->settings.name[0]) ? c->settings.name : defaults[kind]);
    }
    lv_obj_set_width(setDetailName, lv_pct(100));
    lv_obj_set_height(setDetailName, 40);
    lv_obj_add_event_cb(setDetailName, cbInputFocus, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(setDetailName, cbInputFocus, LV_EVENT_DEFOCUSED, this);

    sectionLabel(body, "Ключ канала");
    setDetailKey = simpleKeyDropdown(body, currentKeyIndex(view, cur));
    if (hasCustomKey(view, cur)) {
        lv_obj_t *w = lv_label_create(body);
        lv_label_set_text(w, "Внимание: на канале свой длинный ключ, он будет заменён.");
        lv_obj_set_style_text_font(w, HorizonTheme::fontSmall(), LV_PART_MAIN);
        lv_obj_set_style_text_color(w, hzColor(HorizonTheme::current().danger), LV_PART_MAIN);
    }

    lv_obj_t *apply = lv_btn_create(scr);
    HorizonTheme::styleButton(apply, true);
    lv_obj_set_size(apply, lv_pct(70), 40);
    lv_obj_align(apply, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_t *al = lv_label_create(apply);
    lv_label_set_text(al, "Применить");
    lv_obj_center(al);
    lv_obj_add_event_cb(apply, cbSetApplyChannelDetail, LV_EVENT_CLICKED, this);
}

void HorizonUI::cbSetApplyChannelDetail(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    HorizonConfig &cfg = HorizonConfig::instance();
    uint8_t kind = self->setDetailKind > 2 ? 0 : self->setDetailKind;
    uint8_t ch = self->setDetailCh ? (uint8_t)lv_dropdown_get_selected(self->setDetailCh) : 0;
    uint8_t o1 = kind == 0 ? cfg.groupCh : cfg.generalCh;
    uint8_t o2 = kind == 2 ? cfg.groupCh : cfg.announceCh;
    if (ch == o1 || ch == o2) {
        self->toast("Канал уже занят другим разделом");
        return;
    }
    if (kind != 0 && ch == 0) {
        self->toast("Канал 0 - только для общего чата");
        return;
    }
    if (kind == 0)
        cfg.generalCh = ch;
    else if (kind == 1)
        cfg.groupCh = ch;
    else
        cfg.announceCh = ch;
    cfg.save();

    const char *nm = self->setDetailName ? lv_textarea_get_text(self->setDetailName) : "";
    uint8_t key = self->setDetailKey ? (uint8_t)lv_dropdown_get_selected(self->setDetailKey) : 0xFF;
    self->view->applyChannelSettings(ch, (nm && *nm) ? nm : nullptr, key);
    self->toast("Канал отправлен на узел");
    self->askConfirm("Перезагрузить радиомодуль, чтобы ключи вступили в силу?", "Да", [self]() {
        self->view->rebootNode();
        self->toast("Узел перезагружается...");
    });
}

// ---------------- настройки: устройство ----------------

static int findRegionIndex(int code)
{
    for (uint8_t i = 0; i < kRegionCount; i++) {
        if (kRegions[i].code == code)
            return i;
    }
    return 0;
}

static int findPresetIndex(int code)
{
    for (uint8_t i = 0; i < kPresetCount; i++) {
        if (kPresets[i].code == code)
            return i;
    }
    return 0;
}

static int findRoleIndex(int code)
{
    for (uint8_t i = 0; i < kRoleCount; i++) {
        if (kRoles[i].code == code)
            return i;
    }
    return 0;
}

void HorizonUI::buildSetDevice(void)
{
    clearScreen(Screen::SET_DEVICE);
    addStatusBar(scr);
    addNavHeader(scr, "Устройство", "Настройки радиомодуля");

    lv_obj_t *body = lv_obj_create(scr);
    lv_obj_set_pos(body, 0, kStatusH + kHeaderH + 2);
    lv_obj_set_size(body, lv_pct(100), H() - (kStatusH + kHeaderH + 2) - 56);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(body, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(body, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(body, 4, LV_PART_MAIN);

    int curRegion = 9, curPreset = 0, curHops = 3, curRole = 0;
    bool curTx = true;
    if (view->hasLoRaConfig()) {
        curRegion = (int)view->loraConfig().region;
        curPreset = (int)view->loraConfig().modem_preset;
        curHops = (int)view->loraConfig().hop_limit;
        curTx = view->loraConfig().tx_enabled;
    }
    if (view->hasDeviceConfig())
        curRole = (int)view->deviceConfig().role;

    sectionLabel(body, "Регион LoRa");
    setRegion = lv_dropdown_create(body);
    HorizonTheme::styleField(setRegion);
    {
        char opts[256] = "";
        for (uint8_t i = 0; i < kRegionCount; i++) {
            strcat(opts, kRegions[i].name);
            if (i + 1 < kRegionCount)
                strcat(opts, "\n");
        }
        lv_dropdown_set_options(setRegion, opts);
    }
    lv_dropdown_set_selected(setRegion, findRegionIndex(curRegion));
    lv_obj_set_width(setRegion, lv_pct(100));
    styleDropdownList(setRegion);

    sectionLabel(body, "Модем-пресет");
    setPreset = lv_dropdown_create(body);
    HorizonTheme::styleField(setPreset);
    {
        char opts[256] = "";
        for (uint8_t i = 0; i < kPresetCount; i++) {
            strcat(opts, kPresets[i].name);
            if (i + 1 < kPresetCount)
                strcat(opts, "\n");
        }
        lv_dropdown_set_options(setPreset, opts);
    }
    lv_dropdown_set_selected(setPreset, findPresetIndex(curPreset));
    lv_obj_set_width(setPreset, lv_pct(100));
    styleDropdownList(setPreset);

    sectionLabel(body, "Хопы (дальность)");
    setHops = lv_dropdown_create(body);
    HorizonTheme::styleField(setHops);
    lv_dropdown_set_options(setHops, "1\n2\n3\n4\n5\n6\n7");
    lv_dropdown_set_selected(setHops, curHops >= 1 && curHops <= 7 ? curHops - 1 : 2);
    lv_obj_set_width(setHops, lv_pct(100));
    styleDropdownList(setHops);

    sectionLabel(body, "Роль устройства");
    setRole = lv_dropdown_create(body);
    HorizonTheme::styleField(setRole);
    {
        char opts[160] = "";
        for (uint8_t i = 0; i < kRoleCount; i++) {
            strcat(opts, kRoles[i].name);
            if (i + 1 < kRoleCount)
                strcat(opts, "\n");
        }
        lv_dropdown_set_options(setRole, opts);
    }
    lv_dropdown_set_selected(setRole, findRoleIndex(curRole));
    lv_obj_set_width(setRole, lv_pct(100));
    styleDropdownList(setRole);

    // Передача в эфир (радиомолчание)
    lv_obj_t *txrow = lv_obj_create(body);
    HorizonTheme::stylePanel(txrow);
    lv_obj_set_width(txrow, lv_pct(100));
    lv_obj_set_height(txrow, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(txrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(txrow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(txrow, 8, LV_PART_MAIN);
    lv_obj_remove_flag(txrow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *txl = lv_label_create(txrow);
    lv_label_set_text(txl, "Передача в эфир");
    setTx = lv_switch_create(txrow);
    if (curTx)
        lv_obj_add_state(setTx, LV_STATE_CHECKED);

    lv_obj_t *hint = lv_label_create(body);
    lv_label_set_text(hint, "Выкл. передача = радиомолчание:\nПДА только слушает эфир.");
    lv_obj_set_style_text_font(hint, HorizonTheme::fontSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(hint, hzColor(HorizonTheme::current().textDim), LV_PART_MAIN);

    lv_obj_t *apply = lv_btn_create(scr);
    HorizonTheme::styleButton(apply, true);
    lv_obj_set_size(apply, lv_pct(70), 40);
    lv_obj_align(apply, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_t *al = lv_label_create(apply);
    lv_label_set_text(al, "Применить");
    lv_obj_center(al);
    lv_obj_add_event_cb(apply, cbSetApplyDevice, LV_EVENT_CLICKED, this);
}

void HorizonUI::cbSetApplyDevice(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    int region = kRegions[lv_dropdown_get_selected(self->setRegion)].code;
    int preset = kPresets[lv_dropdown_get_selected(self->setPreset)].code;
    uint32_t hops = (uint32_t)lv_dropdown_get_selected(self->setHops) + 1;
    int role = kRoles[lv_dropdown_get_selected(self->setRole)].code;
    bool tx = lv_obj_has_state(self->setTx, LV_STATE_CHECKED);
    self->view->applyLoRaConfig(region, preset, hops, tx);
    self->view->applyDeviceRole(role);
    self->toast("Настройки отправлены на узел");
    self->askConfirm("Перезагрузить радиомодуль для применения?", "Да", [self]() {
        self->view->rebootNode();
        self->toast("Узел перезагружается...");
    });
}

// ---------------- настройки: экран ----------------

void HorizonUI::buildSetDisplay(void)
{
    clearScreen(Screen::SET_DISPLAY);
    HorizonConfig &cfg = HorizonConfig::instance();
    displayDirty = false;
    addStatusBar(scr);
    addNavHeader(scr, "Экран и тема", "Яркость, таймаут, вид");

    lv_obj_t *body = lv_obj_create(scr);
    lv_obj_set_pos(body, 0, kStatusH + kHeaderH + 2);
    lv_obj_set_size(body, lv_pct(100), H() - (kStatusH + kHeaderH + 2) - 56);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(body, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(body, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(body, 4, LV_PART_MAIN);

    sectionLabel(body, "Яркость экрана");
    setBrightness = lv_slider_create(body);
    lv_slider_set_range(setBrightness, 20, 255);
    lv_slider_set_value(setBrightness, cfg.brightness, LV_ANIM_OFF);
    lv_obj_set_width(setBrightness, lv_pct(100));
    lv_obj_set_style_bg_color(setBrightness, hzColor(HorizonTheme::current().panel), LV_PART_MAIN);
    lv_obj_set_style_bg_color(setBrightness, hzColor(HorizonTheme::current().accent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(setBrightness, hzColor(HorizonTheme::current().accent), LV_PART_KNOB);
    lv_obj_add_event_cb(setBrightness, cbBrightness, LV_EVENT_VALUE_CHANGED, this);
    setBrightnessVal = lv_label_create(body);
    lv_label_set_text_fmt(setBrightnessVal, "%u%%", (unsigned)(cfg.brightness * 100 / 255));
    lv_obj_set_style_text_font(setBrightnessVal, HorizonTheme::fontSmall(), LV_PART_MAIN);

    sectionLabel(body, "Таймаут экрана");
    setTimeout = lv_dropdown_create(body);
    HorizonTheme::styleField(setTimeout);
    {
        char opts[128] = "";
        for (uint8_t i = 0; i < kTimeoutCount; i++) {
            strcat(opts, kTimeouts[i].name);
            if (i + 1 < kTimeoutCount)
                strcat(opts, "\n");
        }
        lv_dropdown_set_options(setTimeout, opts);
    }
    {
        int sel = 2;
        for (uint8_t i = 0; i < kTimeoutCount; i++) {
            if (kTimeouts[i].seconds == cfg.timeoutSec) {
                sel = i;
                break;
            }
        }
        lv_dropdown_set_selected(setTimeout, sel);
    }
    lv_obj_set_width(setTimeout, lv_pct(100));
    styleDropdownList(setTimeout);

    sectionLabel(body, "Ориентация");
    setOrientation = lv_dropdown_create(body);
    HorizonTheme::styleField(setOrientation);
    lv_dropdown_set_options(setOrientation, "Альбомная\nКнижная");
    lv_dropdown_set_selected(setOrientation, cfg.orientation);
    lv_obj_set_width(setOrientation, lv_pct(100));
    styleDropdownList(setOrientation);

    sectionLabel(body, "Цветовая тема");
    setTheme = lv_dropdown_create(body);
    HorizonTheme::styleField(setTheme);
    {
        char opts[160] = "";
        for (uint8_t i = 0; i < kThemeCount; i++) {
            strcat(opts, kThemes[i]);
            if (i + 1 < kThemeCount)
                strcat(opts, "\n");
        }
        lv_dropdown_set_options(setTheme, opts);
    }
    lv_dropdown_set_selected(setTheme, cfg.theme);
    lv_obj_set_width(setTheme, lv_pct(100));
    styleDropdownList(setTheme);

    lv_obj_t *hint = lv_label_create(body);
    lv_label_set_text(hint, "Ориентация применится после перезагрузки ПДА.");
    lv_obj_set_style_text_font(hint, HorizonTheme::fontSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(hint, hzColor(HorizonTheme::current().textDim), LV_PART_MAIN);

    lv_obj_t *apply = lv_btn_create(scr);
    HorizonTheme::styleButton(apply, true);
    lv_obj_set_size(apply, lv_pct(70), 40);
    lv_obj_align(apply, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_t *al = lv_label_create(apply);
    lv_label_set_text(al, "Применить");
    lv_obj_center(al);
    lv_obj_add_event_cb(apply, cbSetApplyDisplay, LV_EVENT_CLICKED, this);
}

void HorizonUI::cbBrightness(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    int v = lv_slider_get_value(self->setBrightness);
    HorizonConfig::instance().brightness = (uint8_t)v;
    self->displayDirty = true;
    if (self->setBrightnessVal && lv_obj_is_valid(self->setBrightnessVal))
        lv_label_set_text_fmt(self->setBrightnessVal, "%u%%", (unsigned)(v * 100 / 255));
    DisplayDriver *dd = self->view->getDisplayDriver();
    if (dd)
        dd->setBrightness((uint8_t)v);
}

void HorizonUI::cbThemeChanged(lv_event_t *e)
{
    (void)e;
}

void HorizonUI::cbSetApplyDisplay(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    HorizonConfig &cfg = HorizonConfig::instance();
    uint8_t oldTheme = cfg.theme;
    uint8_t oldOrient = cfg.orientation;
    if (self->setBrightness)
        cfg.brightness = (uint8_t)lv_slider_get_value(self->setBrightness);
    if (self->setTimeout)
        cfg.timeoutSec = (uint16_t)kTimeouts[lv_dropdown_get_selected(self->setTimeout)].seconds;
    if (self->setOrientation)
        cfg.orientation = (uint8_t)lv_dropdown_get_selected(self->setOrientation);
    if (self->setTheme)
        cfg.theme = (uint8_t)lv_dropdown_get_selected(self->setTheme);
    cfg.save();
    self->displayDirty = false;

    DisplayDriver *dd = self->view->getDisplayDriver();
    if (dd) {
        dd->setBrightness(cfg.brightness);
        dd->setScreenTimeout(cfg.timeoutSec);
    }
    if (cfg.orientation != oldOrient) {
        self->askConfirm(
            "Перезагрузить ПДА для смены ориентации?", "Да",
            [self]() {
#if defined(ARCH_ESP32)
                delay(300);
                ESP.restart();
#else
                self->toast("Перезапусти приложение");
#endif
            },
            [self, oldOrient]() {
                // Отмена: вернуть прежнюю ориентацию, иначе она применится
                // при следующей перезагрузке, несмотря на отказ.
                HorizonConfig::instance().orientation = oldOrient;
                HorizonConfig::instance().save();
                self->buildSetDisplay();
            });
        return;
    }
    if (cfg.theme != oldTheme) {
        self->toast("Тема применена");
        self->showMenu();
        return;
    }
    self->toast("Экран настроен");
    self->showSettings();
}

void HorizonUI::cbRebootNow(lv_event_t *e)
{
    (void)e;
#if defined(ARCH_ESP32)
    ESP.restart();
#endif
}

// ---------------- настройки: PIN ----------------

void HorizonUI::buildSetPin(void)
{
    clearScreen(Screen::SET_PIN);
    addStatusBar(scr);
    addNavHeader(scr, "Мастер-пароль", "PIN для досье и сброса");

    lv_obj_t *body = lv_obj_create(scr);
    lv_obj_set_pos(body, 0, kStatusH + kHeaderH + 2);
    lv_obj_set_size(body, lv_pct(100), H() - (kStatusH + kHeaderH + 2) - 56);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(body, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(body, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(body, 4, LV_PART_MAIN);

    sectionLabel(body, "Старый PIN");
    pinOld = lv_textarea_create(body);
    HorizonTheme::styleField(pinOld);
    lv_textarea_set_one_line(pinOld, true);
    lv_textarea_set_password_mode(pinOld, true);
    lv_textarea_set_max_length(pinOld, 8);
    lv_textarea_set_accepted_chars(pinOld, "0123456789");
    lv_obj_set_width(pinOld, lv_pct(100));
    lv_obj_set_height(pinOld, 40);
    lv_obj_add_event_cb(pinOld, cbInputFocus, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(pinOld, cbInputFocus, LV_EVENT_DEFOCUSED, this);

    sectionLabel(body, "Новый PIN (4-8 цифр)");
    pinNew1 = lv_textarea_create(body);
    HorizonTheme::styleField(pinNew1);
    lv_textarea_set_one_line(pinNew1, true);
    lv_textarea_set_password_mode(pinNew1, true);
    lv_textarea_set_max_length(pinNew1, 8);
    lv_textarea_set_accepted_chars(pinNew1, "0123456789");
    lv_obj_set_width(pinNew1, lv_pct(100));
    lv_obj_set_height(pinNew1, 40);
    lv_obj_add_event_cb(pinNew1, cbInputFocus, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(pinNew1, cbInputFocus, LV_EVENT_DEFOCUSED, this);

    sectionLabel(body, "Новый PIN ещё раз");
    pinNew2 = lv_textarea_create(body);
    HorizonTheme::styleField(pinNew2);
    lv_textarea_set_one_line(pinNew2, true);
    lv_textarea_set_password_mode(pinNew2, true);
    lv_textarea_set_max_length(pinNew2, 8);
    lv_textarea_set_accepted_chars(pinNew2, "0123456789");
    lv_obj_set_width(pinNew2, lv_pct(100));
    lv_obj_set_height(pinNew2, 40);
    lv_obj_add_event_cb(pinNew2, cbInputFocus, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(pinNew2, cbInputFocus, LV_EVENT_DEFOCUSED, this);

    lv_obj_t *hint = lv_label_create(body);
    lv_label_set_text(hint, "Заводской PIN: 0000.\nНе забудь новый PIN!");
    lv_obj_set_style_text_font(hint, HorizonTheme::fontSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(hint, hzColor(HorizonTheme::current().textDim), LV_PART_MAIN);

    lv_obj_t *apply = lv_btn_create(scr);
    HorizonTheme::styleButton(apply, true);
    lv_obj_set_size(apply, lv_pct(70), 40);
    lv_obj_align(apply, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_t *al = lv_label_create(apply);
    lv_label_set_text(al, "Сменить PIN");
    lv_obj_center(al);
    lv_obj_add_event_cb(apply, cbSetApplyPin, LV_EVENT_CLICKED, this);
}

void HorizonUI::cbSetApplyPin(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    const char *o = self->pinOld ? lv_textarea_get_text(self->pinOld) : "";
    const char *n1 = self->pinNew1 ? lv_textarea_get_text(self->pinNew1) : "";
    const char *n2 = self->pinNew2 ? lv_textarea_get_text(self->pinNew2) : "";
    HorizonConfig &cfg = HorizonConfig::instance();
    if (!cfg.checkPin(o)) {
        self->toast("Старый PIN неверный");
        return;
    }
    size_t len = strlen(n1);
    if (len < 4 || len > 8) {
        self->toast("PIN: 4-8 цифр");
        return;
    }
    for (size_t i = 0; i < len; i++) {
        if (!isdigit((unsigned char)n1[i])) {
            self->toast("PIN: только цифры");
            return;
        }
    }
    if (strcmp(n1, n2)) {
        self->toast("PIN не совпадают");
        return;
    }
    cfg.setPin(n1);
    cfg.save();
    self->toast("PIN изменён");
    self->showSettings();
}

// ---------------- настройки: сброс ----------------

void HorizonUI::buildSetReset(void)
{
    clearScreen(Screen::SET_RESET);
    addStatusBar(scr);
    addNavHeader(scr, "Сброс", "Осторожно: данные удалятся");

    lv_obj_t *body = lv_obj_create(scr);
    lv_obj_set_pos(body, 0, kStatusH + kHeaderH + 2);
    lv_obj_set_size(body, lv_pct(100), H() - (kStatusH + kHeaderH + 2) - 2);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(body, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(body, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(body, 8, LV_PART_MAIN);

    struct ResetRow {
        const char *t;
        const char *s;
    };
    static const ResetRow rows[] = {
        {"Сбросить настройки ПДА", "Досье, каналы, тема, PIN=0000"},
        {"Сбросить NodeDB узла", "Забыть все узлы сети"},
        {"Заводской сброс узла", "Полный сброс радиомодуля (мастер)"},
    };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = lv_btn_create(body);
        HorizonTheme::styleDangerButton(b);
        lv_obj_set_width(b, lv_pct(100));
        lv_obj_set_height(b, LV_SIZE_CONTENT);
        lv_obj_set_style_min_height(b, 58, LV_PART_MAIN);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(b, 8, LV_PART_MAIN);
        lv_obj_t *t = lv_label_create(b);
        lv_label_set_text(t, rows[i].t);
        lv_obj_t *s = lv_label_create(b);
        lv_label_set_text(s, rows[i].s);
        lv_obj_set_style_text_font(s, HorizonTheme::fontSmall(), LV_PART_MAIN);
        lv_obj_set_user_data(b, (void *)(intptr_t)i);
        lv_obj_add_event_cb(b, cbSetReset, LV_EVENT_CLICKED, this);
    }
}

void HorizonUI::cbSetReset(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    int id = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t *)lv_event_get_target(e));
    if (id == 0) {
        self->askConfirm("Сбросить все настройки ПДА к заводским?", "Сбросить", [self]() {
            uint8_t oldOrient = HorizonConfig::instance().orientation;
            HorizonConfig::instance().resetToDefaults();
            HorizonConfig::instance().save();
            DisplayDriver *dd = self->view->getDisplayDriver();
            if (dd) {
                dd->setBrightness(HorizonConfig::instance().brightness);
                dd->setScreenTimeout(HorizonConfig::instance().timeoutSec);
            }
            if (oldOrient != 0) {
                self->askConfirm("Нужна перезагрузка ПДА. Перезагрузить?", "Да", [self]() {
#if defined(ARCH_ESP32)
                    delay(300);
                    ESP.restart();
#endif
                });
                return;
            }
            self->toast("ПДА сброшен");
            self->showMenu();
        });
    } else if (id == 1) {
        self->askConfirm("Забыть все узлы сети (NodeDB)?", "Сбросить", [self]() {
            self->view->resetNodeDb();
            self->toast("Команда отправлена");
        });
    } else if (id == 2) {
        self->askPin("Мастер-PIN для сброса:", [self]() {
            self->askConfirm("ПОЛНЫЙ сброс радиомодуля? Всё сотрётся!", "Сбросить", [self]() {
                self->view->factoryResetNode();
                self->toast("Команда отправлена");
            });
        });
    }
}

// ---------------- настройки: о ПДА ----------------

void HorizonUI::buildSetAbout(void)
{
    clearScreen(Screen::SET_ABOUT);
    const HorizonPalette &p = HorizonTheme::current();
    addStatusBar(scr);
    addNavHeader(scr, "О ПДА", kAppName);

    lv_obj_t *body = lv_obj_create(scr);
    lv_obj_set_pos(body, 0, kStatusH + kHeaderH + 2);
    lv_obj_set_size(body, lv_pct(100), H() - (kStatusH + kHeaderH + 2) - 2);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(body, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(body, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(body, 4, LV_PART_MAIN);

    HorizonConfig &cfg = HorizonConfig::instance();
    char lines[9][96];
    snprintf(lines[0], sizeof(lines[0]), "ПДА «Горизонт», прошивка %s", kFirmwareVersion);
    snprintf(lines[1], sizeof(lines[1]), "Плата: Elecrow CrowPanel Advance 3.5\"");
    if (view->getMyNodeNum())
        snprintf(lines[2], sizeof(lines[2]), "Узел: !%08x (%s)", (unsigned)view->getMyNodeNum(), cfg.callsign);
    else
        snprintf(lines[2], sizeof(lines[2]), "Узел: неизвестен - радио молчит");
    snprintf(lines[3], sizeof(lines[3]), "Узлов в базе: %u, в сети: %u", (unsigned)view->nodeCount(),
             (unsigned)view->onlineCount());
    snprintf(lines[4], sizeof(lines[4]), "Чаты: общий %u, группа %u, инфо %u", cfg.generalCh, cfg.groupCh,
             cfg.announceCh);
    // Эфир словами: регион UNSET (0) в свежей прошивке радиомодуля глушит
    // приём и передачу целиком - устройства рядом друг друга не услышат.
    bool airBad = true;
    if (view->hasLoRaConfig()) {
        int rg = (int)view->loraConfig().region;
        int pr = (int)view->loraConfig().modem_preset;
        const char *rname = nullptr;
        const char *pname = nullptr;
        for (uint8_t i = 0; i < kRegionCount; i++) {
            if (kRegions[i].code == rg)
                rname = kRegions[i].name;
        }
        for (uint8_t i = 0; i < kPresetCount; i++) {
            if (kPresets[i].code == pr)
                pname = kPresets[i].name;
        }
        if (rg == 0)
            snprintf(lines[5], sizeof(lines[5]), "ЭФИР ВЫКЛ: регион LoRa не задан!");
        else if (!view->loraConfig().tx_enabled)
            snprintf(lines[5], sizeof(lines[5]), "Эфир: %s / %s, только приём", rname ? rname : "?",
                     pname ? pname : "?");
        else {
            snprintf(lines[5], sizeof(lines[5]), "Эфир: %s / %s, хопы %u", rname ? rname : "?",
                     pname ? pname : "?", (unsigned)view->loraConfig().hop_limit);
            airBad = false;
        }
    } else
        snprintf(lines[5], sizeof(lines[5]), "Эфир: нет данных от радиомодуля");
    snprintf(lines[6], sizeof(lines[6]), "UART от радио: %u байт / %u кадров", (unsigned)hzUartRxBytes,
             (unsigned)hzUartRxFrames);
    snprintf(lines[7], sizeof(lines[7]), "Связь с узлом: %s", view->radioConnected() ? "да" : "нет");
    snprintf(lines[8], sizeof(lines[8]), "Сделано для полевых игр по мотивам S.T.A.L.K.E.R.");

    for (int i = 0; i < 9; i++) {
        lv_obj_t *l = lv_label_create(body);
        lv_label_set_text(l, lines[i]);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(l, lv_pct(100));
        if (i == 0)
            lv_obj_set_style_text_color(l, hzColor(p.accent), LV_PART_MAIN);
        else if (i == 5 && airBad)
            lv_obj_set_style_text_color(l, hzColor(p.danger), LV_PART_MAIN);
        else
            lv_obj_set_style_text_color(l, hzColor(p.textDim), LV_PART_MAIN);
        lv_obj_set_style_text_font(l, HorizonTheme::fontSmall(), LV_PART_MAIN);
    }
}

#endif // HORIZON_UI
