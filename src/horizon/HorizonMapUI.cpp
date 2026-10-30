#include "horizon_target.h" // целевые флаги сборки (вкус: standalone/прошивка)
// ПДА «Горизонт» - экран карты и форма точки (см. docs/MAP.md).
#ifdef HORIZON_UI

#include "horizon/HorizonConfig.h"
#include "horizon/HorizonMap.h"
#include "horizon/HorizonStrings.h"
#include "horizon/HorizonTheme.h"
#include "horizon/HorizonUI.h"
#include "horizon/HorizonView.h"
#include "util/ILog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(ARCH_PORTDUINO)
#include "PortduinoFS.h"
static fs::FS &mapFs = PortduinoFS;
#else
#include "LittleFS.h"
static fs::FS &mapFs = LittleFS;
#endif

// Буферы карты (исходник 300 КБ + кадр до ~270 КБ) - только PSRAM:
// в DRAM ESP32-S3 столько нет. Освобождать обычным free() (годен для ps_malloc).
#if defined(ARDUINO_ARCH_ESP32)
#define HZMAP_ALLOC(n) ps_malloc(n)
#else
#define HZMAP_ALLOC(n) malloc(n)
#endif

using namespace HorizonStrings;

namespace {
constexpr int16_t kStatusH = 22;
constexpr int16_t kHeaderH = 40;
constexpr int kMapW = 480;
constexpr int kMapH = 320;
// Уровни лупы (множитель к вписанному масштабу).
constexpr float kZoomSteps[] = {1.0f, 1.5f, 2.0f, 3.0f};
constexpr int kZoomCount = 4;
// Цвета маркеров по типам (порядок = HzPoiType, docs/MAP.md §3).
constexpr uint32_t kPoiColors[5] = {
    0xE8C33A, // точка интереса - жёлтый
    0x3FA34D, // база - зелёный
    0xC0392B, // опасность - красный
    0x8E44AD, // аномалия - фиолетовый
    0x2E86DE, // схрон - синий
};
constexpr int kTapSlopPx = 12;   // сдвиг меньше - это тап, а не драг
constexpr int kMarkerHitPx = 26; // радиус попадания в маркер, px
constexpr uint32_t kMarkersMs = 120; // маркеры при драге - не чаще (иначе дёргается)

void styleDropdownListLocal(lv_obj_t *dd)
{
    lv_obj_t *lst = lv_dropdown_get_list(dd);
    if (lst) {
        const HorizonPalette &p = HorizonTheme::current();
        HorizonTheme::stylePanel(lst);
        lv_obj_set_style_bg_color(lst, hzColor(p.accent), LV_PART_SELECTED);
        lv_obj_set_style_text_color(lst, hzColor(p.accentText), LV_PART_SELECTED);
    }
}

bool readMapFile(const char *path, uint8_t *dst, size_t len)
{
    File f = mapFs.open(path, "r");
    if (!f)
        return false;
    size_t n = f.readBytes((char *)dst, len);
    f.close();
    return n == len;
}
} // namespace

// Состояние экрана карты: виджеты умирают с экраном (mapDropWidgets),
// буферы и вид (зум/сдвиг) живут между MAP и MAP_FORM.
struct HzMapUi {
    lv_obj_t *img = nullptr;
    lv_obj_t *markers = nullptr;
    lv_obj_t *popup = nullptr;
    lv_obj_t *zin = nullptr;
    lv_obj_t *zout = nullptr;
    lv_obj_t *formName = nullptr;
    lv_obj_t *formComment = nullptr;
    lv_obj_t *formType = nullptr;
    lv_obj_t *formShared = nullptr;
    lv_obj_t *formCh = nullptr;
    lv_obj_t *formChRow = nullptr;
    uint8_t *mapBuf = nullptr;  // исходник 480x320x2
    uint8_t *viewBuf = nullptr; // кадр под текущий вид
    lv_image_dsc_t dsc;
    int viewY = 0;
    int viewW = 0;
    int viewH = 0;
    float baseScale = 1.0f; // вписанный масштаб
    float scale = 1.0f;     // px экрана на px карты
    uint8_t zoomIdx = 0;
    float ox = 0.0f; // какой px карты в левом верхнем углу вида
    float oy = 0.0f;
    bool down = false;
    int lastX = 0;
    int lastY = 0;
    int moved = 0;
    uint32_t lastMarkersMs = 0;
    char popupId[28] = "";
    char formEditId[28] = ""; // "" = новая точка
    int formX = 0;            // тысячные доли для новой точки
    int formY = 0;
};

namespace {
// Точка (тысячные доли) -> px вида.
void poiToScreen(const HzMapUi *m, const HzPoi *p, int *sx, int *sy)
{
    float px = (p->x * (kMapW - 1)) / 1000.0f;
    float py = (p->y * (kMapH - 1)) / 1000.0f;
    *sx = (int)((px - m->ox) * m->scale + 0.5f);
    *sy = (int)((py - m->oy) * m->scale + 0.5f);
}

// Px вида -> тысячные доли (для новой точки; за краем - clamp).
void screenToPm(const HzMapUi *m, int lx, int ly, int *xPm, int *yPm)
{
    float px = m->ox + lx / m->scale;
    float py = m->oy + ly / m->scale;
    if (px < 0)
        px = 0;
    if (px > kMapW - 1)
        px = kMapW - 1;
    if (py < 0)
        py = 0;
    if (py > kMapH - 1)
        py = kMapH - 1;
    *xPm = (int)(px * 1000.0f / (kMapW - 1) + 0.5f);
    *yPm = (int)(py * 1000.0f / (kMapH - 1) + 0.5f);
}

void clampView(HzMapUi *m)
{
    float maxOx = kMapW - m->viewW / m->scale;
    float maxOy = kMapH - m->viewH / m->scale;
    if (maxOx < 0)
        m->ox = maxOx / 2; // вид шире карты - по центру
    else {
        if (m->ox < 0)
            m->ox = 0;
        if (m->ox > maxOx)
            m->ox = maxOx;
    }
    if (maxOy < 0)
        m->oy = maxOy / 2;
    else {
        if (m->oy < 0)
            m->oy = 0;
        if (m->oy > maxOy)
            m->oy = maxOy;
    }
}

// Пересчёт кадра из исходника (nearest neighbour: безопасно на любом LVGL,
// карта рисованная - ступеньки незаметны).
void renderView(HzMapUi *m)
{
    if (!m->mapBuf || !m->viewBuf || !m->img)
        return;
    uint16_t *dst = (uint16_t *)m->viewBuf;
    const uint16_t *src = (const uint16_t *)m->mapBuf;
    float inv = 1.0f / m->scale;
    for (int y = 0; y < m->viewH; y++) {
        int sy = (int)(m->oy + y * inv);
        bool rowOk = sy >= 0 && sy < kMapH;
        for (int x = 0; x < m->viewW; x++) {
            int sx = (int)(m->ox + x * inv);
            uint16_t px = 0x0000;
            if (rowOk && sx >= 0 && sx < kMapW)
                px = src[sy * kMapW + sx];
            *dst++ = px;
        }
    }
    if (lv_obj_is_valid(m->img))
        lv_obj_invalidate(m->img);
}

void addMarkersFor(HzMapUi *m, const std::vector<HzPoi> &v, HzMapStore &st, bool baked)
{
    for (const HzPoi &p : v) {
        if (baked && st.isTombstoned(p.id))
            continue;
        int sx = 0, sy = 0;
        poiToScreen(m, &p, &sx, &sy);
        if (sx < -60 || sy < -24 || sx > m->viewW + 60 || sy > m->viewH + 24)
            continue; // за кадром
        lv_obj_t *dot = lv_obj_create(m->markers);
        lv_obj_set_size(dot, 14, 14);
        lv_obj_set_pos(dot, sx - 7, sy - 7);
        uint32_t col = kPoiColors[p.type < HZ_POI_COUNT ? p.type : 0];
        lv_obj_set_style_bg_color(dot, hzColor(col), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(dot, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(dot, hzColor(0x000000), LV_PART_MAIN);
        lv_obj_set_style_radius(dot, 7, LV_PART_MAIN);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE); // тапы - виду под маркерами
        lv_obj_t *nm = lv_label_create(m->markers);
        lv_label_set_text(nm, p.name.c_str());
        lv_obj_set_style_text_font(nm, HorizonTheme::fontSmall(), LV_PART_MAIN);
        lv_obj_set_style_text_color(nm, hzColor(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_bg_color(nm, hzColor(0x000000), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(nm, LV_OPA_70, LV_PART_MAIN);
        lv_obj_set_width(nm, 110);
        lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(nm, sx + 10, sy - 8);
        lv_obj_remove_flag(nm, LV_OBJ_FLAG_CLICKABLE);
    }
}

// Ближайшая точка к тапу (px вида) в радиусе kMarkerHitPx.
// Указатель - в пулы стора: жив, пока пулы не правили (здесь - сразу в дело).
const HzPoi *pickAt(HzMapUi *m, HzMapStore &st, int lx, int ly)
{
    const HzPoi *best = nullptr;
    int bestD2 = kMarkerHitPx * kMarkerHitPx;
    const std::vector<HzPoi> *pools[3] = {&st.baked(), &st.net(), &st.my()};
    for (int i = 0; i < 3; i++) {
        for (const HzPoi &p : *pools[i]) {
            if (i == 0 && st.isTombstoned(p.id))
                continue;
            int sx = 0, sy = 0;
            poiToScreen(m, &p, &sx, &sy);
            int dx = sx - lx, dy = sy - ly;
            int d2 = dx * dx + dy * dy;
            if (d2 < bestD2) {
                bestD2 = d2;
                best = &p;
            }
        }
    }
    return best;
}
} // namespace

void HorizonUI::mapDropWidgets(void)
{
    if (!mapUi)
        return;
    mapUi->img = nullptr;
    mapUi->markers = nullptr;
    mapUi->popup = nullptr;
    mapUi->zin = nullptr;
    mapUi->zout = nullptr;
    mapUi->formName = nullptr;
    mapUi->formComment = nullptr;
    mapUi->formType = nullptr;
    mapUi->formShared = nullptr;
    mapUi->formCh = nullptr;
    mapUi->formChRow = nullptr;
}

void HorizonUI::mapCleanup(void)
{
    if (!mapUi)
        return;
    if (mapUi->mapBuf) {
        free(mapUi->mapBuf);
        mapUi->mapBuf = nullptr;
    }
    if (mapUi->viewBuf) {
        free(mapUi->viewBuf);
        mapUi->viewBuf = nullptr;
    }
    delete mapUi;
    mapUi = nullptr;
}

void HorizonUI::showMap(void)
{
    buildMap();
}

void HorizonUI::buildMap(void)
{
    HzMapStore &st = view->getMapStore();
    if (!st.loaded)
        st.loadAll(); // страховка (обычно уже загружено в init)
    if (!mapUi)
        mapUi = new HzMapUi();
    HzMapUi *m = mapUi;
    if (!m->mapBuf) {
        uint8_t *buf = (uint8_t *)HZMAP_ALLOC((size_t)kMapW * kMapH * 2);
        if (!buf || !readMapFile("/hzmap/map.rgb565", buf, (size_t)kMapW * kMapH * 2)) {
            if (buf)
                free(buf);
            mapCleanup();
            toast("Нет файла карты");
            ILOG_ERROR("Horizon: map: bad /hzmap/map.rgb565");
            showMenu();
            return;
        }
        m->mapBuf = buf;
    }
    // Геометрия вида (не меняется без перезагрузки, но пересчёт дёшев).
    m->viewY = kStatusH + kHeaderH + 2;
    m->viewW = W();
    m->viewH = H() - m->viewY;
    if (!m->viewBuf) {
        float fit = m->viewW / (float)kMapW;
        float fitH = m->viewH / (float)kMapH;
        if (fitH < fit)
            fit = fitH;
        m->baseScale = fit;
        m->scale = fit * kZoomSteps[m->zoomIdx];
        clampView(m);
        m->viewBuf = (uint8_t *)HZMAP_ALLOC((size_t)m->viewW * m->viewH * 2);
        if (!m->viewBuf) {
            mapCleanup();
            toast("Нет памяти для карты");
            ILOG_ERROR("Horizon: map: view alloc failed");
            showMenu();
            return;
        }
        m->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        m->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
        m->dsc.header.flags = 0;
        m->dsc.header.w = m->viewW;
        m->dsc.header.h = m->viewH;
        m->dsc.header.stride = m->viewW * 2;
        m->dsc.data_size = (uint32_t)((size_t)m->viewW * m->viewH * 2);
        m->dsc.data = m->viewBuf;
    }

    clearScreen(Screen::MAP); // сносит виджеты (mapDropWidgets), буферы целы
    addStatusBar(scr);
    char sub[64];
    snprintf(sub, sizeof(sub), "Общих %u/50 · своих %u/10", (unsigned)st.sharedCount(),
             (unsigned)st.privCount());
    addNavHeader(scr, "Карта", sub);

    lv_obj_t *box = lv_obj_create(scr);
    lv_obj_set_pos(box, 0, m->viewY);
    lv_obj_set_size(box, m->viewW, m->viewH);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(box, 0, LV_PART_MAIN);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    m->img = lv_image_create(box);
    lv_image_set_src(m->img, &m->dsc);
    lv_obj_set_pos(m->img, 0, 0);
    lv_obj_add_event_cb(m->img, cbMapTouch, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(m->img, cbMapTouch, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(m->img, cbMapTouch, LV_EVENT_RELEASED, this);

    m->markers = lv_obj_create(box);
    lv_obj_set_pos(m->markers, 0, 0);
    lv_obj_set_size(m->markers, m->viewW, m->viewH);
    lv_obj_set_style_bg_opa(m->markers, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(m->markers, 0, LV_PART_MAIN);
    lv_obj_remove_flag(m->markers, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(m->markers, LV_OBJ_FLAG_CLICKABLE); // тапы проваливаются к виду

    // Лупа «−»/«+» (минус - ASCII: U+2212 нет в шрифтах).
    m->zout = lv_btn_create(box);
    HorizonTheme::styleButton(m->zout, true);
    lv_obj_set_size(m->zout, 56, 44);
    lv_obj_align(m->zout, LV_ALIGN_BOTTOM_RIGHT, -72, -8);
    lv_obj_t *zl1 = lv_label_create(m->zout);
    lv_label_set_text(zl1, "-");
    lv_obj_set_style_text_font(zl1, HorizonTheme::fontBig(), LV_PART_MAIN);
    lv_obj_center(zl1);
    lv_obj_set_user_data(m->zout, (void *)(intptr_t)(-1));
    lv_obj_add_event_cb(m->zout, cbMapZoom, LV_EVENT_CLICKED, this);

    m->zin = lv_btn_create(box);
    HorizonTheme::styleButton(m->zin, true);
    lv_obj_set_size(m->zin, 56, 44);
    lv_obj_align(m->zin, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
    lv_obj_t *zl2 = lv_label_create(m->zin);
    lv_label_set_text(zl2, "+");
    lv_obj_set_style_text_font(zl2, HorizonTheme::fontBig(), LV_PART_MAIN);
    lv_obj_center(zl2);
    lv_obj_set_user_data(m->zin, (void *)(intptr_t)1);
    lv_obj_add_event_cb(m->zin, cbMapZoom, LV_EVENT_CLICKED, this);

    renderView(m);
    mapRefreshMarkers();
}

void HorizonUI::mapRefreshMarkers(void)
{
    if (!mapUi || !mapUi->markers || !lv_obj_is_valid(mapUi->markers))
        return;
    lv_obj_clean(mapUi->markers);
    HzMapStore &st = view->getMapStore();
    addMarkersFor(mapUi, st.baked(), st, true);
    addMarkersFor(mapUi, st.net(), st, false);
    addMarkersFor(mapUi, st.my(), st, false);
    mapUi->lastMarkersMs = nowMs();
}

// Точка прилетела/удалилась из эфира: обновить открытый экран карты.
void HorizonUI::mapOnNetUpdate(void)
{
    if (current != Screen::MAP || !mapUi)
        return;
    HzMapStore &st = view->getMapStore();
    if (mapUi->popupId[0] && !st.find(mapUi->popupId))
        mapClosePopup(); // попап удалённой точки - закрыть
    mapRefreshMarkers();
    if (chatSub && lv_obj_is_valid(chatSub)) {
        char sub[64];
        snprintf(sub, sizeof(sub), "Общих %u/50 · своих %u/10", (unsigned)st.sharedCount(),
                 (unsigned)st.privCount());
        lv_label_set_text(chatSub, sub);
    }
}

void HorizonUI::mapTap(int lx, int ly)
{
    HzMapUi *m = mapUi;
    if (!m)
        return;
    if (m->popup && lv_obj_is_valid(m->popup))
        return; // попап открыт: сначала закрыть
    HzMapStore &st = view->getMapStore();
    const HzPoi *hit = pickAt(m, st, lx, ly);
    if (hit) {
        mapOpenPopup(hit->id);
        return;
    }
    screenToPm(m, lx, ly, &m->formX, &m->formY);
    m->formEditId[0] = '\0';
    buildMapForm();
}

void HorizonUI::mapClosePopup(void)
{
    HzMapUi *m = mapUi;
    if (!m || !m->popup)
        return;
    if (lv_obj_is_valid(m->popup))
        lv_obj_delete(m->popup);
    m->popup = nullptr;
    m->popupId[0] = '\0';
}

void HorizonUI::mapOpenPopup(const char *id)
{
    HzMapUi *m = mapUi;
    if (!m || !id || !*id)
        return;
    HzMapStore &st = view->getMapStore();
    const HzPoi *p = st.find(id);
    if (!p)
        return;
    mapClosePopup();
    strncpy(m->popupId, id, sizeof(m->popupId) - 1);
    m->popupId[sizeof(m->popupId) - 1] = '\0';
    const HorizonPalette &pal = HorizonTheme::current();
    bool mine = st.isMine(id);
    bool canDel = mine || HorizonConfig::instance().rank == 3; // чужие - строго мастер

    lv_obj_t *card = lv_obj_create(scr);
    HorizonTheme::stylePanel(card);
    lv_obj_set_width(card, W() - 60);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(card, 120, LV_PART_MAIN);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(card, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(card, 4, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, hzColor(pal.accent), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 2, LV_PART_MAIN);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 8);

    // Шапка: цветная точка + «тип · имя».
    lv_obj_t *head = lv_obj_create(card);
    lv_obj_set_width(head, lv_pct(100));
    lv_obj_set_height(head, 24);
    lv_obj_set_style_bg_opa(head, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(head, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, 6, LV_PART_MAIN);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *dot = lv_obj_create(head);
    lv_obj_set_size(dot, 14, 14);
    uint32_t col = kPoiColors[p->type < HZ_POI_COUNT ? p->type : 0];
    lv_obj_set_style_bg_color(dot, hzColor(col), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(dot, 7, LV_PART_MAIN);
    lv_obj_t *nm = lv_label_create(head);
    lv_label_set_text_fmt(nm, "%s · %s", kPoiTypes[p->type < kPoiTypeCount ? p->type : 0],
                          p->name.c_str());
    lv_obj_set_style_text_font(nm, HorizonTheme::fontMedium(), LV_PART_MAIN);
    lv_obj_set_flex_grow(nm, 1);
    lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);

    lv_obj_t *snd = lv_label_create(card);
    lv_label_set_text_fmt(snd, "Автор: %s", p->sender.c_str());
    lv_obj_set_style_text_font(snd, HorizonTheme::fontSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(snd, hzColor(pal.textDim), LV_PART_MAIN);
    lv_obj_set_width(snd, lv_pct(100));

    if (!p->comment.empty()) {
        lv_obj_t *cm = lv_label_create(card);
        lv_label_set_text(cm, p->comment.c_str());
        lv_obj_set_style_text_font(cm, HorizonTheme::fontNormal(), LV_PART_MAIN);
        lv_obj_set_width(cm, lv_pct(100));
        lv_label_set_long_mode(cm, LV_LABEL_LONG_WRAP);
    }

    lv_obj_t *row = lv_obj_create(card);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, 44);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 6, LV_PART_MAIN);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    // Кнопки: закрыть всегда; править - свои; удалить - свои или мастер.
    int order[3];
    int nb = 0;
    order[nb++] = 0;
    if (mine)
        order[nb++] = 1;
    if (canDel)
        order[nb++] = 2;
    for (int i = 0; i < nb; i++) {
        int bid = order[i];
        lv_obj_t *b = lv_btn_create(row);
        if (bid == 2)
            HorizonTheme::styleDangerButton(b);
        else
            HorizonTheme::styleButton(b, bid == 1);
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_height(b, 40);
        lv_obj_t *bl = lv_label_create(b);
        lv_label_set_text(bl, bid == 0 ? "Закрыть" : (bid == 1 ? "Править" : "Удалить"));
        lv_obj_center(bl);
        lv_obj_set_user_data(b, (void *)(intptr_t)bid);
        lv_obj_add_event_cb(b, cbMapPopup, LV_EVENT_CLICKED, this);
    }
    m->popup = card;
}

void HorizonUI::buildMapForm(void)
{
    HzMapUi *m = mapUi;
    if (!m) {
        showMap();
        return;
    }
    HzMapStore &st = view->getMapStore();
    bool editing = m->formEditId[0] != '\0';
    const HzPoi *cur = editing ? st.find(m->formEditId) : nullptr;
    if (editing && (!cur || !st.isMine(m->formEditId))) {
        showMap(); // точку уже снесли или чужая
        return;
    }
    clearScreen(Screen::MAP_FORM); // виджеты умерли, буферы карты целы
    HorizonConfig &cfg = HorizonConfig::instance();

    addStatusBar(scr);
    addNavHeader(scr, editing ? "Точка: правка" : "Новая точка", nullptr);

    lv_obj_t *body = lv_obj_create(scr);
    lv_obj_set_pos(body, 0, kStatusH + kHeaderH + 2);
    lv_obj_set_size(body, lv_pct(100), H() - (kStatusH + kHeaderH + 2) - 56);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(body, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(body, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(body, 4, LV_PART_MAIN);

    sectionLabel(body, "Название (до 20 букв)");
    m->formName = lv_textarea_create(body);
    HorizonTheme::styleField(m->formName);
    lv_textarea_set_one_line(m->formName, true);
    lv_textarea_set_max_length(m->formName, 20);
    if (cur)
        lv_textarea_set_text(m->formName, cur->name.c_str());
    else
        lv_textarea_set_placeholder_text(m->formName, "Например: Бар");
    lv_obj_set_width(m->formName, lv_pct(100));
    lv_obj_set_height(m->formName, 40);
    lv_obj_add_event_cb(m->formName, cbInputFocus, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(m->formName, cbInputFocus, LV_EVENT_DEFOCUSED, this);

    sectionLabel(body, "Тип");
    m->formType = lv_dropdown_create(body);
    HorizonTheme::styleField(m->formType);
    {
        char opts[128] = "";
        for (uint8_t i = 0; i < kPoiTypeCount; i++) {
            strcat(opts, kPoiTypes[i]);
            if (i + 1 < kPoiTypeCount)
                strcat(opts, "\n");
        }
        lv_dropdown_set_options(m->formType, opts);
    }
    lv_dropdown_set_selected(m->formType, cur ? cur->type : 0);
    lv_obj_set_width(m->formType, lv_pct(100));
    styleDropdownListLocal(m->formType);

    sectionLabel(body, "Комментарий (до 150 букв)");
    m->formComment = lv_textarea_create(body);
    HorizonTheme::styleField(m->formComment);
    lv_textarea_set_one_line(m->formComment, false);
    lv_textarea_set_max_length(m->formComment, 150);
    if (cur)
        lv_textarea_set_text(m->formComment, cur->comment.c_str());
    else
        lv_textarea_set_placeholder_text(m->formComment, "Что здесь?");
    lv_obj_set_width(m->formComment, lv_pct(100));
    lv_obj_set_height(m->formComment, 64);
    lv_obj_add_event_cb(m->formComment, cbInputFocus, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(m->formComment, cbInputFocus, LV_EVENT_DEFOCUSED, this);

    sectionLabel(body, "Видимость");
    lv_obj_t *shrow = lv_obj_create(body);
    HorizonTheme::stylePanel(shrow);
    lv_obj_set_width(shrow, lv_pct(100));
    lv_obj_set_height(shrow, 52);
    lv_obj_set_flex_flow(shrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(shrow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(shrow, 8, LV_PART_MAIN);
    lv_obj_remove_flag(shrow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *sht = lv_label_create(shrow);
    lv_label_set_text(sht, "Общая (видно всем)");
    m->formShared = lv_switch_create(shrow);
    bool shared = cur ? cur->shared : true;
    if (shared)
        lv_obj_add_state(m->formShared, LV_STATE_CHECKED);
    lv_obj_add_event_cb(m->formShared, cbMapVis, LV_EVENT_VALUE_CHANGED, this);

    m->formChRow = lv_obj_create(body);
    lv_obj_set_width(m->formChRow, lv_pct(100));
    lv_obj_set_height(m->formChRow, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(m->formChRow, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(m->formChRow, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(m->formChRow, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(m->formChRow, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(m->formChRow, 4, LV_PART_MAIN);
    sectionLabel(m->formChRow, "Канал для общей точки");
    m->formCh = channelDropdown(m->formChRow, cur ? cur->ch : cfg.generalCh);
    if (!shared)
        lv_obj_add_flag(m->formChRow, LV_OBJ_FLAG_HIDDEN);

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
    lv_obj_add_event_cb(save, cbMapFormSave, LV_EVENT_CLICKED, this);
}

void HorizonUI::mapSaveForm(void)
{
    HzMapUi *m = mapUi;
    if (!m)
        return;
    const char *nm = m->formName ? lv_textarea_get_text(m->formName) : "";
    const char *cm = m->formComment ? lv_textarea_get_text(m->formComment) : "";
    if (!nm)
        nm = "";
    if (!cm)
        cm = "";
    if (!*nm) {
        toast("Введи название точки");
        return;
    }
    if (hzUtf8Len(nm) > HzMapStore::kMaxNameChars) {
        toast("Название - до 20 букв");
        return;
    }
    if (hzUtf8Len(cm) > HzMapStore::kMaxCommentChars) {
        toast("Комментарий - до 150 букв");
        return;
    }
    uint8_t type = m->formType ? (uint8_t)lv_dropdown_get_selected(m->formType) : 0;
    if (type >= HZ_POI_COUNT)
        type = 0;
    bool shared = m->formShared ? lv_obj_has_state(m->formShared, LV_STATE_CHECKED) : true;
    HorizonConfig &cfg = HorizonConfig::instance();
    uint8_t ch = m->formCh ? (uint8_t)lv_dropdown_get_selected(m->formCh) : cfg.generalCh;
    if (ch > 7)
        ch = cfg.generalCh;
    char name[96], comment[640], sender[96];
    hzUtf8Trunc(name, sizeof(name), nm, HzMapStore::kMaxNameChars);
    hzUtf8Trunc(comment, sizeof(comment), cm, HzMapStore::kMaxCommentChars);
    hzUtf8Trunc(sender, sizeof(sender), cfg.callsign, 20);
    hzSanitizeField(name);
    hzSanitizeField(comment);
    hzSanitizeField(sender);

    HzMapStore &st = view->getMapStore();
    bool editing = m->formEditId[0] != '\0';
    if (editing) {
        HzPoi *e = st.findMut(m->formEditId);
        if (!e) {
            showMap();
            return;
        }
        bool wasShared = e->shared;
        uint8_t oldCh = e->ch;
        // Смена пула: места считаем без себя (точка пока в другом пуле).
        if (shared != wasShared) {
            if (shared && st.sharedCount() >= HzMapStore::kMaxShared) {
                toast("Нет места для точек");
                return;
            }
            if (!shared && st.privCount() >= HzMapStore::kMaxPriv) {
                toast("Нет места для точек");
                return;
            }
        }
        e->type = type;
        e->name = name;
        e->comment = comment;
        e->sender = sender;
        e->shared = shared;
        e->ch = ch;
        st.saveMy();
        if (shared) {
            if (!view->mapSendPoint(*e))
                toast("Нет связи");
            else
                toast("Сохранено");
        } else {
            if (wasShared)
                view->mapSendDelete(e->id, oldCh); // убрать копии из эфира
            toast("Сохранено");
        }
        showMap();
        return;
    }
    // Новая точка.
    HzPoi p;
    if (!view->mapAllocId(p.id, sizeof(p.id))) {
        toast("Нет связи"); // своего номера ещё нет (радио не представлялось)
        return;
    }
    p.type = type;
    p.name = name;
    p.x = (uint16_t)m->formX;
    p.y = (uint16_t)m->formY;
    p.sender = sender;
    p.comment = comment;
    p.shared = shared;
    p.ch = ch;
    if (!st.addMy(p)) {
        toast("Нет места для точек");
        return;
    }
    st.saveMy();
    if (shared && !view->mapSendPoint(p))
        toast("Нет связи"); // точка сохранена локально, в эфир не ушла
    else
        toast("Точка создана");
    showMap();
}

void HorizonUI::cbMapTouch(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    if (!self->mapUi)
        return;
    HzMapUi *m = self->mapUi;
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_RELEASED && m->popup && lv_obj_is_valid(m->popup))
        return; // попап открыт: вид заморожен
    lv_indev_t *indev = lv_indev_active();
    if (!indev)
        return;
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);
    int lx = pt.x; // вид - на всю ширину с x=0
    int ly = pt.y - m->viewY;
    if (code == LV_EVENT_PRESSED) {
        m->down = true;
        m->lastX = lx;
        m->lastY = ly;
        m->moved = 0;
    } else if (code == LV_EVENT_PRESSING) {
        if (!m->down)
            return;
        int dx = lx - m->lastX, dy = ly - m->lastY;
        m->lastX = lx;
        m->lastY = ly;
        m->moved += abs(dx) + abs(dy);
        if (dx || dy) {
            m->ox -= dx / m->scale;
            m->oy -= dy / m->scale;
            clampView(m);
            renderView(m);
            if (nowMs() - m->lastMarkersMs > kMarkersMs)
                self->mapRefreshMarkers();
        }
    } else if (code == LV_EVENT_RELEASED) {
        bool tap = m->down && m->moved < kTapSlopPx;
        m->down = false;
        self->mapRefreshMarkers(); // маркеры точно на места после драга
        if (tap)
            self->mapTap(lx, ly);
    }
}

void HorizonUI::cbMapZoom(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    if (!self->mapUi)
        return;
    HzMapUi *m = self->mapUi;
    int dir = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t *)lv_event_get_current_target(e));
    int ni = (int)m->zoomIdx + dir;
    if (ni < 0)
        ni = 0;
    if (ni >= kZoomCount)
        ni = kZoomCount - 1;
    if (ni == m->zoomIdx)
        return;
    // Держать центр вида на месте.
    float cx = m->ox + (m->viewW / m->scale) / 2;
    float cy = m->oy + (m->viewH / m->scale) / 2;
    m->zoomIdx = (uint8_t)ni;
    m->scale = m->baseScale * kZoomSteps[m->zoomIdx];
    m->ox = cx - (m->viewW / m->scale) / 2;
    m->oy = cy - (m->viewH / m->scale) / 2;
    clampView(m);
    renderView(m);
    self->mapRefreshMarkers();
}

void HorizonUI::cbMapPopup(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    if (!self->mapUi)
        return;
    int bid = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t *)lv_event_get_current_target(e));
    std::string pid = self->mapUi->popupId;
    if (bid == 1) {
        if (!self->view->getMapStore().isMine(pid.c_str()))
            return; // кнопки у чужих нет, но перестраховаться дёшево
        strncpy(self->mapUi->formEditId, pid.c_str(), sizeof(self->mapUi->formEditId) - 1);
        self->mapUi->formEditId[sizeof(self->mapUi->formEditId) - 1] = '\0';
        self->buildMapForm();
    } else if (bid == 2) {
        const HzPoi *p = self->view->getMapStore().find(pid.c_str());
        char q[160];
        snprintf(q, sizeof(q), "Удалить точку «%s»?", p ? p->name.c_str() : pid.c_str());
        self->askConfirm(q, "Удалить", [self, pid]() {
            self->view->mapDeletePoint(pid.c_str());
            self->mapClosePopup();
            self->mapRefreshMarkers();
            self->toast("Точка удалена");
        });
    } else {
        self->mapClosePopup();
    }
}

void HorizonUI::cbMapVis(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    if (!self->mapUi || !self->mapUi->formChRow || !lv_obj_is_valid(self->mapUi->formChRow))
        return;
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_current_target(e);
    if (lv_obj_has_state(sw, LV_STATE_CHECKED))
        lv_obj_remove_flag(self->mapUi->formChRow, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(self->mapUi->formChRow, LV_OBJ_FLAG_HIDDEN);
}

void HorizonUI::cbMapFormSave(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    self->mapSaveForm();
}

#endif // HORIZON_UI
