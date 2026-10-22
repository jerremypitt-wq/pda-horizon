#include "horizon_target.h" // целевые флаги сборки (вкус: standalone/прошивка)
// ПДА «Горизонт» — инструменты Mesh: детектор эфира, сканер сигналов,
// трассировка маршрута, статистика и лог пакетов.
#ifdef HORIZON_UI

#include "horizon/HorizonStrings.h"
#include "horizon/HorizonTheme.h"
#include "horizon/HorizonUI.h"
#include "horizon/HorizonView.h"
#include <stdio.h>
#include <string.h>
#include <vector>

using namespace HorizonStrings;

namespace {
constexpr int16_t kStatusH = 22;
constexpr int16_t kHeaderH = 40;

// Русское имя типа пакета по номеру порта (номера - из штатной таблицы
// имён device-ui, deviceonly порт 10 - «сенсор»).
void hzPortName(uint16_t port, char *out, size_t cap)
{
    const char *n = nullptr;
    switch (port) {
    case 1:
    case 7:
        n = "текст";
        break;
    case 2:
        n = "железо";
        break;
    case 3:
        n = "позиция";
        break;
    case 4:
        n = "инфо";
        break;
    case 5:
        n = "маршрут";
        break;
    case 6:
        n = "админ";
        break;
    case 8:
        n = "точка";
        break;
    case 9:
        n = "аудио";
        break;
    case 10:
        n = "сенсор";
        break;
    case 32:
        n = "ответ";
        break;
    case 33:
        n = "ip";
        break;
    case 34:
        n = "pax";
        break;
    case 64:
        n = "serial";
        break;
    case 65:
        n = "SF";
        break;
    case 66:
        n = "range";
        break;
    case 67:
        n = "телеметрия";
        break;
    case 68:
        n = "ZPS";
        break;
    case 69:
        n = "сим";
        break;
    case 70:
        n = "trace";
        break;
    case 71:
        n = "соседи";
        break;
    case 72:
        n = "atak";
        break;
    case 73:
        n = "карта";
        break;
    case 74:
        n = "нагрузка";
        break;
    case 256:
        n = "приват";
        break;
    case 257:
        n = "atak-fwd";
        break;
    default:
        break;
    }
    if (n)
        snprintf(out, cap, "%s", n);
    else
        snprintf(out, cap, "порт %u", port);
}

// Качество связи 0..100 из RSSI/SNR (формула штатного сканера device-ui,
// ветка не-SX127x: SNR -18..8, RSSI -125..-25).
int signalPercent(int32_t rssi, float snr)
{
    float s = snr < -18.0f ? -18.0f : snr;
    int32_t r = rssi < -125 ? -125 : rssi;
    int pSnr = (int)((s + 18.0f) / 26.0f * 100.0f);
    int pRssi = (int)(r + 125);
    int q = (pSnr + pRssi * 2) / 3;
    if (q < 0)
        q = 0;
    if (q > 100)
        q = 100;
    return q;
}
} // namespace

lv_obj_t *HorizonUI::toolBody(void)
{
    lv_obj_t *body = lv_obj_create(scr);
    lv_obj_set_pos(body, 0, kStatusH + kHeaderH + 2);
    lv_obj_set_size(body, lv_pct(100), H() - (kStatusH + kHeaderH + 2) - 2);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(body, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(body, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(body, 6, LV_PART_MAIN);
    return body;
}

// ---------------- список инструментов ----------------

void HorizonUI::buildSetTools(void)
{
    clearScreen(Screen::SET_TOOLS);
    addStatusBar(scr);
    addNavHeader(scr, "Инструменты Mesh", "Эфир и диагностика");

    lv_obj_t *body = toolBody();
    struct ToolRow {
        const char *t;
        const char *s;
    };
    ToolRow rows[5] = {
        {"Детектор Mesh", "кто сейчас в эфире"},
        {"Сканер сигналов", "замер RSSI/SNR узла"},
        {"Маршрут", "трассировка пути до узла"},
        {"Статистика", "пакеты по узлам"},
        {"Лог пакетов", "последние пакеты эфира"},
    };
    for (int i = 0; i < 5; i++) {
        lv_obj_t *b = rowButton(body, rows[i].t, rows[i].s, i);
        // rowButton вешает cbRow (работает только на экране SETTINGS) -
        // переназначить на выбор инструмента.
        lv_obj_remove_event_cb(b, cbRow);
        lv_obj_add_event_cb(b, cbToolRow, LV_EVENT_CLICKED, this);
    }
}

void HorizonUI::cbToolRow(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    int id = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t *)lv_event_get_target(e));
    switch (id) {
    case 0:
        self->buildToolDetector();
        break;
    case 1:
        self->toolPickMode = 0;
        self->buildToolPick();
        break;
    case 2:
        self->toolPickMode = 1;
        self->buildToolPick();
        break;
    case 3:
        self->buildToolStats();
        break;
    case 4:
        self->buildToolLog();
        break;
    default:
        break;
    }
}

// ---------------- тик-обновление ----------------

void HorizonUI::refreshTools(void)
{
    bool dirty = view->takeToolsDirty();
    switch (current) {
    case Screen::TOOL_DETECTOR:
        // takeNodesDirty дёргаем только здесь: на экране контактов его
        // забирает ветка контактов тика (экраны взаимоисключающие).
        if (dirty || view->takeNodesDirty())
            refreshDetector();
        break;
    case Screen::TOOL_SCAN:
        refreshScan(); // каждый тик: прогресс и значения
        break;
    case Screen::TOOL_ROUTE:
        refreshRoute(); // каждый тик: обратный отсчёт
        break;
    case Screen::TOOL_STATS:
        if (dirty)
            refreshStats();
        break;
    case Screen::TOOL_LOG:
        if (dirty)
            refreshLog();
        break;
    default:
        break;
    }
}

// ---------------- детектор ----------------

void HorizonUI::buildToolDetector(void)
{
    clearScreen(Screen::TOOL_DETECTOR);
    addStatusBar(scr);
    addNavHeader(scr, "Детектор Mesh", "кто сейчас в эфире");

    lv_obj_t *body = toolBody();
    detSub = lv_label_create(body);
    lv_obj_set_style_text_font(detSub, HorizonTheme::fontSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(detSub, hzColor(HorizonTheme::current().textDim), LV_PART_MAIN);

    detList = lv_obj_create(body);
    lv_obj_set_width(detList, lv_pct(100));
    lv_obj_set_height(detList, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(detList, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(detList, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(detList, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(detList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(detList, 4, LV_PART_MAIN);
    lv_obj_remove_flag(detList, LV_OBJ_FLAG_SCROLLABLE); // мотается body
    refreshDetector();
}

void HorizonUI::refreshDetector(void)
{
    if (current != Screen::TOOL_DETECTOR || !detList || !lv_obj_is_valid(detList))
        return;
    if (fingerDown())
        return; // палец на экране - не выбивать строку из-под tap'а
    const HorizonPalette &p = HorizonTheme::current();
    lv_obj_clean(detList);

    struct Row {
        uint32_t id;
        uint32_t lh;
    };
    std::vector<Row> rows;
    for (auto &kv : view->getNodes().records()) {
        if (kv.first == view->getMyNodeNum())
            continue;
        rows.push_back({kv.first, kv.second.lastHeard});
    }
    // свежие сверху
    for (size_t i = 0; i < rows.size(); i++) {
        for (size_t j = i + 1; j < rows.size(); j++) {
            if (rows[j].lh > rows[i].lh) {
                Row t = rows[i];
                rows[i] = rows[j];
                rows[j] = t;
            }
        }
    }

    if (detSub && lv_obj_is_valid(detSub))
        lv_label_set_text_fmt(detSub, "Узлов в эфире: %u", (unsigned)rows.size());

    if (rows.empty()) {
        lv_obj_t *e = lv_label_create(detList);
        lv_label_set_text(e, "Эфир пуст - подожди или проверь антенну");
        lv_label_set_long_mode(e, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(e, lv_pct(100));
        lv_obj_set_style_text_color(e, hzColor(p.textDim), LV_PART_MAIN);
        lv_obj_set_style_text_align(e, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        return;
    }

    for (auto &r : rows) {
        const NodeRecord *rec = view->getNodes().find(r.id);
        bool online = view->isNodeOnline(r.id);

        lv_obj_t *row = lv_obj_create(detList);
        HorizonTheme::stylePanel(row);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, 56);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 8, LV_PART_MAIN);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *badge = lv_obj_create(row);
        lv_obj_set_size(badge, 52, 38);
        lv_obj_set_style_bg_color(badge, hzColor(online ? p.ok : p.border), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(badge, 4, LV_PART_MAIN);
        lv_obj_set_style_border_width(badge, 0, LV_PART_MAIN);
        lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *bt = lv_label_create(badge);
        lv_label_set_text(bt, view->nodeShortName(r.id));
        lv_obj_set_style_text_font(bt, HorizonTheme::fontSmall(), LV_PART_MAIN);
        lv_obj_set_style_text_color(bt, lv_color_black(), LV_PART_MAIN);
        lv_obj_center(bt);

        // Ширина колонки - точная (бейдж 52 + зазор 8 + пады 32),
        // иначе длинные имена режутся без точек.
        lv_obj_t *col = lv_obj_create(row);
        lv_obj_set_width(col, W() - 92);
        lv_obj_set_height(col, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(col, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(col, 0, LV_PART_MAIN);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *nm = lv_label_create(col);
        lv_label_set_text(nm, view->displayName(r.id));
        lv_obj_set_width(nm, lv_pct(100));
        lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);

        char sub[96];
        if (rec && rec->lastHeard != 0) {
            char lh[24];
            fmtLastHeard(view, rec->lastHeard, lh, sizeof(lh));
            snprintf(sub, sizeof(sub), "RSSI %d · SNR %.1f · %s", (int)rec->rssi, (double)rec->snr, lh);
        } else {
            snprintf(sub, sizeof(sub), "не слышно");
        }
        lv_obj_t *sl = lv_label_create(col);
        lv_label_set_text(sl, sub);
        lv_obj_set_style_text_font(sl, HorizonTheme::fontSmall(), LV_PART_MAIN);
        lv_obj_set_style_text_color(sl, hzColor(p.textDim), LV_PART_MAIN);
        lv_obj_set_width(sl, lv_pct(100));
        lv_label_set_long_mode(sl, LV_LABEL_LONG_DOT);
    }
}

// ---------------- выбор узла ----------------

void HorizonUI::buildToolPick(void)
{
    clearScreen(Screen::TOOL_PICK);
    addStatusBar(scr);
    addNavHeader(scr, toolPickMode == 0 ? "Сканер: узел" : "Маршрут: узел", "выбери цель");

    lv_obj_t *body = toolBody();
    const HorizonPalette &p = HorizonTheme::current();

    struct Row {
        uint32_t id;
        uint32_t lh;
    };
    std::vector<Row> rows;
    for (auto &kv : view->getNodes().records()) {
        if (kv.first == view->getMyNodeNum())
            continue;
        rows.push_back({kv.first, kv.second.lastHeard});
    }
    for (size_t i = 0; i < rows.size(); i++) {
        for (size_t j = i + 1; j < rows.size(); j++) {
            if (rows[j].lh > rows[i].lh) {
                Row t = rows[i];
                rows[i] = rows[j];
                rows[j] = t;
            }
        }
    }

    if (rows.empty()) {
        lv_obj_t *e = lv_label_create(body);
        lv_label_set_text(e, "Нет узлов - загляни в детектор эфира");
        lv_obj_set_style_text_color(e, hzColor(p.textDim), LV_PART_MAIN);
        lv_obj_set_width(e, lv_pct(100));
        lv_obj_set_style_text_align(e, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        return;
    }

    for (auto &r : rows) {
        bool online = view->isNodeOnline(r.id);
        lv_obj_t *b = lv_btn_create(body);
        HorizonTheme::styleButton(b, false);
        lv_obj_set_width(b, lv_pct(100));
        lv_obj_set_height(b, 54);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(b, 8, LV_PART_MAIN);

        lv_obj_t *badge = lv_obj_create(b);
        lv_obj_set_size(badge, 52, 38);
        lv_obj_set_style_bg_color(badge, hzColor(online ? p.ok : p.border), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(badge, 4, LV_PART_MAIN);
        lv_obj_set_style_border_width(badge, 0, LV_PART_MAIN);
        lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *bt = lv_label_create(badge);
        lv_label_set_text(bt, view->nodeShortName(r.id));
        lv_obj_set_style_text_font(bt, HorizonTheme::fontSmall(), LV_PART_MAIN);
        lv_obj_set_style_text_color(bt, lv_color_black(), LV_PART_MAIN);
        lv_obj_center(bt);

        lv_obj_t *col = lv_obj_create(b);
        lv_obj_set_width(col, W() - 92);
        lv_obj_set_height(col, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(col, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(col, 0, LV_PART_MAIN);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *nm = lv_label_create(col);
        lv_label_set_text(nm, view->displayName(r.id));
        lv_obj_set_width(nm, lv_pct(100));
        lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);

        char lh[24];
        const NodeRecord *rec = view->getNodes().find(r.id);
        fmtLastHeard(view, rec ? rec->lastHeard : 0, lh, sizeof(lh));
        char sub[64];
        snprintf(sub, sizeof(sub), "!%04x · %s", (unsigned)(r.id & 0xFFFF), lh);
        lv_obj_t *sl = lv_label_create(col);
        lv_label_set_text(sl, sub);
        lv_obj_set_style_text_font(sl, HorizonTheme::fontSmall(), LV_PART_MAIN);
        lv_obj_set_style_text_color(sl, hzColor(p.textDim), LV_PART_MAIN);

        lv_obj_set_user_data(b, (void *)(intptr_t)r.id);
        lv_obj_add_event_cb(b, cbToolPick, LV_EVENT_CLICKED, this);
    }
}

void HorizonUI::cbToolPick(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    uint32_t id = (uint32_t)(intptr_t)lv_obj_get_user_data((lv_obj_t *)lv_event_get_target(e));
    if (id == 0 || id == 0xFFFFFFFFu)
        return;
    if (self->toolPickMode == 0) {
        self->view->startScan(id);
        self->buildToolScan();
    } else {
        self->view->startTrace(id);
        self->buildToolRoute();
    }
}

// ---------------- сканер ----------------

void HorizonUI::buildToolScan(void)
{
    clearScreen(Screen::TOOL_SCAN);
    addStatusBar(scr);
    addNavHeader(scr, "Сканер сигналов", view->nodeShortName(view->scanNode));

    lv_obj_t *body = toolBody();
    const HorizonPalette &p = HorizonTheme::current();

    lv_obj_t *nm = lv_label_create(body);
    lv_label_set_text(nm, view->displayName(view->scanNode));
    lv_obj_set_width(nm, lv_pct(100));
    lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);

    scanBig = lv_label_create(body);
    lv_obj_set_style_text_font(scanBig, HorizonTheme::fontBig(), LV_PART_MAIN);

    scanPct = lv_label_create(body);
    lv_obj_set_style_text_font(scanPct, HorizonTheme::fontMedium(), LV_PART_MAIN);
    lv_obj_set_style_text_color(scanPct, hzColor(p.accent), LV_PART_MAIN);

    scanProg = lv_label_create(body);
    lv_obj_set_style_text_font(scanProg, HorizonTheme::fontSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(scanProg, hzColor(p.textDim), LV_PART_MAIN);

    lv_obj_t *stop = lv_btn_create(body);
    HorizonTheme::styleButton(stop, false);
    lv_obj_set_width(stop, lv_pct(100));
    lv_obj_set_height(stop, 44);
    lv_obj_t *sl = lv_label_create(stop);
    lv_label_set_text(sl, "Остановить");
    lv_obj_center(sl);
    lv_obj_add_event_cb(stop, cbScanStop, LV_EVENT_CLICKED, this);

    lv_obj_t *hint = lv_label_create(body);
    lv_label_set_text(hint, "Сканер шлёт запросы позиции и меряет ответы. 8 замеров - дальше итог.");
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, HorizonTheme::fontSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(hint, hzColor(p.textDim), LV_PART_MAIN);

    refreshScan();
}

void HorizonUI::refreshScan(void)
{
    if (current != Screen::TOOL_SCAN)
        return;
    if (view->scanGot) {
        if (scanBig && lv_obj_is_valid(scanBig))
            lv_label_set_text_fmt(scanBig, "RSSI %d · SNR %.1f", (int)view->scanRssi, (double)view->scanSnr);
        if (scanPct && lv_obj_is_valid(scanPct))
            lv_label_set_text_fmt(scanPct, "%d%%", signalPercent(view->scanRssi, view->scanSnr));
    } else {
        if (scanBig && lv_obj_is_valid(scanBig))
            lv_label_set_text(scanBig, "---");
        if (scanPct && lv_obj_is_valid(scanPct))
            lv_label_set_text(scanPct, "--%");
    }
    if (scanProg && lv_obj_is_valid(scanProg)) {
        if (view->scanRunning)
            lv_label_set_text_fmt(scanProg, "Замер %u/%u%s", (unsigned)view->scanCount, (unsigned)view->scanWant,
                                  view->scanGot && view->scanDirect ? " · напрямую" : "");
        else if (view->scanGot)
            lv_label_set_text_fmt(scanProg, "Готово: %u/%u", (unsigned)view->scanCount, (unsigned)view->scanWant);
        else
            lv_label_set_text(scanProg, "Нет ответа");
    }
}

void HorizonUI::cbScanStop(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    self->view->stopScan();
    self->buildToolPick();
}

// ---------------- маршрут ----------------

void HorizonUI::buildToolRoute(void)
{
    clearScreen(Screen::TOOL_ROUTE);
    addStatusBar(scr);
    addNavHeader(scr, "Маршрут", view->nodeShortName(view->traceNode));

    lv_obj_t *body = toolBody();

    lv_obj_t *nm = lv_label_create(body);
    lv_label_set_text(nm, view->displayName(view->traceNode));
    lv_obj_set_width(nm, lv_pct(100));
    lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);

    routeStatus = lv_label_create(body);

    routeBox = lv_obj_create(body);
    lv_obj_set_width(routeBox, lv_pct(100));
    lv_obj_set_height(routeBox, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(routeBox, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(routeBox, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(routeBox, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(routeBox, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(routeBox, 2, LV_PART_MAIN);
    lv_obj_remove_flag(routeBox, LV_OBJ_FLAG_SCROLLABLE); // мотается body

    lv_obj_t *stop = lv_btn_create(body);
    HorizonTheme::styleButton(stop, false);
    lv_obj_set_width(stop, lv_pct(100));
    lv_obj_set_height(stop, 44);
    lv_obj_t *sl = lv_label_create(stop);
    lv_label_set_text(sl, "Остановить");
    lv_obj_center(sl);
    lv_obj_add_event_cb(stop, cbTraceStop, LV_EVENT_CLICKED, this);

    refreshRoute();
}

void HorizonUI::refreshRoute(void)
{
    if (current != Screen::TOOL_ROUTE || !routeStatus || !lv_obj_is_valid(routeStatus))
        return;
    if (view->traceRunning) {
        uint32_t gone = lastTickMs >= view->traceStartMs ? (lastTickMs - view->traceStartMs) / 1000 : 0;
        uint32_t left = gone >= 20 ? 0 : 20 - gone;
        lv_label_set_text_fmt(routeStatus, "Опрос... %u с", (unsigned)left);
        return;
    }
    if (!routeBox || !lv_obj_is_valid(routeBox))
        return;
    // Результат рисуем один раз (дальше тики только обновляют статус-строку).
    if (lv_obj_get_child_cnt(routeBox) == 0) {
        const HorizonPalette &p = HorizonTheme::current();
        if (view->traceGot && view->trace.valid) {
            if (view->trace.fwdN == 0 && view->trace.backN == 0) {
                lv_obj_t *d = lv_label_create(routeBox);
                lv_label_set_text(d, "Прямой эфир, без хопов");
                lv_obj_set_style_text_color(d, hzColor(p.ok), LV_PART_MAIN);
            } else {
                lv_obj_t *fh = lv_label_create(routeBox);
                lv_label_set_text(fh, "Туда:");
                lv_obj_set_style_text_color(fh, hzColor(p.accent), LV_PART_MAIN);
                for (uint8_t i = 0; i < view->trace.fwdN; i++) {
                    uint32_t hop = view->trace.fwd[i];
                    lv_obj_t *hl = lv_label_create(routeBox);
                    lv_label_set_text_fmt(hl, "%u. %s !%04x", (unsigned)(i + 1), view->nodeShortName(hop),
                                          (unsigned)(hop & 0xFFFF));
                    lv_obj_set_width(hl, lv_pct(100));
                    lv_label_set_long_mode(hl, LV_LABEL_LONG_DOT);
                }
                if (view->trace.backN > 0) {
                    lv_obj_t *bh = lv_label_create(routeBox);
                    lv_label_set_text(bh, "Обратно:");
                    lv_obj_set_style_text_color(bh, hzColor(p.accent), LV_PART_MAIN);
                    for (uint8_t i = 0; i < view->trace.backN; i++) {
                        uint32_t hop = view->trace.back[i];
                        lv_obj_t *hl = lv_label_create(routeBox);
                        lv_label_set_text_fmt(hl, "%u. %s !%04x", (unsigned)(i + 1), view->nodeShortName(hop),
                                              (unsigned)(hop & 0xFFFF));
                        lv_obj_set_width(hl, lv_pct(100));
                        lv_label_set_long_mode(hl, LV_LABEL_LONG_DOT);
                    }
                }
            }
        } else {
            lv_obj_t *f = lv_label_create(routeBox);
            lv_label_set_text(f, "Узел не ответил за 20 с");
            lv_obj_set_style_text_color(f, hzColor(p.textDim), LV_PART_MAIN);
        }
    }
    if (view->traceGot)
        lv_label_set_text(routeStatus, "Маршрут найден");
    else
        lv_label_set_text(routeStatus, "Нет ответа");
}

void HorizonUI::cbTraceStop(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    self->view->stopTrace();
    self->buildToolPick();
}

// ---------------- статистика ----------------

void HorizonUI::buildToolStats(void)
{
    clearScreen(Screen::TOOL_STATS);
    addStatusBar(scr);
    addNavHeader(scr, "Статистика", "пакеты по узлам");

    lv_obj_t *body = toolBody();
    statsSub = lv_label_create(body);
    lv_obj_set_style_text_font(statsSub, HorizonTheme::fontSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(statsSub, hzColor(HorizonTheme::current().textDim), LV_PART_MAIN);

    statsList = lv_obj_create(body);
    lv_obj_set_width(statsList, lv_pct(100));
    lv_obj_set_height(statsList, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(statsList, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(statsList, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(statsList, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(statsList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(statsList, 4, LV_PART_MAIN);
    lv_obj_remove_flag(statsList, LV_OBJ_FLAG_SCROLLABLE); // мотается body

    lv_obj_t *clr = lv_btn_create(body);
    HorizonTheme::styleButton(clr, false);
    lv_obj_set_width(clr, lv_pct(100));
    lv_obj_set_height(clr, 40);
    lv_obj_t *cl = lv_label_create(clr);
    lv_label_set_text(cl, "Сбросить");
    lv_obj_center(cl);
    lv_obj_add_event_cb(clr, cbStatsClear, LV_EVENT_CLICKED, this);

    refreshStats();
}

void HorizonUI::refreshStats(void)
{
    if (current != Screen::TOOL_STATS || !statsList || !lv_obj_is_valid(statsList))
        return;
    if (fingerDown())
        return; // палец на экране - не дёргать список
    const HorizonPalette &p = HorizonTheme::current();
    if (statsSub && lv_obj_is_valid(statsSub))
        lv_label_set_text_fmt(statsSub, "Всего пакетов: %u", (unsigned)view->statsTotal());
    lv_obj_clean(statsList);

    std::vector<HorizonView::HzNodeStats> v = view->nodeStatsSorted();
    if (v.empty()) {
        lv_obj_t *e = lv_label_create(statsList);
        lv_label_set_text(e, "Пока тихо - пакеты копятся сами");
        lv_obj_set_style_text_color(e, hzColor(p.textDim), LV_PART_MAIN);
        lv_obj_set_width(e, lv_pct(100));
        lv_obj_set_style_text_align(e, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        return;
    }
    // Т-текст, П-позиция, И-инфо, М-маршрут, ТЛ-телеметрия, С-соседи.
    for (auto &s : v) {
        lv_obj_t *row = lv_obj_create(statsList);
        HorizonTheme::stylePanel(row);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_min_height(row, 50, LV_PART_MAIN);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(row, 6, LV_PART_MAIN);
        lv_obj_set_style_pad_row(row, 1, LV_PART_MAIN);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *nm = lv_label_create(row);
        lv_label_set_text_fmt(nm, "%s (%u)", view->displayName(s.node), (unsigned)s.sum);
        lv_obj_set_width(nm, lv_pct(100));
        lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);

        char sub[64];
        snprintf(sub, sizeof(sub), "Т%u П%u И%u М%u ТЛ%u С%u", s.txt, s.pos, s.inf, s.trc, s.tel, s.nbr);
        lv_obj_t *sl = lv_label_create(row);
        lv_label_set_text(sl, sub);
        lv_obj_set_style_text_font(sl, HorizonTheme::fontSmall(), LV_PART_MAIN);
        lv_obj_set_style_text_color(sl, hzColor(p.textDim), LV_PART_MAIN);
    }
}

void HorizonUI::cbStatsClear(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    self->view->clearNodeStats();
    self->refreshStats();
}

// ---------------- лог пакетов ----------------

void HorizonUI::buildToolLog(void)
{
    clearScreen(Screen::TOOL_LOG);
    addStatusBar(scr);
    addNavHeader(scr, "Лог пакетов", "последние 30");

    lv_obj_t *body = toolBody();

    logList = lv_obj_create(body);
    lv_obj_set_width(logList, lv_pct(100));
    lv_obj_set_height(logList, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(logList, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(logList, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(logList, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(logList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(logList, 2, LV_PART_MAIN);
    lv_obj_remove_flag(logList, LV_OBJ_FLAG_SCROLLABLE); // мотается body

    lv_obj_t *clr = lv_btn_create(body);
    HorizonTheme::styleButton(clr, false);
    lv_obj_set_width(clr, lv_pct(100));
    lv_obj_set_height(clr, 40);
    lv_obj_t *cl = lv_label_create(clr);
    lv_label_set_text(cl, "Очистить");
    lv_obj_center(cl);
    lv_obj_add_event_cb(clr, cbLogClear, LV_EVENT_CLICKED, this);

    refreshLog();
}

void HorizonUI::refreshLog(void)
{
    if (current != Screen::TOOL_LOG || !logList || !lv_obj_is_valid(logList))
        return;
    if (fingerDown())
        return; // палец на экране - не дёргать список
    const HorizonPalette &p = HorizonTheme::current();
    lv_obj_clean(logList);

    const std::vector<HorizonView::HzPacketEntry> &log = view->packetLog();
    if (log.empty()) {
        lv_obj_t *e = lv_label_create(logList);
        lv_label_set_text(e, "Пока тихо - эфир молчит");
        lv_obj_set_style_text_color(e, hzColor(p.textDim), LV_PART_MAIN);
        lv_obj_set_width(e, lv_pct(100));
        lv_obj_set_style_text_align(e, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        return;
    }
    // Новые сверху.
    for (size_t i = log.size(); i-- > 0;) {
        const HorizonView::HzPacketEntry &e = log[i];
        char tb[16];
        fmtTime(e.time, tb, sizeof(tb));
        char pn[24];
        hzPortName(e.port, pn, sizeof(pn));
        char line[128];
        if (e.to == 0xFFFFFFFFu)
            snprintf(line, sizeof(line), "%s %s>all ch%u %s %uБ", tb, view->nodeShortName(e.from), e.ch, pn,
                     (unsigned)e.size);
        else
            snprintf(line, sizeof(line), "%s %s>%s ch%u %s %uБ", tb, view->nodeShortName(e.from),
                     view->nodeShortName(e.to), e.ch, pn, (unsigned)e.size);
        lv_obj_t *l = lv_label_create(logList);
        lv_label_set_text(l, line);
        lv_obj_set_style_text_font(l, HorizonTheme::fontSmall(), LV_PART_MAIN);
        lv_obj_set_width(l, lv_pct(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    }
}

void HorizonUI::cbLogClear(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    self->view->clearPacketLog();
    self->refreshLog();
}

#endif // HORIZON_UI
