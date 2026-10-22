#include "horizon_target.h" // целевые флаги сборки (вкус: standalone/прошивка)
// ПДА «Горизонт» - ядро интерфейса: каркас, заставка, меню, тосты, PIN, клавиатура.
#ifdef HORIZON_UI

#include "horizon/HorizonUI.h"
#include "horizon/HorizonConfig.h"
#include "horizon/HorizonLogo.h"
#include "horizon/HorizonStrings.h"
#include "horizon/HorizonTheme.h"
#include "horizon/HorizonView.h"
#include "util/ILog.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(ARCH_ESP32) || defined(ARDUINO_ARCH_ESP32)
#include "Arduino.h"
#endif

using namespace HorizonStrings;

namespace {
constexpr int16_t kStatusH = 22;
constexpr int16_t kHeaderH = 40;
constexpr int16_t kInputH = 48;

uint32_t nowMs(void)
{
    return lv_tick_get();
}
} // namespace

HorizonUI::HorizonUI(HorizonView *v) : view(v)
{
}

int16_t HorizonUI::W(void)
{
    lv_display_t *d = lv_display_get_default();
    return d ? (int16_t)lv_display_get_horizontal_resolution(d) : 480;
}

int16_t HorizonUI::H(void)
{
    lv_display_t *d = lv_display_get_default();
    return d ? (int16_t)lv_display_get_vertical_resolution(d) : 320;
}

bool HorizonUI::isPortrait(void)
{
    return H() > W();
}

// ---------------- каркас ----------------

void HorizonUI::init(void)
{
    lastTickMs = nowMs();
    lastSlowMs = lastTickMs;
    showBoot();
}

void HorizonUI::clearScreen(Screen s)
{
    // Закрыть всё временное
    closeModal();
    if (toastBtn) {
        lv_obj_delete(toastBtn);
        toastBtn = nullptr;
        toastLabel = nullptr;
    }
    kbd = nullptr;
    kbdMatrix = nullptr;
    kbdTarget = nullptr;
    stTime = stOnline = stUnread = stBatt = stLink = nullptr;
    bootBar = nullptr;
    bootStatus = nullptr;
    chatList = nullptr;
    chatInput = nullptr;
    chatSub = nullptr;
    chatSendBtn = nullptr;
    contactsList = nullptr;
    contactsCount = nullptr;
    editName = editCall = editRank = editGroup = nullptr;
    setDetailCh = setDetailName = setDetailKey = nullptr;
    setRegion = setPreset = setHops = setRole = setTx = nullptr;
    setBrightness = setBrightnessVal = setTimeout = setOrientation = setTheme = nullptr;
    pinOld = pinNew1 = pinNew2 = nullptr;
    timeHVal = timeMVal = timeCur = nullptr;
    detList = detSub = nullptr;
    scanBig = scanPct = scanProg = nullptr;
    routeStatus = routeBox = nullptr;
    statsList = statsSub = nullptr;
    logList = nullptr;
    for (int i = 0; i < 7; i++)
        menuBadges[i] = nullptr;
    kbdShift = false;
    // Охранник сна живёт на lv_layer_top и смену экрана не переживает.
    hideSleepGuard();
    kbdSuppressClick = false;

    current = s;
    lv_obj_t *old = scr;
    scr = lv_obj_create(nullptr);
    HorizonTheme::styleScreen(scr);
    lv_obj_set_size(scr, lv_pct(100), lv_pct(100));
    lv_scr_load(scr);
    if (old)
        lv_obj_delete(old);
}

lv_obj_t *HorizonUI::addStatusBar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    HorizonTheme::styleHeader(bar);
    lv_obj_set_size(bar, lv_pct(100), kStatusH);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(bar, 6, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);

    const lv_font_t *f = HorizonTheme::fontSmall();
    stTime = lv_label_create(bar);
    lv_label_set_text(stTime, "--:--");
    lv_obj_set_style_text_font(stTime, f, LV_PART_MAIN);
    stOnline = lv_label_create(bar);
    lv_label_set_text(stOnline, "");
    lv_obj_set_style_text_font(stOnline, f, LV_PART_MAIN);
    stUnread = lv_label_create(bar);
    lv_obj_set_style_text_font(stUnread, f, LV_PART_MAIN);
    stBatt = lv_label_create(bar);
    lv_obj_set_style_text_font(stBatt, f, LV_PART_MAIN);
    stLink = lv_label_create(bar);
    lv_obj_set_style_text_font(stLink, f, LV_PART_MAIN);
    refreshStatus();
    return bar;
}

void HorizonUI::refreshStatus(void)
{
    if (!stTime)
        return;
    char buf[48];
    // время
    fmtTime(view->getActTime(), buf, sizeof(buf));
    // Тильда: время восстановлено из файла и ещё не уточнено по сети.
    lv_label_set_text_fmt(stTime, view->timeUncertain() ? "%s ~%s" : "%s %s", kGlyphClock, buf);
    // онлайн
    lv_label_set_text_fmt(stOnline, "В сети: %u", (unsigned)view->onlineCount());
    // непрочитанные (только привязанные чаты + лички)
    uint32_t u = visibleUnread();
    if (u > 0)
        lv_label_set_text_fmt(stUnread, "%s %u", kGlyphMail, (unsigned)u);
    else
        lv_label_set_text(stUnread, "");
    // батарея
    int b = view->myBattery();
    if (b >= 0)
        lv_label_set_text_fmt(stBatt, "%d%%", b);
    else
        lv_label_set_text(stBatt, "--%");
    // связь
    const HorizonPalette &p = HorizonTheme::current();
    if (view->radioConnected()) {
        lv_label_set_text(stLink, "MESH");
        lv_obj_set_style_text_color(stLink, hzColor(p.ok), LV_PART_MAIN);
    } else {
        lv_label_set_text(stLink, "OFFLINE");
        lv_obj_set_style_text_color(stLink, hzColor(p.danger), LV_PART_MAIN);
    }
}

lv_obj_t *HorizonUI::addNavHeader(lv_obj_t *parent, const char *title, const char *sub)
{
    lv_obj_t *hdr = lv_obj_create(parent);
    HorizonTheme::styleHeader(hdr);
    lv_obj_set_size(hdr, lv_pct(100), kHeaderH);
    lv_obj_set_pos(hdr, 0, kStatusH);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = lv_btn_create(hdr);
    HorizonTheme::styleButton(back, true);
    lv_obj_set_size(back, 64, 30);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, "<");
    lv_obj_set_style_text_font(bl, HorizonTheme::fontBig(), LV_PART_MAIN);
    lv_obj_center(bl);
    lv_obj_add_event_cb(back, cbBack, LV_EVENT_CLICKED, this);

    lv_obj_t *t = lv_label_create(hdr);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, HorizonTheme::fontMedium(), LV_PART_MAIN);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 76, sub ? -8 : 0);

    if (sub) {
        chatSub = lv_label_create(hdr);
        lv_label_set_text(chatSub, sub);
        lv_obj_set_style_text_font(chatSub, HorizonTheme::fontSmall(), LV_PART_MAIN);
        lv_obj_set_style_text_color(chatSub, hzColor(HorizonTheme::current().textDim), LV_PART_MAIN);
        lv_obj_align(chatSub, LV_ALIGN_LEFT_MID, 76, 10);
    } else {
        chatSub = nullptr;
    }
    return hdr;
}

lv_obj_t *HorizonUI::addFooterHint(lv_obj_t *parent, const char *text)
{
    lv_obj_t *f = lv_label_create(parent);
    lv_label_set_text(f, text);
    lv_obj_set_style_text_font(f, HorizonTheme::fontSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(f, hzColor(HorizonTheme::current().textDim), LV_PART_MAIN);
    lv_obj_set_width(f, lv_pct(100));
    lv_obj_set_style_text_align(f, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(f, LV_ALIGN_BOTTOM_MID, 0, -4);
    return f;
}

// ---------------- тосты ----------------

void HorizonUI::toast(const char *fmt, ...)
{
    char buf[220];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (!toastBtn || !lv_obj_is_valid(toastBtn)) {
        toastBtn = lv_btn_create(lv_layer_top());
        HorizonTheme::styleButton(toastBtn, true);
        lv_obj_set_width(toastBtn, lv_pct(92));
        lv_obj_set_height(toastBtn, LV_SIZE_CONTENT);
        lv_obj_set_style_min_height(toastBtn, 34, LV_PART_MAIN);
        lv_obj_align(toastBtn, LV_ALIGN_BOTTOM_MID, 0, -8);
        toastLabel = lv_label_create(toastBtn);
        lv_label_set_long_mode(toastLabel, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(toastLabel, lv_pct(100));
        lv_obj_set_style_text_align(toastLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_add_event_cb(toastBtn, cbToast, LV_EVENT_CLICKED, this);
        lv_obj_move_foreground(toastBtn);
    }
    toastHasTarget = false;
    lv_label_set_text(toastLabel, buf);
    lv_obj_remove_flag(toastBtn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(toastBtn);
    toastUntil = nowMs() + 2300;
}

void HorizonUI::cbToast(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    if (self->toastHasTarget) {
        bool isCh = self->toastIsChannel;
        uint8_t ch = self->toastCh;
        uint32_t peer = self->toastPeer;
        self->toastHasTarget = false;
        if (self->toastBtn)
            lv_obj_add_flag(self->toastBtn, LV_OBJ_FLAG_HIDDEN);
        if (isCh)
            self->showChatChannel(ch);
        else
            self->showChatDm(peer);
    } else if (self->toastBtn) {
        lv_obj_add_flag(self->toastBtn, LV_OBJ_FLAG_HIDDEN);
    }
}



// ---------------- модальные окна ----------------

void HorizonUI::closeModal(void)
{
    if (modal) {
        if (lv_obj_is_valid(modal))
            lv_obj_delete(modal);
        modal = nullptr;
    }
    pinDots = nullptr;
    pinOk = nullptr;
    confirmOk = nullptr;
}

void HorizonUI::askPin(const char *prompt, std::function<void(void)> onOk)
{
    closeModal();
    pinEntry.clear();
    pinOk = onOk;
    const HorizonPalette &p = HorizonTheme::current();

    modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(modal, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(modal, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(modal, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(modal, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(modal, 0, LV_PART_MAIN);

    int32_t pw = isPortrait() ? lv_pct(86) : 300;
    lv_obj_t *panel = lv_obj_create(modal);
    HorizonTheme::stylePanel(panel);
    lv_obj_set_size(panel, pw, LV_SIZE_CONTENT);
    lv_obj_center(panel);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(panel, 10, LV_PART_MAIN);

    lv_obj_t *t = lv_label_create(panel);
    lv_label_set_text(t, prompt);
    lv_obj_set_style_text_font(t, HorizonTheme::fontMedium(), LV_PART_MAIN);

    pinDots = lv_label_create(panel);
    lv_label_set_text(pinDots, "- - - -");
    lv_obj_set_style_text_font(pinDots, HorizonTheme::fontBig(), LV_PART_MAIN);
    lv_obj_set_style_text_color(pinDots, hzColor(p.accent), LV_PART_MAIN);

    static const char *keys[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "C", "0", "<"};
    lv_obj_t *grid = lv_obj_create(panel);
    lv_obj_set_size(grid, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(grid, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(grid, 0, LV_PART_MAIN);
    static lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {44, 44, 44, 44, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    lv_obj_set_style_grid_column_dsc_array(grid, col_dsc, LV_PART_MAIN);
    lv_obj_set_style_grid_row_dsc_array(grid, row_dsc, LV_PART_MAIN);
    lv_obj_set_style_pad_column(grid, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_row(grid, 6, LV_PART_MAIN);
    for (int i = 0; i < 12; i++) {
        lv_obj_t *b = lv_btn_create(grid);
        HorizonTheme::styleButton(b, false);
        lv_obj_set_grid_cell(b, LV_GRID_ALIGN_STRETCH, i % 3, 1, LV_GRID_ALIGN_STRETCH, i / 3, 1);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, keys[i]);
        lv_obj_set_style_text_font(l, HorizonTheme::fontMedium(), LV_PART_MAIN);
        lv_obj_center(l);
        lv_obj_set_user_data(b, (void *)keys[i]);
        lv_obj_add_event_cb(b, cbPinBtn, LV_EVENT_CLICKED, this);
    }

    lv_obj_t *row = lv_obj_create(panel);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *cancel = lv_btn_create(row);
    HorizonTheme::styleButton(cancel, false);
    lv_obj_set_size(cancel, 110, 40);
    lv_obj_t *cl = lv_label_create(cancel);
    lv_label_set_text(cl, "Отмена");
    lv_obj_center(cl);
    lv_obj_add_event_cb(cancel, cbConfirmCancel, LV_EVENT_CLICKED, this);

    lv_obj_t *ok = lv_btn_create(row);
    HorizonTheme::styleButton(ok, true);
    lv_obj_set_size(ok, 110, 40);
    lv_obj_t *ol = lv_label_create(ok);
    lv_label_set_text(ol, "OK");
    lv_obj_center(ol);
    lv_obj_set_user_data(ok, (void *)"OK");
    lv_obj_add_event_cb(ok, cbPinBtn, LV_EVENT_CLICKED, this);
}

void HorizonUI::pinPress(const char *key)
{
    if (!key)
        return;
    if (!strcmp(key, "C")) {
        pinEntry.clear();
    } else if (!strcmp(key, "<")) {
        if (!pinEntry.empty())
            pinEntry.pop_back();
    } else if (!strcmp(key, "OK")) {
        if (HorizonConfig::instance().checkPin(pinEntry.c_str())) {
            auto cb = pinOk;
            closeModal();
            if (cb)
                cb();
        } else {
            pinEntry.clear();
            toast("Неверный PIN");
        }
    } else if (pinEntry.size() < 8) {
        pinEntry += key;
    }
    if (pinDots && lv_obj_is_valid(pinDots)) {
        char dots[32] = "";
        for (size_t i = 0; i < 8; i++)
            strcat(dots, i < pinEntry.size() ? "* " : "- ");
        lv_label_set_text(pinDots, dots);
    }
}

void HorizonUI::cbPinBtn(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    const char *key = (const char *)lv_obj_get_user_data((lv_obj_t *)lv_event_get_target(e));
    self->pinPress(key);
}

void HorizonUI::askConfirm(const char *text, const char *okLabel, std::function<void(void)> onOk,
                    std::function<void(void)> onCancel)
{
    closeModal();
    confirmOk = onOk;
    confirmCancel = onCancel;

    modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(modal, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(modal, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(modal, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(modal, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(modal, 0, LV_PART_MAIN);

    int32_t pw = isPortrait() ? lv_pct(86) : 330;
    lv_obj_t *panel = lv_obj_create(modal);
    HorizonTheme::stylePanel(panel);
    lv_obj_set_size(panel, pw, LV_SIZE_CONTENT);
    lv_obj_center(panel);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(panel, 12, LV_PART_MAIN);

    lv_obj_t *t = lv_label_create(panel);
    lv_label_set_text(t, text);
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(t, lv_pct(100));
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_t *row = lv_obj_create(panel);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *cancel = lv_btn_create(row);
    HorizonTheme::styleButton(cancel, false);
    lv_obj_set_size(cancel, 110, 40);
    lv_obj_t *cl = lv_label_create(cancel);
    lv_label_set_text(cl, "Отмена");
    lv_obj_center(cl);
    lv_obj_add_event_cb(cancel, cbConfirmCancel, LV_EVENT_CLICKED, this);

    lv_obj_t *ok = lv_btn_create(row);
    HorizonTheme::styleButton(ok, true);
    lv_obj_set_size(ok, 110, 40);
    lv_obj_t *ol = lv_label_create(ok);
    lv_label_set_text(ol, okLabel);
    lv_obj_center(ol);
    lv_obj_add_event_cb(ok, cbConfirmOk, LV_EVENT_CLICKED, this);
}

void HorizonUI::cbConfirmOk(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    auto cb = self->confirmOk;
    self->closeModal();
    if (cb)
        cb();
}

void HorizonUI::cbConfirmCancel(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    auto cb = self->confirmCancel;
    self->confirmCancel = nullptr;
    self->closeModal();
    if (cb)
        cb();
}

void HorizonUI::cbModalBlock(lv_event_t *e)
{
    (void)e;
}

// ---------------- клавиатура ----------------

namespace {
// Раскладки: ровно kKbdCols*kKbdRows ключей; "" - пустая клетка.
// Широкий пробел пишется дубликатами подряд и склеивается (см. kbdSetLayout).
// "Aa" - Shift разового действия (следующий символ - из Shift-карты),
// "<<" - стереть символ. RU - стандартный ЙЦУКЕН; "э" и "х" живут в нижнем
// ряду (в 10 колонок полный верхний ряд не влез).
const char *const kKbdEn[] = {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p", //
                              "a", "s", "d", "f", "g", "h", "j", "k", "l", "",  //
                              "Aa", "z", "x", "c", "v", "b", "n", "m", "'", "-", //
                              "123", "RU", " ", " ", " ", " ", ",", ".", "OK", "<<"};
const char *const kKbdEnShift[] = {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", //
                                   "A", "S", "D", "F", "G", "H", "J", "K", "L", "",  //
                                   "Aa", "Z", "X", "C", "V", "B", "N", "M", "\"", "_", //
                                   "123", "RU", " ", " ", " ", " ", ";", ":", "OK", "<<"};
const char *const kKbdRu[] = {"й", "ц", "у", "к", "е", "н", "г", "ш", "щ", "з", //
                              "ф", "ы", "в", "а", "п", "р", "о", "л", "д", "ж", //
                              "Aa", "я", "ч", "с", "м", "и", "т", "ь", "б", "ю", //
                              "123", "EN", " ", " ", "э", "х", ",", ".", "OK", "<<"};
const char *const kKbdRuShift[] = {"Й", "Ц", "У", "К", "Е", "Н", "Г", "Ш", "Щ", "З", //
                                   "Ф", "Ы", "В", "А", "П", "Р", "О", "Л", "Д", "Ж", //
                                   "Aa", "Я", "Ч", "С", "М", "И", "Т", "Ь", "Б", "Ю", //
                                   "123", "EN", " ", " ", "Э", "Х", ";", ":", "OK", "<<"};
const char *const kKbdNum[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", //
                               "-", "/", ":", ";", "(", ")", "+", "=", "", "<<", //
                               "АБВ", ",", "?", "!", ".", "@", "#", "%", "'", "", //
                               "", " ", " ", " ", " ", " ", " ", " ", "OK", ""};
constexpr int kKbdCols = 10;
constexpr int kKbdRows = 4;
static_assert(sizeof(kKbdEn) / sizeof(kKbdEn[0]) == (size_t)(kKbdCols * kKbdRows), "EN map size");
static_assert(sizeof(kKbdEnShift) / sizeof(kKbdEnShift[0]) == (size_t)(kKbdCols * kKbdRows),
              "EN-shift map size");
static_assert(sizeof(kKbdRu) / sizeof(kKbdRu[0]) == (size_t)(kKbdCols * kKbdRows), "RU map size");
static_assert(sizeof(kKbdRuShift) / sizeof(kKbdRuShift[0]) == (size_t)(kKbdCols * kKbdRows),
              "RU-shift map size");
static_assert(sizeof(kKbdNum) / sizeof(kKbdNum[0]) == (size_t)(kKbdCols * kKbdRows), "NUM map size");
// Долгое нажатие: альтернативная буква (ё/ъ не влезли в сетку 10x4).
const char *kbdLongAlt(const char *key)
{
    if (!strcmp(key, "е"))
        return "ё";
    if (!strcmp(key, "Е"))
        return "Ё";
    if (!strcmp(key, "ь"))
        return "ъ";
    if (!strcmp(key, "Ь"))
        return "Ъ";
    return nullptr;
}
} // namespace

void HorizonUI::kbdShow(lv_obj_t *ta)
{
    kbdTarget = ta;
    if (kbd && lv_obj_is_valid(kbd)) {
        lv_obj_remove_flag(kbd, LV_OBJ_FLAG_HIDDEN);
    } else {
        kbd = lv_obj_create(scr);
        HorizonTheme::styleKeyboard(kbd);
        // Тапы по клавиатуре не должны снимать фокус с поля ввода: иначе
        // DEFOCUSED закроет клавиатуру раньше, чем обработается нажатие.
        lv_obj_remove_flag(kbd, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        int16_t kh = isPortrait() ? 190 : 148;
        lv_obj_set_size(kbd, lv_pct(100), kh);
        // В чате - над строкой ввода, иначе - у нижнего края
        if (current == Screen::CHAT)
            lv_obj_align(kbd, LV_ALIGN_BOTTOM_MID, 0, -kInputH);
        else
            lv_obj_align(kbd, LV_ALIGN_BOTTOM_MID, 0, 0);
        kbdSetLayout(kbdLayout);
    }
    // В чате сжать список сообщений
    if (current == Screen::CHAT && chatList && lv_obj_is_valid(chatList)) {
        int16_t kh = (int16_t)lv_obj_get_height(kbd);
        int16_t top = kStatusH + kHeaderH + 2;
        lv_obj_set_pos(chatList, 0, top);
        lv_obj_set_size(chatList, lv_pct(100), H() - top - kInputH - kh - 4);
    }
}

void HorizonUI::kbdHide(void)
{
    if (kbd && lv_obj_is_valid(kbd))
        lv_obj_add_flag(kbd, LV_OBJ_FLAG_HIDDEN);
    kbdTarget = nullptr;
    if (current == Screen::CHAT && chatList && lv_obj_is_valid(chatList)) {
        int16_t top = kStatusH + kHeaderH + 2;
        lv_obj_set_pos(chatList, 0, top);
        lv_obj_set_size(chatList, lv_pct(100), H() - top - kInputH - 4);
    }
}

bool HorizonUI::kbdVisible(void) const
{
    return kbd && lv_obj_is_valid(kbd) && !lv_obj_has_flag(kbd, LV_OBJ_FLAG_HIDDEN);
}

void HorizonUI::kbdSetLayout(uint8_t layout)
{
    kbdLayout = layout % 3;
    if (!kbd || !lv_obj_is_valid(kbd))
        return;
    lv_obj_clean(kbd);
    const char *const *map = kKbdEn;
    if (kbdLayout == 1)
        map = kbdShift ? kKbdRuShift : kKbdRu;
    else if (kbdLayout == 2)
        map = kKbdNum; // у цифр Shift нет
    else if (kbdShift)
        map = kKbdEnShift;

    static lv_coord_t col_dsc[kKbdCols + 1];
    static lv_coord_t row_dsc[kKbdRows + 1];
    for (int i = 0; i < kKbdCols; i++)
        col_dsc[i] = LV_GRID_FR(1);
    col_dsc[kKbdCols] = LV_GRID_TEMPLATE_LAST;
    for (int i = 0; i < kKbdRows; i++)
        row_dsc[i] = LV_GRID_FR(1);
    row_dsc[kKbdRows] = LV_GRID_TEMPLATE_LAST;

    lv_obj_set_layout(kbd, LV_LAYOUT_GRID);
    lv_obj_set_style_grid_column_dsc_array(kbd, col_dsc, LV_PART_MAIN);
    lv_obj_set_style_grid_row_dsc_array(kbd, row_dsc, LV_PART_MAIN);

    // Широкий пробел занимает все подряд идущие дубликаты в строке;
    // повторные вхождения пропускаем.
    bool spaceDone = false;
    int cells = kKbdCols * kKbdRows;
    for (int i = 0; i < cells; i++) {
        const char *key = map[i];
        if (!key || !*key)
            continue;
        bool wide = !strcmp(key, " ");
        if (wide && spaceDone)
            continue;
        int col = i % kKbdCols;
        int row = i / kKbdCols;
        int span = 1;
        if (wide) {
            for (int j = i + 1; j < (row + 1) * kKbdCols && !strcmp(map[j], key); j++)
                span++;
            spaceDone = true;
        }
        lv_obj_t *b = lv_btn_create(kbd);
        // OK всегда акцентная; "Aa" подсвечена, пока Shift взведён.
        bool accent = !strcmp(key, "OK") || (!strcmp(key, "Aa") && kbdShift);
        HorizonTheme::styleButton(b, accent);
        // Клавиши не отбирают фокус у поля ввода (см. kbdShow).
        lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_obj_set_grid_cell(b, LV_GRID_ALIGN_STRETCH, col, span, LV_GRID_ALIGN_STRETCH, row, 1);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, !strcmp(key, " ") ? "ПРОБЕЛ" : key);
        if (wide || !strcmp(key, "123") || !strcmp(key, "АБВ") || !strcmp(key, "EN") || !strcmp(key, "RU") ||
            !strcmp(key, "Aa") || !strcmp(key, "<<"))
            lv_obj_set_style_text_font(l, HorizonTheme::fontSmall(), LV_PART_MAIN);
        lv_obj_center(l);
        lv_obj_set_user_data(b, (void *)key);
        lv_obj_add_event_cb(b, cbKbd, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(b, cbKbdLong, LV_EVENT_LONG_PRESSED, this);
    }
}

void HorizonUI::kbdPress(const char *txt)
{
    if (!txt)
        return;
    if (!strcmp(txt, "RU")) {
        kbdShift = false;
        kbdSetLayout(1);
        return;
    }
    if (!strcmp(txt, "EN")) {
        kbdShift = false;
        kbdSetLayout(0);
        return;
    }
    if (!strcmp(txt, "АБВ")) {
        kbdShift = false;
        kbdSetLayout(1);
        return;
    }
    if (!strcmp(txt, "123")) {
        kbdShift = false;
        kbdSetLayout(2);
        return;
    }
    if (!strcmp(txt, "Aa")) {
        // Shift разового действия (в 123 не работает - там его нет).
        if (kbdLayout != 2) {
            kbdShift = !kbdShift;
            kbdSetLayout(kbdLayout);
        }
        return;
    }
    if (!strcmp(txt, "OK")) {
        kbdHide();
        return;
    }
    if (!kbdTarget || !lv_obj_is_valid(kbdTarget))
        return;
    if (!strcmp(txt, "<<")) {
        lv_textarea_delete_char(kbdTarget);
        return;
    }
    lv_textarea_add_text(kbdTarget, txt);
    if (kbdShift) {
        // Shift разовый: после вставленного символа - назад к строчным.
        kbdShift = false;
        kbdSetLayout(kbdLayout);
    }
}

void HorizonUI::cbKbd(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    if (self->kbdSuppressClick) {
        // Долгое нажатие уже вставило альтернативный символ.
        self->kbdSuppressClick = false;
        return;
    }
    const char *key = (const char *)lv_obj_get_user_data((lv_obj_t *)lv_event_get_target(e));
    self->kbdPress(key);
}

void HorizonUI::cbKbdLong(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    const char *key = (const char *)lv_obj_get_user_data((lv_obj_t *)lv_event_get_target(e));
    const char *alt = key ? kbdLongAlt(key) : nullptr;
    if (alt && self->kbdTarget && lv_obj_is_valid(self->kbdTarget)) {
        lv_textarea_add_text(self->kbdTarget, alt);
        self->kbdSuppressClick = true; // грядущий CLICKED пропустить
    }
}

// ---------------- охранник сна ----------------

void HorizonUI::showSleepGuard(void)
{
    if (sleepGuard && lv_obj_is_valid(sleepGuard))
        return;
    sleepGuard = lv_obj_create(lv_layer_top());
    lv_obj_set_size(sleepGuard, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(sleepGuard, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(sleepGuard, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(sleepGuard, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(sleepGuard, cbSleepWake, LV_EVENT_PRESSED, this);
    lv_obj_move_foreground(sleepGuard);
}

void HorizonUI::hideSleepGuard(void)
{
    if (sleepGuard) {
        if (lv_obj_is_valid(sleepGuard))
            lv_obj_delete(sleepGuard);
        sleepGuard = nullptr;
    }
}

void HorizonUI::cbSleepWake(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    // Удаление на PRESSED гарантирует: отпущенная кнопка уже мертва,
    // CLICKED никуда не уйдёт - нажатие только будит устройство.
    self->hideSleepGuard();
}

// ---------------- заставка ----------------

void HorizonUI::showBoot(void)
{
    clearScreen(Screen::BOOT);
    bootMs = nowMs();
    const HorizonPalette &p = HorizonTheme::current();

    lv_obj_t *box = lv_obj_create(scr);
    lv_obj_set_size(box, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(box, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box, 6, LV_PART_MAIN);

    // Логотип (бинарник собран bin/make_logo.py из assets/horizon-logo.png).
    lv_obj_t *logo = lv_image_create(box);
    lv_image_set_src(logo, &hzLogo);
    // Лого - альфа-маска (A8 без фона): красим в акцент текущей темы.
    lv_obj_set_style_image_recolor(logo, hzColor(p.accent), LV_PART_MAIN);
    lv_obj_set_style_image_recolor_opa(logo, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *name = lv_label_create(box);
    lv_label_set_text(name, kAppName);
    lv_obj_set_style_text_font(name, HorizonTheme::fontBig(), LV_PART_MAIN);
    lv_obj_set_style_text_color(name, hzColor(p.accent), LV_PART_MAIN);

    lv_obj_t *sub = lv_label_create(box);
    lv_label_set_text(sub, kAppSub);
    lv_obj_set_style_text_font(sub, HorizonTheme::fontSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(sub, hzColor(p.textDim), LV_PART_MAIN);

    bootBar = lv_bar_create(box);
    lv_obj_set_size(bootBar, isPortrait() ? 220 : 300, 10);
    lv_bar_set_range(bootBar, 0, 100);
    lv_bar_set_value(bootBar, 5, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bootBar, hzColor(p.panel), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bootBar, hzColor(p.accent), LV_PART_INDICATOR);

    bootStatus = lv_label_create(box);
#ifdef HORIZON_PACKET_LINK
    lv_label_set_text(bootStatus, "Запуск...");
#else
    lv_label_set_text(bootStatus, "Подключение к радиомодулю...");
#endif
    lv_obj_set_style_text_font(bootStatus, HorizonTheme::fontSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(bootStatus, hzColor(p.textDim), LV_PART_MAIN);

    lv_obj_t *ver = lv_label_create(box);
    lv_label_set_text_fmt(ver, "прошивка %s", kFirmwareVersion);
    lv_obj_set_style_text_font(ver, HorizonTheme::fontSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(ver, hzColor(p.textDim), LV_PART_MAIN);
}

// ---------------- меню ----------------

void HorizonUI::showMenu(void)
{
    buildMenu();
}

void HorizonUI::buildMenu(void)
{
    clearScreen(Screen::MENU);
    const HorizonPalette &p = HorizonTheme::current();
    HorizonConfig &cfg = HorizonConfig::instance();

    addStatusBar(scr);

    // Заголовок с позывным
    lv_obj_t *title = lv_obj_create(scr);
    HorizonTheme::styleHeader(title);
    lv_obj_set_size(title, lv_pct(100), 30);
    lv_obj_set_pos(title, 0, kStatusH);
    lv_obj_remove_flag(title, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *tl = lv_label_create(title);
    // Титул: "позывной" // группировка // ранг. Ширина фиксирована,
    // длинный позывной уходит в точки, а не под край экрана.
    lv_label_set_text_fmt(tl, "\"%s\" // %s // %s", cfg.callsign,
                          kGroups[cfg.group < kGroupCount ? cfg.group : 0],
                          kRanks[cfg.rank < kRankCount ? cfg.rank : 0]);
    lv_obj_set_width(tl, W() - 16);
    lv_label_set_long_mode(tl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(tl, HorizonTheme::fontNormal(), LV_PART_MAIN);
    lv_obj_set_style_text_color(tl, hzColor(p.accent), LV_PART_MAIN);
    lv_obj_align(tl, LV_ALIGN_LEFT_MID, 8, 0);

    struct TileDef {
        const char *glyph;
        const char *label;
    };
    // 8 плиток ровно заполняют сетку 4x2 (альбом) и 2x4 (книжная).
    static const TileDef tiles[8] = {
        {kGlyphGeneral, "Общий чат"}, {kGlyphGroup, "Группировка"}, {kGlyphAnnounce, "Объявления"},
        {kGlyphContacts, "Контакты"}, {kGlyphChats, "Открытые чаты"}, {kGlyphDossier, "Досье"},
        {kGlyphSettings, "Настройки"}, {kGlyphMaps, "Карта"},
    };

    int cols = isPortrait() ? 2 : 4;
    int16_t top = kStatusH + 30 + 4;
    int16_t bottom = 20;
    lv_obj_t *grid = lv_obj_create(scr);
    lv_obj_set_pos(grid, 0, top);
    lv_obj_set_size(grid, lv_pct(100), H() - top - bottom);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(grid, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(grid, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_column(grid, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_row(grid, 6, LV_PART_MAIN);

    static lv_coord_t col4[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t col2[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row2[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row4[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    if (isPortrait()) {
        lv_obj_set_style_grid_column_dsc_array(grid, col2, LV_PART_MAIN);
        lv_obj_set_style_grid_row_dsc_array(grid, row4, LV_PART_MAIN);
    } else {
        lv_obj_set_style_grid_column_dsc_array(grid, col4, LV_PART_MAIN);
        lv_obj_set_style_grid_row_dsc_array(grid, row2, LV_PART_MAIN);
    }

    for (int i = 0; i < 8; i++) {
        int col = i % cols;
        int row = i / cols;
        bool locked = (i == 7); // карта - заглушка «Позже»
        lv_obj_t *t = lv_btn_create(grid);
        HorizonTheme::styleTile(t);
        lv_obj_set_grid_cell(t, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, row, 1);
        if (locked)
            lv_obj_set_style_opa(t, LV_OPA_60, LV_PART_MAIN); // приглушить заблокированную
        // акцентная верхняя кромка
        lv_obj_set_style_border_color(t, hzColor(p.accent), LV_PART_MAIN);
        lv_obj_set_style_border_width(t, 3, LV_PART_MAIN);
        lv_obj_set_style_border_side(t, LV_BORDER_SIDE_TOP, LV_PART_MAIN);

        lv_obj_t *g = lv_label_create(t);
        lv_label_set_text(g, tiles[i].glyph);
        // Значки-эмодзи есть только в шрифтах 12/14 (см. HorizonStrings.h).
        lv_obj_set_style_text_font(g, HorizonTheme::fontNormal(), LV_PART_MAIN);
        lv_obj_set_style_text_color(g, hzColor(p.accent), LV_PART_MAIN);
        lv_obj_align(g, LV_ALIGN_CENTER, 0, isPortrait() ? -16 : -12);

        lv_obj_t *l = lv_label_create(t);
        lv_label_set_text(l, tiles[i].label);
        lv_obj_set_style_text_font(l, HorizonTheme::fontNormal(), LV_PART_MAIN);
        lv_obj_align(l, LV_ALIGN_CENTER, 0, isPortrait() ? 18 : 14);
        if (locked) {
            lv_obj_t *later = lv_label_create(t);
            lv_label_set_text(later, "Позже");
            lv_obj_set_style_text_font(later, HorizonTheme::fontSmall(), LV_PART_MAIN);
            lv_obj_set_style_text_color(later, hzColor(p.textDim), LV_PART_MAIN);
            lv_obj_align(later, LV_ALIGN_CENTER, 0, isPortrait() ? 38 : 34);
        }

        // бейдж непрочитанных (только у чатов)
        if (i <= 4) {
            lv_obj_t *b = lv_label_create(t);
            lv_obj_set_style_bg_color(b, hzColor(p.accent), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_text_color(b, hzColor(p.accentText), LV_PART_MAIN);
            lv_obj_set_style_text_font(b, HorizonTheme::fontSmall(), LV_PART_MAIN);
            lv_obj_set_style_radius(b, 9, LV_PART_MAIN);
            lv_obj_set_style_pad_hor(b, 6, LV_PART_MAIN);
            lv_obj_align(b, LV_ALIGN_TOP_RIGHT, -4, 4);
            lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
            menuBadges[i] = b;
        }
        lv_obj_set_user_data(t, (void *)(intptr_t)i);
        lv_obj_add_event_cb(t, cbTile, LV_EVENT_CLICKED, this);
    }

    addFooterHint(scr, kAppName);
    refreshMenuBadges();
    refreshStatus();
}

void HorizonUI::refreshMenuBadges(void)
{
    if (current != Screen::MENU)
        return;
    HorizonConfig &cfg = HorizonConfig::instance();
    HorizonStore &st = view->getStore();
    uint32_t counts[5] = {0};
    const HorizonConvo *c0 = st.findChannelConvo(cfg.generalCh);
    const HorizonConvo *c1 = st.findChannelConvo(cfg.groupCh);
    const HorizonConvo *c2 = st.findChannelConvo(cfg.announceCh);
    // Если два раздела смотрят в один канал - показать счётчик один раз
    counts[0] = c0 ? c0->unread : 0;
    counts[1] = (c1 && cfg.groupCh != cfg.generalCh) ? c1->unread : 0;
    counts[2] = (c2 && cfg.announceCh != cfg.generalCh && cfg.announceCh != cfg.groupCh) ? c2->unread : 0;
    counts[4] = st.unreadDms();
    for (int i = 0; i < 5; i++) {
        if (i == 3)
            continue; // у контактов бейджа нет
        lv_obj_t *b = menuBadges[i];
        if (!b || !lv_obj_is_valid(b))
            continue;
        if (counts[i] > 0) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%u", (unsigned)(counts[i] > 99 ? 99 : counts[i]));
            lv_label_set_text(b, buf);
            lv_obj_remove_flag(b, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void HorizonUI::cbTile(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    int id = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t *)lv_event_get_target(e));
    HorizonConfig &cfg = HorizonConfig::instance();
    switch (id) {
    case 0:
        self->showChatChannel(cfg.generalCh);
        break;
    case 1:
        self->showChatChannel(cfg.groupCh);
        break;
    case 2:
        self->showChatChannel(cfg.announceCh);
        break;
    case 3:
        self->showContacts();
        break;
    case 4:
        self->showOpenChats();
        break;
    case 5:
        self->showDossier();
        break;
    case 6:
        self->openSettingsGated();
        break;
    case 7:
        self->toast("Карта появится позже");
        break;
    default:
        break;
    }
}

void HorizonUI::cbBack(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    // Сохранить яркость/таймаут при выходе с экрана дисплея
    if (self->displayDirty) {
        HorizonConfig::instance().save();
        self->displayDirty = false;
    }
    switch (self->current) {
    case Screen::SET_CHANNEL_DETAIL:
        self->buildSetChannels();
        break;
    case Screen::SET_CHANNELS:
    case Screen::SET_DEVICE:
    case Screen::SET_DISPLAY:
    case Screen::SET_PIN:
    case Screen::SET_RESET:
    case Screen::SET_ABOUT:
    case Screen::SET_TIME:
    case Screen::SET_SOUND:
    case Screen::SET_TOOLS:
        self->showSettings();
        break;
    case Screen::TOOL_SCAN:
        self->view->stopScan();
        self->buildToolPick();
        break;
    case Screen::TOOL_ROUTE:
        self->view->stopTrace();
        self->buildToolPick();
        break;
    case Screen::TOOL_DETECTOR:
    case Screen::TOOL_PICK:
    case Screen::TOOL_STATS:
    case Screen::TOOL_LOG:
        self->buildSetTools();
        break;
    case Screen::DOSSIER_EDIT:
        self->showDossier();
        break;
    default:
        self->showMenu();
        break;
    }
}

// ---------------- входящие сообщения ----------------

uint32_t HorizonUI::visibleUnread(void)
{
    HorizonConfig &cfg = HorizonConfig::instance();
    HorizonStore &st = view->getStore();
    uint32_t u = st.unreadDms();
    const HorizonConvo *c0 = st.findChannelConvo(cfg.generalCh);
    const HorizonConvo *c1 = st.findChannelConvo(cfg.groupCh);
    const HorizonConvo *c2 = st.findChannelConvo(cfg.announceCh);
    if (c0)
        u += c0->unread;
    if (c1 && cfg.groupCh != cfg.generalCh)
        u += c1->unread;
    if (c2 && cfg.announceCh != cfg.generalCh && cfg.announceCh != cfg.groupCh)
        u += c2->unread;
    return u;
}

static bool isMappedChannel(uint8_t ch)
{
    HorizonConfig &cfg = HorizonConfig::instance();
    return ch == cfg.generalCh || ch == cfg.groupCh || ch == cfg.announceCh;
}

void HorizonUI::onMessage(uint32_t from, uint32_t to, uint8_t ch, const char *text, uint32_t msgtime)
{
    bool isBroadcast = (to == 0xFFFFFFFFu);
    // Шум с непривязанных каналов - тихо в хранилище, без тостов
    if (isBroadcast && !isMappedChannel(ch))
        return;
    bool isOpen = false;
    if (current == Screen::CHAT) {
        if (chatIsChannel && isBroadcast && ch == chatCh)
            isOpen = true;
        else if (!chatIsChannel && !isBroadcast && (from == chatPeer || to == chatPeer))
            isOpen = true;
    }
    if (isOpen) {
        appendChatBubble(from, text, msgtime, from == view->getMyNodeNum());
        HorizonStore &st = view->getStore();
        if (chatIsChannel)
            st.markRead(st.channelConvo(chatCh));
        else
            st.markRead(st.dmConvo(chatPeer));
        refreshStatus();
        return;
    }
    // Тост с переходом
    char preview[72];
    copyUtf8(preview, sizeof(preview), text ? text : ""); // не рвать UTF-8 на границе
    // убрать переносы строк
    for (char *p = preview; *p; p++) {
        if (*p == '\n' || *p == '\r')
            *p = ' ';
    }
    const char *who = view->nodeLongName(from);
    if (isBroadcast)
        toast("%s %s [%s]: %s", kGlyphMail, who, view->channelLabel(ch), preview);
    else
        toast("%s %s (лично): %s", kGlyphMail, who, preview);
    toastHasTarget = true;
    toastIsChannel = isBroadcast;
    toastCh = ch;
    toastPeer = (from == view->getMyNodeNum()) ? to : from;
    refreshStatus();
    refreshMenuBadges();
}

// ---------------- tick ----------------

void HorizonUI::tick(void)
{
    uint32_t now = nowMs();
    if (now - lastTickMs < 200)
        return;
    lastTickMs = now;

    // скрыть тост
    if (toastBtn && lv_obj_is_valid(toastBtn) && !lv_obj_has_flag(toastBtn, LV_OBJ_FLAG_HIDDEN)) {
        if ((int32_t)(now - toastUntil) >= 0)
            lv_obj_add_flag(toastBtn, LV_OBJ_FLAG_HIDDEN);
    }

    if (current == Screen::BOOT) {
        if (bootBar && lv_obj_is_valid(bootBar)) {
            int v = (int)((now - bootMs) / 40) % 100;
            lv_bar_set_value(bootBar, v, LV_ANIM_OFF);
        }
        if (bootStatus && lv_obj_is_valid(bootStatus)) {
            if (view->radioConnected())
                lv_label_set_text(bootStatus, "Загрузка данных...");
            else if (now - bootMs > 6000)
#ifdef HORIZON_PACKET_LINK
                lv_label_set_text(bootStatus, "Ожидание mesh...");
#else
                lv_label_set_text(bootStatus, "Ожидание радиомодуля...");
#endif
        }
        if (now - bootMs > 1500 && view->bootReady())
            showMenu();
        return;
    }

    // Охранник сна: показывать поверх всего, когда дисплей уснул по таймауту.
    // Первое нажатие попадёт в него (и разбудит подсветку), а не в меню.
    {
        uint16_t t = HorizonConfig::instance().timeoutSec;
        if (t > 0) {
            uint32_t inact = lv_display_get_inactive_time(lv_display_get_default());
            if (inact >= (uint32_t)t * 1000)
                showSleepGuard();
            else
                hideSleepGuard();
        } else {
            hideSleepGuard();
        }
    }

    // медленные обновления раз в секунду
    if (now - lastSlowMs >= 1000) {
        lastSlowMs = now;
        refreshStatus();
        refreshMenuBadges();
        view->pollTools(now);
        refreshTools();
        if (now - lastTimeSaveMs >= 15000) {
            lastTimeSaveMs = now;
            view->persistTime(); // часы переживут перезагрузку (давность не хуже 15 с)
        }
        if (current == Screen::SET_TIME && timeCur && lv_obj_is_valid(timeCur)) {
            char tb[16];
            fmtTime(view->getActTime(), tb, sizeof(tb));
            lv_label_set_text_fmt(timeCur, "Сейчас: %s", tb);
        }
        if (view->takeMsgsDirty()) {
            if (current == Screen::OPENCHATS)
                buildOpenChats();
            else if (current == Screen::CHAT)
                rebuildChatList();
            refreshMenuBadges();
        }
        if (view->takeNodesDirty() || view->takeChannelsDirty()) {
            if (current == Screen::CONTACTS && now - lastContactsMs >= 3000) {
                lastContactsMs = now;
                rebuildContactsList();
            }
            else if (current == Screen::CHAT && !chatIsChannel) {
                // обновить подпись лички (last heard)
                if (chatSub && lv_obj_is_valid(chatSub)) {
                    char buf[64];
                    const NodeRecord *r = view->getNodes().find(chatPeer);
                    if (r) {
                        char lh[24];
                        fmtLastHeard(view, r->lastHeard, lh, sizeof(lh));
                        snprintf(buf, sizeof(buf), "%s · %s", view->nodeShortName(chatPeer), lh);
                    } else {
                        snprintf(buf, sizeof(buf), "0x%08X", (unsigned)chatPeer);
                    }
                    lv_label_set_text(chatSub, buf);
                }
            }
        }
    }
}

// ---------------- утилиты ----------------

void HorizonUI::formatSender(uint32_t node, char *out, size_t cap)
{
    const HorizonDossierInfo *d = view->getStore().findDossier(node);
    if (d && d->callsign[0]) {
        const char *r = kRanks[d->rank < kRankCount ? d->rank : 0];
        snprintf(out, cap, "%s, %s", d->callsign, r);
    } else {
        snprintf(out, cap, "%s", view->nodeLongName(node));
    }
}

void HorizonUI::openSettingsGated(void)
{
    // Настройки ставит техник: вход только по мастер-PIN.
    askPin("Мастер-PIN:", [this]() { showSettings(); });
}

void HorizonUI::fmtTime(uint32_t t, char *out, size_t cap)
{
    if (t < 1000000) {
        snprintf(out, cap, "--:--");
        return;
    }
    time_t tt = (time_t)t;
    struct tm tmv;
    gmtime_r(&tt, &tmv);
    snprintf(out, cap, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
}

void HorizonUI::fmtDate(uint32_t t, char *out, size_t cap)
{
    if (t < 1000000) {
        snprintf(out, cap, "--.--.----");
        return;
    }
    time_t tt = (time_t)t;
    struct tm tmv;
    gmtime_r(&tt, &tmv);
    snprintf(out, cap, "%02d.%02d.%04d", tmv.tm_mday, tmv.tm_mon + 1, tmv.tm_year + 1900);
}

void HorizonUI::fmtLastHeard(HorizonView *v, uint32_t lh, char *out, size_t cap)
{
    if (lh == 0) {
        snprintf(out, cap, "не слышно");
        return;
    }
    time_t now;
    time(&now);
    uint32_t ref = now > 1000000 ? (uint32_t)now : v->getActTime();
    if (ref < lh || ref - lh < 90) {
        snprintf(out, cap, "сейчас");
    } else if (ref - lh < 3600) {
        snprintf(out, cap, "%u мин назад", (unsigned)((ref - lh) / 60));
    } else if (ref - lh < 86400) {
        snprintf(out, cap, "%u ч назад", (unsigned)((ref - lh) / 3600));
    } else {
        snprintf(out, cap, "%u д назад", (unsigned)((ref - lh) / 86400));
    }
}

#endif // HORIZON_UI
