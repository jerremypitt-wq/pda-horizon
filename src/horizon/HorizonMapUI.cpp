#include "horizon_target.h" // целевые флаги сборки (вкус: standalone/прошивка)
// ПДА «Горизонт» - экран карты и форма точки (см. docs/MAP.md).
#ifdef HORIZON_UI

#include "horizon/HorizonConfig.h"
#include "horizon/HorizonDisplay.h"
#include "horizon/HorizonMap.h"
#include "horizon/HorizonPoiPresentation.h"
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

// Обзор и кадр (~600 КБ) нужны сразу; исходник 1.2 МБ — только при зуме.
// Буферы изображения — PSRAM; при нехватке памяти строки читаются из LittleFS.
// Освобождать обычным free() (годен для ps_malloc).
#if defined(ARDUINO_ARCH_ESP32)
#define HZMAP_ALLOC(n) ps_malloc(n)
#else
#define HZMAP_ALLOC(n) malloc(n)
#endif

using namespace HorizonStrings;

namespace {
constexpr int16_t kStatusH = 22;
// 20x20 A8: ровный треугольник вниз со сглаженными краями (без шрифта).
LV_ATTRIBUTE_MEM_ALIGN const uint8_t kFilterPixels[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xAF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xAF, 0x00, 0x00,
    0x00, 0x00, 0x20, 0xF3, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF3, 0x20, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x78, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x78, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x04, 0xD3, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xD3, 0x04, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x3C, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x3C, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x9B, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x9B, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0xEB, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xEB, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC3, 0xFF, 0xFF, 0xFF, 0xFF, 0xC3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2C, 0xFB, 0xFF, 0xFF, 0xFB, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x87, 0xFF, 0xFF, 0x87, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0xDF, 0xDF, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
// В C++17 вложенные designated initializers (.header.magic) недопустимы.
// Заполняем заголовок по полям, сохраняя дескриптор неизменным для LVGL.
const lv_image_dsc_t kFilterIcon = []() {
    lv_image_dsc_t icon{};
    icon.header.magic = LV_IMAGE_HEADER_MAGIC;
    icon.header.cf = LV_COLOR_FORMAT_A8;
    icon.header.flags = 0;
    icon.header.w = 20;
    icon.header.h = 20;
    icon.header.stride = 20;
    icon.data_size = sizeof(kFilterPixels);
    icon.data = kFilterPixels;
    return icon;
}();

constexpr int16_t kHeaderH = 40;
// Новые карты не более 720×480; 960×640 читаем ради ранее отправленных.
// Для строго 3:2: W=3*k, H=2*k, RGB565 = 12*k*k байт.
constexpr int kMinMapUnit = 160;
constexpr int kMaxMapUnit = 320;
constexpr int kOldMapW = 480;
constexpr int kOldMapH = 320;
// Уровни лупы (множитель к базовому масштабу cover).
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
constexpr uint32_t kPanFrameMs = 40; // не пересчитывать полный кадр на каждом опросе тача

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

// Имена файлов зависят только от постоянного ID, а не от порядка вкладок.
void mapFilePath(char *out, size_t size, uint8_t tab, bool preview)
{
    if (tab)
        snprintf(out, size, "/hzmap/map%u%s.rgb565", (unsigned)tab, preview ? "_preview" : "");
    else
        snprintf(out, size, "/hzmap/map%s.rgb565", preview ? "_preview" : "");
}

bool readMapFile(const char *path, uint8_t *dst, size_t len)
{
    File f = mapFs.open(path, "r");
    if (!f)
        return false;
    size_t n = f.size() == len ? f.readBytes((char *)dst, len) : 0;
    f.close();
    return n == len;
}
} // namespace

// Состояние экрана карты: виджеты умирают с экраном (mapDropWidgets),
// буферы и вид (зум/сдвиг) живут между MAP и MAP_FORM.
struct HzMapMarker {
    lv_obj_t *dot;
    uint16_t x, y; // координаты точки в тысячных долях
};

struct HzMapUi {
    lv_obj_t *img = nullptr;
    lv_obj_t *markers = nullptr;
    lv_obj_t *popup = nullptr;
    lv_obj_t *zin = nullptr;
    lv_obj_t *zout = nullptr;
    lv_obj_t *tag = nullptr;         // бирка с именем выбранной точки
    lv_obj_t *filterPanel = nullptr; // панель фильтров справа
    lv_obj_t *filterRows[7] = {nullptr}; // только галочки, без заголовков разделов
    bool filterOpen = false;
    uint8_t tab = 0;
    lv_obj_t *tabPanel = nullptr;
    bool fType[5] = {true, true, true, true, true};
    bool fShared = true;
    bool fPriv = true;
    char selId[28] = ""; // выбранная точка (первый тап показал имя)
    char pulseId[28] = "";
    uint32_t pulseStartedMs = 0;
    lv_obj_t *pulseRings[2] = {nullptr, nullptr};
    uint32_t lastEmptyMs = 0; // последний тап по пустому (двойной тап)
    int lastEmptyX = 0;
    int lastEmptyY = 0;
    lv_obj_t *formName = nullptr;
    lv_obj_t *formComment = nullptr;
    lv_obj_t *formType = nullptr;
    lv_obj_t *formVis = nullptr;      // Публичная/Частная/Поделиться
    lv_obj_t *formDestRow = nullptr;  // строка списка назначения
    lv_obj_t *formDest = nullptr;     // Общая/Группировка
    lv_obj_t *formPeerRow = nullptr;  // строка выбора контакта
    lv_obj_t *formPeerLabel = nullptr;
    bool destRowShown = true;
    bool peerRowShown = false;
    uint32_t formPeerShown = 0xFFFFFFFFu; // подпись контакта уже отрисована для
    // Черновик формы (пережить выбор контакта на соседнем экране).
    bool formDraft = false;
    char draftName[96] = "";
    char draftComment[640] = "";
    uint8_t draftType = 0;
    uint8_t draftVis = 0;
    uint8_t draftDest = 0;
    uint8_t *mapBuf = nullptr;  // полный исходник, если поместился в PSRAM
    bool mapReady = false;      // файл проверен, даже если большой буфер не выделен
    bool mapLoadTried = false;  // не пробовать ps_malloc(1.2 МБ) на каждом кадре
    uint8_t *previewBuf = nullptr; // сглаженный обзор 480x320 для масштаба 1x
    int mapW = 0, mapH = 0;
    std::vector<HzMapMarker> dots;
    std::vector<int16_t> sampleX; // кэш колонок для пересчёта кадра
    std::vector<uint16_t> rowCache; // до 1920 Б, запасной путь без большой PSRAM
    uint8_t *viewBuf = nullptr; // кадр под текущий вид
    lv_image_dsc_t dsc;
    int viewY = 0;
    int viewW = 0;
    int viewH = 0;
    float baseScale = 1.0f; // базовый масштаб cover (без полей)
    float scale = 1.0f;     // px экрана на px карты
    uint8_t zoomIdx = 0;
    float ox = 0.0f; // какой px карты в левом верхнем углу вида
    float oy = 0.0f;
    bool down = false;
    int lastX = 0;
    int lastY = 0;
    int moved = 0;
    uint32_t lastFrameMs = 0;
    bool framePending = false;
    char popupId[28] = "";
    char formEditId[28] = ""; // "" = новая точка
    int formX = 0;            // тысячные доли для новой точки
    int formY = 0;
};

namespace {
// Индекс списка зависит от ранга, а сохранённое значение назначения - нет.
uint8_t mapFormDestination(lv_obj_t *dropdown)
{
    uint8_t first = HorizonConfig::instance().rank < kRankVeteran ? 1 : 0;
    return dropdown ? (uint8_t)lv_dropdown_get_selected(dropdown) + first : first;
}

// Округление, честное и для отрицательных (кромка вида): (int) режет к нулю.
int rndPx(float v)
{
    return (int)(v + (v >= 0 ? 0.5f : -0.5f));
}

// Точка (тысячные доли) -> px вида.
void poiToScreen(const HzMapUi *m, const HzPoi *p, int *sx, int *sy)
{
    float px = (p->x * (m->mapW - 1)) / 1000.0f;
    float py = (p->y * (m->mapH - 1)) / 1000.0f;
    *sx = rndPx((px - m->ox) * m->scale);
    *sy = rndPx((py - m->oy) * m->scale);
}

// Px вида -> тысячные доли (для новой точки; за краем - clamp).
void screenToPm(const HzMapUi *m, int lx, int ly, int *xPm, int *yPm)
{
    float px = m->ox + lx / m->scale;
    float py = m->oy + ly / m->scale;
    if (px < 0)
        px = 0;
    if (px > m->mapW - 1)
        px = m->mapW - 1;
    if (py < 0)
        py = 0;
    if (py > m->mapH - 1)
        py = m->mapH - 1;
    *xPm = (int)(px * 1000.0f / (m->mapW - 1) + 0.5f);
    *yPm = (int)(py * 1000.0f / (m->mapH - 1) + 0.5f);
}

// Видна ли точка с учётом фильтров (запечённые - общие).
bool mapShown(const HzMapUi *m, const HzPoi &p)
{
    if (p.tab != m->tab || p.type >= HZ_POI_COUNT || !m->fType[p.type])
        return false;
    // Лично отправленная точка хранится с shared=true (нужен send), но на
    // карте по доступу она личная, как и полученная независимая копия.
    if (hzPoiIsPrivateOnMap(p))
        return m->fPriv;
    return m->fShared;
}

void clampView(HzMapUi *m)
{
    float maxOx = m->mapW - m->viewW / m->scale;
    float maxOy = m->mapH - m->viewH / m->scale;
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

// Пересчёт кадра. На масштабе 1x — сглаженный обзор, без загрузки
// исходника 1.2 МБ в PSRAM. При увеличении пробуем кешировать исходник;
// если памяти мало, читаем нужные строки LittleFS по одной (~2 КБ RAM).
// Возвращает false при ошибке чтения; плохой файл не должен ронять UI.
bool renderView(HzMapUi *m)
{
    if (!m->viewBuf || !m->img)
        return false;
    bool preview = m->previewBuf && m->zoomIdx == 0;
    size_t mapBytes = (size_t)m->mapW * m->mapH * 2;
    if (!preview && !m->mapBuf && !m->mapLoadTried) {
        m->mapLoadTried = true;
        uint8_t *buf = (uint8_t *)HZMAP_ALLOC(mapBytes);
        char path[48];
        mapFilePath(path, sizeof(path), m->tab, false);
        if (buf && readMapFile(path, buf, mapBytes))
            m->mapBuf = buf;
        else {
            if (buf)
                free(buf);
            ILOG_INFO("Horizon: map: full PSRAM cache unavailable; use LittleFS rows");
        }
    }
    File file;
    bool stream = !preview && !m->mapBuf;
    if (stream) {
        char path[48];
        mapFilePath(path, sizeof(path), m->tab, false);
        file = mapFs.open(path, "r");
        if (!file || file.size() != mapBytes) {
            if (file)
                file.close();
            ILOG_ERROR("Horizon: map: stream open/size failed");
            return false;
        }
    }
    uint16_t *dst = (uint16_t *)m->viewBuf;
    const uint16_t *src = (const uint16_t *)(preview ? m->previewBuf : m->mapBuf);
    int srcW = preview ? kOldMapW : m->mapW;
    int srcH = preview ? kOldMapH : m->mapH;
    float factor = preview ? kOldMapW / (float)m->mapW : 1.0f;
    float inv = factor / m->scale;
    float left = factor * m->ox;
    float top = factor * m->oy;
    m->sampleX.resize(m->viewW); // сохраняет ёмкость между кадрами
    for (int x = 0; x < m->viewW; x++)
        m->sampleX[x] = (int16_t)(left + x * inv);
    if (stream)
        m->rowCache.resize(srcW);
    int lastSy = -1;
    for (int y = 0; y < m->viewH; y++) {
        int sy = (int)(top + y * inv);
        const uint16_t *row = nullptr;
        if (sy >= 0 && sy < srcH) {
            if (stream) {
                if (sy != lastSy) {
                    size_t offset = (size_t)sy * srcW * 2;
                    if (!file.seek(offset) || file.read((uint8_t *)m->rowCache.data(), srcW * 2) != (size_t)srcW * 2) {
                        file.close();
                        ILOG_ERROR("Horizon: map: row read failed at %u", (unsigned)offset);
                        return false;
                    }
                    lastSy = sy;
                }
                row = m->rowCache.data();
            } else {
                row = src + sy * srcW;
            }
        }
        for (int x = 0; x < m->viewW; x++) {
            int sx = m->sampleX[x];
            *dst++ = row && sx >= 0 && sx < srcW ? row[sx] : 0x0000;
        }
    }
    if (stream)
        file.close();
    if (lv_obj_is_valid(m->img))
        lv_obj_invalidate(m->img);
    m->lastFrameMs = millis();
    m->framePending = false;
    return true;
}

// Переставляем уже созданные маркеры, не удаляя/создавая LVGL-объекты
// каждые 40 мс. В том числе показываем маркеры, вошедшие в вид при панораме.
void moveMarkers(HzMapUi *m)
{
    for (const HzMapMarker &mark : m->dots) {
        float px = (mark.x * (m->mapW - 1)) / 1000.0f;
        float py = (mark.y * (m->mapH - 1)) / 1000.0f;
        int sx = rndPx((px - m->ox) * m->scale);
        int sy = rndPx((py - m->oy) * m->scale);
        if (sx < -20 || sy < -20 || sx > m->viewW + 20 || sy > m->viewH + 20)
            lv_obj_add_flag(mark.dot, LV_OBJ_FLAG_HIDDEN);
        else {
            lv_obj_set_pos(mark.dot, sx - 7, sy - 7);
            lv_obj_remove_flag(mark.dot, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void addMarkersFor(HzMapUi *m, const std::vector<HzPoi> &v, HzMapStore &st, bool baked)
{
    for (const HzPoi &p : v) {
        if ((baked && st.isTombstoned(p.id)) || !mapShown(m, p))
            continue;
        lv_obj_t *dot = lv_obj_create(m->markers);
        lv_obj_set_size(dot, 14, 14);
        uint32_t col = kPoiColors[p.type < HZ_POI_COUNT ? p.type : 0];
        lv_obj_set_style_bg_color(dot, hzColor(col), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(dot, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(dot, hzColor(0x000000), LV_PART_MAIN);
        lv_obj_set_style_radius(dot, 7, LV_PART_MAIN);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        m->dots.push_back({dot, p.x, p.y});
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
            if (!mapShown(m, p))
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
    mapUi->dots.clear();
    mapUi->pulseRings[0] = mapUi->pulseRings[1] = nullptr;
    mapUi->popup = nullptr;
    mapUi->zin = nullptr;
    mapUi->zout = nullptr;
    mapUi->formName = nullptr;
    mapUi->formComment = nullptr;
    mapUi->formType = nullptr;
    mapUi->formVis = nullptr;
    mapUi->formDestRow = nullptr;
    mapUi->formDest = nullptr;
    mapUi->formPeerRow = nullptr;
    mapUi->formPeerLabel = nullptr;
    mapUi->tag = nullptr;
    mapUi->filterPanel = nullptr;
    mapUi->tabPanel = nullptr;
    for (lv_obj_t *&row : mapUi->filterRows)
        row = nullptr;
    mapUi->filterOpen = false;
}

void HorizonUI::mapCleanup(void)
{
    if (!mapUi)
        return;
    if (mapUi->mapBuf) {
        free(mapUi->mapBuf);
        mapUi->mapBuf = nullptr;
    }
    if (mapUi->previewBuf) {
        free(mapUi->previewBuf);
        mapUi->previewBuf = nullptr;
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
    if (!st.hasTab(m->tab)) m->tab = st.tabs().front().id;
    m->formDraft = false; // черновик жил только для выбора контакта
    // Пока не трогать ориентацию: ошибка файла не должна мигать экраном.
    if (!m->mapReady) {
        char path[48];
        mapFilePath(path, sizeof(path), m->tab, false);
        File mf = mapFs.open(path, "r");
        size_t bytes = mf ? mf.size() : 0;
        if (mf)
            mf.close();
        for (int unit = kMinMapUnit; unit <= kMaxMapUnit; unit++) {
            if (bytes == (size_t)12 * unit * unit) {
                m->mapW = 3 * unit;
                m->mapH = 2 * unit;
                break;
            }
        }
        if (!m->mapW) {
            ILOG_ERROR("Horizon: map: invalid file size %u (expected 3:2, 480x320..960x640)", (unsigned)bytes);
            showMenu();
            toast("Карта: неверный размер файла (%u)", (unsigned)bytes);
            return;
        }
        if (m->mapW > kOldMapW) {
            // Обзор всего 300 КБ: на обычном масштабе крупный буфер не нужен.
            size_t n = (size_t)kOldMapW * kOldMapH * 2;
            m->previewBuf = (uint8_t *)HZMAP_ALLOC(n);
            mapFilePath(path, sizeof(path), m->tab, true);
            if (m->previewBuf && !readMapFile(path, m->previewBuf, n)) {
                free(m->previewBuf);
                m->previewBuf = nullptr; // обзор необязателен: запасной путь — строки карты
            }
        }
        // Старый файл маленький: кешируем, если есть память. Неуспех не
        // критичен — renderView сможет читать строки и старого формата.
        if (m->mapW == kOldMapW) {
            m->mapLoadTried = true;
            m->mapBuf = (uint8_t *)HZMAP_ALLOC(bytes);
            mapFilePath(path, sizeof(path), m->tab, false);
            if (m->mapBuf && !readMapFile(path, m->mapBuf, bytes)) {
                free(m->mapBuf);
                m->mapBuf = nullptr;
            }
        }
        m->mapReady = true;
        ILOG_INFO("Horizon: map: %ux%u, preview %s, cache %s",
                  (unsigned)m->mapW, (unsigned)m->mapH,
                  m->previewBuf ? "yes" : "no", m->mapBuf ? "yes" : "no");
    }
    // Карта всегда альбомная. При выходе clearScreen вернёт настройку ПДА.
    horizonSetMapLandscape(view->getDisplayDriver(), true);
    // Карта занимает и область статусбара: затемнённый полупрозрачный бар поверх картинки.
    m->viewY = 0;
    m->viewW = W();
    m->viewH = H();
    if (!m->viewBuf) {
        // Cover, не fit: карта всегда заливает вид целиком, фона не видно
        // вообще (лишнее обрезается и доступно двигом).
        float fit = m->viewW / (float)m->mapW;
        float fitH = m->viewH / (float)m->mapH;
        if (fitH > fit)
            fit = fitH;
        m->baseScale = fit;
        m->scale = fit * kZoomSteps[m->zoomIdx];
        clampView(m);
        size_t viewBytes = (size_t)m->viewW * m->viewH * 2;
        m->viewBuf = (uint8_t *)HZMAP_ALLOC(viewBytes);
        if (!m->viewBuf && m->previewBuf) {
            // Последний шанс: освободить обзор и читать карту из ФС построчно.
            free(m->previewBuf);
            m->previewBuf = nullptr;
            m->viewBuf = (uint8_t *)HZMAP_ALLOC(viewBytes);
        }
        if (!m->viewBuf && m->mapBuf) {
            free(m->mapBuf);
            m->mapBuf = nullptr;
            m->mapLoadTried = true;
            m->viewBuf = (uint8_t *)HZMAP_ALLOC(viewBytes);
        }
        if (!m->viewBuf) {
            ILOG_ERROR("Horizon: map: view alloc failed (%u bytes)",
                       (unsigned)((size_t)m->viewW * m->viewH * 2));
            showMenu(); // сам освободит карту и вернёт ориентацию
            toast("Карта: не хватает памяти для экрана");
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

    // Картинка рисуется первой; полупрозрачный статусбар добавим последним поверх.
    // Нулевые отступы обязательны: тапы и маркеры
    // считаются от начала картинки, любой pad темы даст сдвиг.
    lv_obj_t *box = lv_obj_create(scr);
    lv_obj_set_pos(box, 0, m->viewY);
    lv_obj_set_size(box, m->viewW, m->viewH);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(box, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(box, 0, LV_PART_MAIN);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    m->img = lv_image_create(box);
    lv_image_set_src(m->img, &m->dsc);
    lv_obj_set_pos(m->img, 0, 0);
    lv_obj_set_size(m->img, m->viewW, m->viewH); // хит-зона - весь вид
    // lv_image по умолчанию не кликабельна: без флага тапы/драг мёртвые.
    lv_obj_add_flag(m->img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(m->img, cbMapTouch, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(m->img, cbMapTouch, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(m->img, cbMapTouch, LV_EVENT_RELEASED, this);

    m->markers = lv_obj_create(box);
    lv_obj_set_pos(m->markers, 0, 0);
    lv_obj_set_size(m->markers, m->viewW, m->viewH);
    lv_obj_set_style_bg_opa(m->markers, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(m->markers, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(m->markers, 0, LV_PART_MAIN); // точки ровно на местах
    lv_obj_remove_flag(m->markers, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(m->markers, LV_OBJ_FLAG_CLICKABLE); // тапы проваливаются к виду

    // Бирка с именем выбранной точки (фон - по ширине имени).
    m->tag = lv_label_create(box);
    lv_label_set_text(m->tag, "");
    lv_obj_set_style_text_font(m->tag, HorizonTheme::fontSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(m->tag, hzColor(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_color(m->tag, hzColor(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(m->tag, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(m->tag, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(m->tag, 2, LV_PART_MAIN);
    lv_obj_remove_flag(m->tag, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(m->tag, LV_OBJ_FLAG_HIDDEN);

    // Управление ниже прозрачного бара, чтобы не закрыть показатели.
    lv_obj_t *back = lv_btn_create(box);
    HorizonTheme::styleButton(back, true);
    lv_obj_set_size(back, 44, 32);
    lv_obj_set_pos(back, 4, kStatusH + 4);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, "<");
    lv_obj_set_style_text_font(bl, HorizonTheme::fontBig(), LV_PART_MAIN);
    lv_obj_center(bl);
    lv_obj_add_event_cb(back, cbBack, LV_EVENT_CLICKED, this);

    lv_obj_t *flt = lv_btn_create(box);
    HorizonTheme::styleButton(flt, true);
    lv_obj_set_size(flt, 42, 32);
    lv_obj_set_pos(flt, W() - 46, kStatusH + 4);
    lv_obj_t *fl = lv_image_create(flt);
    lv_image_set_src(fl, &kFilterIcon);
    lv_obj_set_style_image_recolor(fl, hzColor(HorizonTheme::current().accentText), LV_PART_MAIN);
    lv_obj_set_style_image_recolor_opa(fl, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_center(fl);
    lv_obj_add_event_cb(flt, cbMapFilter, LV_EVENT_CLICKED, this);

    // Лупа - слева посередине вида.
    int midY = m->viewH / 2;
    m->zin = lv_btn_create(box);
    HorizonTheme::styleButton(m->zin, true);
    lv_obj_set_size(m->zin, 44, 40);
    lv_obj_set_pos(m->zin, 4, midY - 42);
    lv_obj_t *zl2 = lv_label_create(m->zin);
    lv_label_set_text(zl2, "+");
    lv_obj_set_style_text_font(zl2, HorizonTheme::fontBig(), LV_PART_MAIN);
    lv_obj_center(zl2);
    lv_obj_set_user_data(m->zin, (void *)(intptr_t)1);
    lv_obj_add_event_cb(m->zin, cbMapZoom, LV_EVENT_CLICKED, this);

    // Минус - ASCII (U+2212 нет в шрифтах).
    m->zout = lv_btn_create(box);
    HorizonTheme::styleButton(m->zout, true);
    lv_obj_set_size(m->zout, 44, 40);
    lv_obj_set_pos(m->zout, 4, midY + 2);
    lv_obj_t *zl1 = lv_label_create(m->zout);
    lv_label_set_text(zl1, "-");
    lv_obj_set_style_text_font(zl1, HorizonTheme::fontBig(), LV_PART_MAIN);
    lv_obj_center(zl1);
    lv_obj_set_user_data(m->zout, (void *)(intptr_t)(-1));
    lv_obj_add_event_cb(m->zout, cbMapZoom, LV_EVENT_CLICKED, this);

    if (st.tabs().size() > 1) {
        // Вкладки доступны снизу слева; меню идёт вверх и не перекрывает статус.
        lv_obj_t *tabBtn = lv_btn_create(box);
        HorizonTheme::styleButton(tabBtn, true);
        lv_obj_set_size(tabBtn, 170, 38);
        lv_obj_align(tabBtn, LV_ALIGN_BOTTOM_LEFT, 4, -4);
        lv_obj_t *title = lv_label_create(tabBtn);
        for (const HzMapTab &t : st.tabs())
            if (t.id == m->tab) lv_label_set_text(title, t.name.c_str());
        lv_obj_set_style_text_font(title, HorizonTheme::fontSmall(), LV_PART_MAIN);
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        lv_obj_set_width(title, 154);
        lv_obj_center(title);
        lv_obj_add_event_cb(tabBtn, cbMapTabs, LV_EVENT_CLICKED, this);
    }

    // Бар поверх картинки: тёмная полупрозрачная подложка только под белым текстом.
    // Карта по-прежнему заполняет весь экран, включая область под баром.
    lv_obj_t *bar = addStatusBar(scr);
    lv_obj_set_style_bg_color(bar, hzColor(0x0A0D0C), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(bar, hzColor(0xFFFFFF), LV_PART_MAIN);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE); // тап по карте проходит сквозь бар

    if (!renderView(m)) {
        showMenu();
        toast("Карта: ошибка чтения данных");
        return;
    }
    mapRefreshMarkers();
    mapPlaceTag();
}

void HorizonUI::mapRefreshMarkers(void)
{
    if (!mapUi || !mapUi->markers || !lv_obj_is_valid(mapUi->markers))
        return;
    lv_obj_clean(mapUi->markers);
    mapUi->pulseRings[0] = mapUi->pulseRings[1] = nullptr;
    mapUi->dots.clear();
    HzMapStore &st = view->getMapStore();
    addMarkersFor(mapUi, st.baked(), st, true);
    addMarkersFor(mapUi, st.net(), st, false);
    addMarkersFor(mapUi, st.my(), st, false);
    moveMarkers(mapUi);
}

void HorizonUI::onMapPoint(const char *id, const char *name)
{
    if (current == Screen::LOCK)
        return; // точка хранится в View, но уведомление не раскрывает содержимое
    if (!id || !hzPoiIdOk(id))
        return;
    if (pendingMapId[0]) {
        const HzPoi *point = view->getMapStore().find(id);
        if (!strcmp(pendingMapId, id) || (point && !strcmp(pendingMapId, point->sourceId))) {
            pendingMapId[0] = '\0';
            mapOpenPoint(id); // ссылка пришла раньше многочастевой точки
            return;
        }
    }
    toast("Точка: %s", name ? name : "");
    strncpy(toastMapId, id, sizeof(toastMapId) - 1);
    toastMapId[sizeof(toastMapId) - 1] = '\0';
    toastHasTarget = true;
    if (toastBtn && lv_obj_is_valid(toastBtn))
        lv_obj_add_flag(toastBtn, LV_OBJ_FLAG_CLICKABLE);
}

void HorizonUI::mapOpenPoint(const char *id)
{
    if (current == Screen::LOCK)
        return;
    if (!id || !hzPoiIdOk(id))
        return;
    const HzMapStore &st = view->getMapStore();
    // Если один исходный ID присылали и в общий эфир, и лично, ссылка из
    // личного чата должна вести именно к моей независимой копии.
    const HzPoi *point = st.findBySource(id);
    if (!point)
        point = st.find(id);
    if (!point) {
        if (st.directDismissed(id) || id[0] == 'B' ||
            id[0] == 'F' ||
            hzPoiAuthorNode(id) == view->getMyNodeNum()) {
            toast("Точка удалена"); // ссылка осталась в истории, но копии уже нет
            return;
        }
        strncpy(pendingMapId, id, sizeof(pendingMapId) - 1);
        pendingMapId[sizeof(pendingMapId) - 1] = '\0';
        showMap();
        toast("Точка ещё принимается");
        return;
    }
    if (!st.hasTab(point->tab)) {
        toast("Вкладка точки удалена или недоступна");
        return;
    }
    char localId[28];
    strncpy(localId, point->id, sizeof(localId) - 1);
    localId[sizeof(localId) - 1] = '\0';
    if (!mapUi) mapUi = new HzMapUi();
    if (mapUi->tab != point->tab) {
        mapUi->tab = point->tab;
        mapUi->mapReady = false;
        mapUi->mapW = mapUi->mapH = 0;
        if (mapUi->mapBuf) { free(mapUi->mapBuf); mapUi->mapBuf = nullptr; }
        if (mapUi->previewBuf) { free(mapUi->previewBuf); mapUi->previewBuf = nullptr; }
        mapUi->mapLoadTried = false;
        mapUi->zoomIdx = 0;
        if (mapUi->viewBuf) { free(mapUi->viewBuf); mapUi->viewBuf = nullptr; }
    }
    showMap();
    if (current != Screen::MAP || !mapUi)
        return;
    HzMapUi *m = mapUi;
    point = st.find(localId); // после перехода указатели на пулы не держим
    if (!point)
        return;
    m->fType[point->type < HZ_POI_COUNT ? point->type : 0] = true;
    if (hzPoiIsPrivateOnMap(*point)) m->fPriv = true;
    else m->fShared = true;
    m->ox = point->x * (m->mapW - 1) / 1000.0f - m->viewW / (2 * m->scale);
    m->oy = point->y * (m->mapH - 1) / 1000.0f - m->viewH / (2 * m->scale);
    clampView(m);
    renderView(m);
    mapRefreshMarkers();
    strncpy(m->selId, localId, sizeof(m->selId) - 1);
    m->selId[sizeof(m->selId) - 1] = '\0';
    mapPlaceTag();
    strncpy(m->pulseId, localId, sizeof(m->pulseId) - 1);
    m->pulseId[sizeof(m->pulseId) - 1] = '\0';
    m->pulseStartedMs = nowMs();
    mapPulseTick(m->pulseStartedMs);
}

void HorizonUI::mapPulseTick(uint32_t now)
{
    HzMapUi *m = mapUi;
    if (!m || current != Screen::MAP || !m->pulseId[0] || !m->markers || !lv_obj_is_valid(m->markers))
        return;
    const HzPoi *point = view->getMapStore().find(m->pulseId);
    uint32_t elapsed = now - m->pulseStartedMs;
    if (!point || elapsed >= 2500) { // 5 волн через каждые полсекунды
        for (lv_obj_t *&ring : m->pulseRings) {
            if (ring && lv_obj_is_valid(ring)) lv_obj_delete(ring);
            ring = nullptr;
        }
        m->pulseId[0] = '\0';
        return;
    }
    int sx = 0, sy = 0;
    poiToScreen(m, point, &sx, &sy);
    uint32_t phase = elapsed % 500;
    for (int i = 0; i < 2; i++) {
        if (!m->pulseRings[i]) {
            lv_obj_t *ring = lv_obj_create(m->markers);
            lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(ring, 2, LV_PART_MAIN);
            lv_obj_set_style_border_color(ring, hzColor(HorizonTheme::current().accent), LV_PART_MAIN);
            lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, LV_PART_MAIN);
            lv_obj_set_style_pad_all(ring, 0, LV_PART_MAIN);
            lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_remove_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
            m->pulseRings[i] = ring;
        }
        int age = (int)phase - i * 150;
        if (age < 0 || age >= 450) {
            lv_obj_add_flag(m->pulseRings[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        int radius = 10 + age * 30 / 450; // расходятся недалеко от маркера
        lv_obj_set_size(m->pulseRings[i], radius * 2, radius * 2);
        lv_obj_set_pos(m->pulseRings[i], sx - radius, sy - radius);
        lv_obj_set_style_border_opa(m->pulseRings[i], (lv_opa_t)(220 - age * 190 / 450), LV_PART_MAIN);
        lv_obj_remove_flag(m->pulseRings[i], LV_OBJ_FLAG_HIDDEN);
    }
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
    mapPlaceTag();
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
        m->lastEmptyMs = 0; // сбить двойной тап
        if (m->selId[0] && !strcmp(m->selId, hit->id))
            mapOpenPopup(hit->id); // второй тап - карточка
        else {
            strncpy(m->selId, hit->id, sizeof(m->selId) - 1);
            m->selId[sizeof(m->selId) - 1] = '\0';
            mapPlaceTag(); // первый тап - имя
        }
        return;
    }
    // Пустое: второй тап рядом и быстро - новая точка.
    uint32_t now = nowMs();
    int dx = lx - m->lastEmptyX, dy = ly - m->lastEmptyY;
    if (m->lastEmptyMs && now - m->lastEmptyMs < 400 && dx * dx + dy * dy < 24 * 24) {
        m->lastEmptyMs = 0;
        m->selId[0] = '\0';
        screenToPm(m, lx, ly, &m->formX, &m->formY);
        m->formEditId[0] = '\0';
        buildMapForm();
        return;
    }
    m->lastEmptyMs = now;
    m->lastEmptyX = lx;
    m->lastEmptyY = ly;
    if (m->selId[0]) {
        m->selId[0] = '\0'; // тап по пустому снимает выбор
        mapPlaceTag();
    }
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
    m->selId[0] = '\0';
    if (m->tag && lv_obj_is_valid(m->tag))
        lv_obj_add_flag(m->tag, LV_OBJ_FLAG_HIDDEN); // карточка вместо бирки
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
    lv_obj_set_style_max_height(card, m->viewH - 16, LV_PART_MAIN); // длинный комментарий прокручивается
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(card, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(card, 4, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, hzColor(pal.accent), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 2, LV_PART_MAIN);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE); // тапы гаснут в карточке
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 8);

    // Только название точки (тип и точку-маркер не пишем).
    lv_obj_t *nm = lv_label_create(card);
    lv_label_set_text(nm, p->name.c_str());
    lv_obj_set_style_text_font(nm, HorizonTheme::fontMedium(), LV_PART_MAIN);
    lv_obj_set_width(nm, lv_pct(100));
    lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);

    const HorizonConfig &cfg = HorizonConfig::instance();
    HzPoiAccess access = hzPoiAccess(*p, cfg.generalCh, cfg.groupCh);
    if (access != HzPoiAccess::Received) {
        lv_obj_t *author = lv_label_create(card);
        lv_label_set_text_fmt(author, "Автор: %s", p->sender.c_str());
        lv_obj_set_style_text_font(author, HorizonTheme::fontSmall(), LV_PART_MAIN);
        lv_obj_set_style_text_color(author, hzColor(pal.textDim), LV_PART_MAIN);
        lv_obj_set_width(author, lv_pct(100));
    }
    lv_obj_t *scope = lv_label_create(card);
    if (access == HzPoiAccess::Received) {
        // В sourceId сохранён номер отправителя; досье могло обновиться после
        // приёма. Без досье берём позывной, записанный вместе с точкой.
        const char *contact = p->sender.c_str();
        uint32_t authorId = hzPoiAuthorNode(p->sourceId);
        const HorizonDossierInfo *dossier = view->getStore().findDossier(authorId);
        if (dossier && dossier->callsign[0])
            contact = dossier->callsign;
        lv_label_set_text_fmt(scope, "Получена от контакта: %s", contact);
    } else {
        lv_label_set_text(scope, access == HzPoiAccess::Group ? "Группировка" :
                                 access == HzPoiAccess::Personal ? "Личная" : "Общая");
    }
    lv_obj_set_style_text_font(scope, HorizonTheme::fontSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(scope, hzColor(pal.textDim), LV_PART_MAIN);
    lv_obj_set_width(scope, lv_pct(100));
    lv_label_set_long_mode(scope, LV_LABEL_LONG_WRAP);

    if (!p->comment.empty()) {
        lv_obj_t *cm = lv_label_create(card);
        lv_label_set_text(cm, p->comment.c_str());
        lv_obj_set_style_text_font(cm, HorizonTheme::fontNormal(), LV_PART_MAIN);
        lv_obj_set_width(cm, lv_pct(100));
        lv_label_set_long_mode(cm, LV_LABEL_LONG_WRAP);
    }

    lv_obj_t *row = lv_obj_create(card);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, 40);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, 4, LV_PART_MAIN);
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
        lv_obj_set_height(b, 36);
        lv_obj_t *bl = lv_label_create(b);
        lv_label_set_text(bl, bid == 0 ? "Закрыть" : (bid == 1 ? "Править" : "Удалить"));
        lv_obj_set_style_text_font(bl, HorizonTheme::fontSmall(), LV_PART_MAIN);
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

    // Значения: черновик (возврат с выбора контакта) > точка (правка) > дефолты.
    const char *vName = "";
    const char *vComment = "";
    uint8_t vType = 0, vVis = 0, vDest = 0;
    if (m->formDraft) {
        vName = m->draftName;
        vComment = m->draftComment;
        vType = m->draftType;
        vVis = m->draftVis;
        vDest = m->draftDest;
    } else if (cur) {
        mapPickPeer = 0;
        vName = cur->name.c_str();
        vComment = cur->comment.c_str();
        vType = cur->type;
        vVis = cur->sourceId[0] ? 1 : (cur->dmPeer ? 2 : (cur->shared ? 0 : 1));
        if (cur->dmPeer)
            mapPickPeer = cur->dmPeer;
        vDest = (cur->ch == cfg.groupCh && cfg.groupCh != cfg.generalCh) ? 1 : 0;
    } else {
        mapPickPeer = 0;
    }
    if (vType >= HZ_POI_COUNT)
        vType = 0;
    if (vVis > 2)
        vVis = 0;
    if (vDest > 1)
        vDest = 0;
    if (cfg.rank < kRankVeteran && vDest == 0)
        vDest = 1; // ранее созданная общая точка при понижении ранга

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
    if (*vName)
        lv_textarea_set_text(m->formName, vName);
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
    lv_dropdown_set_selected(m->formType, vType);
    lv_obj_set_width(m->formType, lv_pct(100));
    styleDropdownListLocal(m->formType);

    sectionLabel(body, "Комментарий (до 150 букв)");
    m->formComment = lv_textarea_create(body);
    HorizonTheme::styleField(m->formComment);
    lv_textarea_set_one_line(m->formComment, false);
    lv_textarea_set_max_length(m->formComment, 150);
    if (*vComment)
        lv_textarea_set_text(m->formComment, vComment);
    else
        lv_textarea_set_placeholder_text(m->formComment, "Что здесь?");
    lv_obj_set_width(m->formComment, lv_pct(100));
    lv_obj_set_height(m->formComment, 64);
    lv_obj_add_event_cb(m->formComment, cbInputFocus, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(m->formComment, cbInputFocus, LV_EVENT_DEFOCUSED, this);

    sectionLabel(body, "Видимость");
    m->formVis = lv_dropdown_create(body);
    HorizonTheme::styleField(m->formVis);
    lv_dropdown_set_options(m->formVis, "Публичная\nЧастная\nПоделиться");
    lv_dropdown_set_selected(m->formVis, vVis);
    if (cur && cur->sourceId[0])
        lv_obj_add_state(m->formVis, LV_STATE_DISABLED); // чужую копию меняем только локально
    lv_obj_set_width(m->formVis, lv_pct(100));
    styleDropdownListLocal(m->formVis);

    // Строка назначения (видна только для публичной).
    m->formDestRow = lv_obj_create(body);
    lv_obj_set_width(m->formDestRow, lv_pct(100));
    lv_obj_set_height(m->formDestRow, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(m->formDestRow, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(m->formDestRow, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(m->formDestRow, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(m->formDestRow, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(m->formDestRow, 4, LV_PART_MAIN);
    sectionLabel(m->formDestRow, "Кому видна точка");
    m->formDest = lv_dropdown_create(m->formDestRow);
    HorizonTheme::styleField(m->formDest);
    lv_dropdown_set_options(m->formDest, cfg.rank < kRankVeteran ? "Группировка"
                                                    : "Общая\nГруппировка");
    lv_dropdown_set_selected(m->formDest, vDest - (cfg.rank < kRankVeteran ? 1 : 0));
    lv_obj_set_width(m->formDest, lv_pct(100));
    styleDropdownListLocal(m->formDest);

    // Строка выбора контакта (видна только в режиме «Поделиться»).
    m->formPeerRow = lv_btn_create(body);
    HorizonTheme::styleButton(m->formPeerRow, false);
    lv_obj_set_width(m->formPeerRow, lv_pct(100));
    lv_obj_set_height(m->formPeerRow, 44);
    m->formPeerLabel = lv_label_create(m->formPeerRow);
    lv_obj_set_style_text_font(m->formPeerLabel, HorizonTheme::fontNormal(), LV_PART_MAIN);
    lv_obj_align(m->formPeerLabel, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_add_event_cb(m->formPeerRow, cbMapPeerPick, LV_EVENT_CLICKED, this);

    // Начальное состояние строк - против значений, чтобы синхронизация применила.
    m->destRowShown = (vVis != 0);
    m->peerRowShown = (vVis != 2);
    m->formPeerShown = 0xFFFFFFFFu;
    mapSyncFormRows();

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
    uint8_t vis = m->formVis ? (uint8_t)lv_dropdown_get_selected(m->formVis) : 0;
    if (vis > 2)
        vis = 0;
    bool shared = (vis != 1);
    bool sendingDm = (vis == 2);
    HorizonConfig &cfg = HorizonConfig::instance();
    uint8_t dest = mapFormDestination(m->formDest);
    if (dest > 1)
        dest = cfg.rank < kRankVeteran ? 1 : 0;
    // В том числе при правке старой общей метки: запрещаем саму отправку,
    // а не только скрываем пункт списка.
    if (vis == 0 && cfg.rank < kRankVeteran && (dest == 0 || cfg.groupCh == cfg.generalCh)) {
        toast("Для твоего ранга общий канал недоступен");
        return;
    }
    uint32_t peer = sendingDm ? mapPickPeer : 0;
    if (sendingDm && (!peer || peer == 0xFFFFFFFFu)) {
        toast("Выбери контакт");
        return;
    }
    uint8_t ch = cfg.generalCh;
    if (vis == 0 && dest == 1)
        ch = cfg.groupCh;
    else if (peer)
        ch = view->mapDmChannel(peer);
    // Отправка общей точки: в канал или в личку.
    auto sendShared = [&](HzPoi &e) -> bool {
        if (e.dmPeer)
            return view->mapSendPointDm(e, e.dmPeer);
        return view->mapSendPoint(e);
    };
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
        bool imported = e->sourceId[0] != '\0';
        if (imported) { // личная копия чужой точки не становится общедоступной
            shared = false;
            peer = 0;
        }
        bool wasShared = e->shared;
        uint8_t oldCh = e->ch;
        uint32_t oldPeer = e->dmPeer;
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
        if (!imported)
            e->sender = sender; // автор личной копии навсегда остаётся исходным
        e->shared = shared;
        e->ch = ch;
        e->dmPeer = peer;
        st.saveMy();
        bool destChanged = (peer != oldPeer) || (!peer && ch != oldCh);
        if (wasShared && (!shared || destChanged)) {
            if (oldPeer)
                view->mapSendDeleteDm(e->id, oldPeer); // убрать DM-копию у пира
            else
                view->mapSendDelete(e->id, oldCh); // убрать копии из эфира
        }
        if (shared) {
            if (!sendShared(*e))
                toast("Нет связи");
            else
                toast("Сохранено");
        } else {
            toast("Сохранено");
        }
        m->formDraft = false;
        mapPickPeer = 0;
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
    p.tab = m->tab;
    p.name = name;
    p.x = (uint16_t)m->formX;
    p.y = (uint16_t)m->formY;
    p.sender = sender;
    p.comment = comment;
    p.shared = shared;
    p.ch = ch;
    p.dmPeer = peer;
    if (!st.addMy(p)) {
        toast("Нет места для точек");
        return;
    }
    st.saveMy();
    if (shared && !sendShared(p))
        toast("Нет связи"); // точка сохранена локально, в эфир не ушла
    else
        toast("Точка создана");
    m->formDraft = false;
    mapPickPeer = 0;
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
    if (!m->img || !lv_obj_is_valid(m->img))
        return;
    // Начало вида - от самой картинки (переживёт любые отступы темы).
    lv_area_t crd;
    lv_obj_get_coords(m->img, &crd);
    int lx = pt.x - crd.x1;
    int ly = pt.y - crd.y1;
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
            m->framePending = true;
            if (nowMs() - m->lastFrameMs >= kPanFrameMs) {
                renderView(m);
                moveMarkers(m);
                self->mapPlaceTag(); // бирка едет за маркером
            }
        }
    } else if (code == LV_EVENT_RELEASED) {
        bool tap = m->down && m->moved < kTapSlopPx;
        m->down = false;
        if (m->framePending)
            renderView(m); // последнее положение пальца всегда отрисовано
        moveMarkers(m);
        self->mapPlaceTag();
        if (tap)
            self->mapTap(lx, ly);
    }
}

void HorizonUI::cbMapTabs(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    HzMapUi *m = self->mapUi;
    if (!m) return;
    if (m->tabPanel && lv_obj_is_valid(m->tabPanel)) {
        if (lv_obj_has_flag(m->tabPanel, LV_OBJ_FLAG_HIDDEN))
            lv_obj_remove_flag(m->tabPanel, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(m->tabPanel, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const auto &tabs = self->view->getMapStore().tabs();
    lv_obj_t *panel = lv_obj_create(self->scr);
    m->tabPanel = panel;
    HorizonTheme::stylePanel(panel);
    lv_obj_set_size(panel, 190, (int)tabs.size() * 42 + 12);
    lv_obj_align(panel, LV_ALIGN_BOTTOM_LEFT, 4, -46);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(panel, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_row(panel, 2, LV_PART_MAIN);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    for (const HzMapTab &tab : tabs) {
        lv_obj_t *btn = lv_btn_create(panel);
        HorizonTheme::styleButton(btn, tab.id == m->tab);
        lv_obj_set_size(btn, lv_pct(100), 38);
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, tab.name.c_str());
        lv_obj_set_style_text_font(label, HorizonTheme::fontSmall(), LV_PART_MAIN);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(label, 165);
        lv_obj_center(label);
        lv_obj_set_user_data(btn, (void *)(intptr_t)(tab.id + 1));
        lv_obj_add_event_cb(btn, cbMapTabSelect, LV_EVENT_CLICKED, self);
    }
}

void HorizonUI::cbMapTabSelect(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    if (!self->mapUi) return;
    uint8_t id = (uint8_t)((intptr_t)lv_obj_get_user_data((lv_obj_t *)lv_event_get_current_target(e)) - 1);
    HzMapUi *m = self->mapUi;
    if (id == m->tab) {
        // Callback выполняется на дочерней кнопке панели: не удалять её синхронно.
        lv_obj_add_flag(m->tabPanel, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (!self->view->getMapStore().hasTab(id)) return;
    if (m->mapBuf) { free(m->mapBuf); m->mapBuf = nullptr; }
    if (m->previewBuf) { free(m->previewBuf); m->previewBuf = nullptr; }
    if (m->viewBuf) { free(m->viewBuf); m->viewBuf = nullptr; }
    m->mapReady = m->mapLoadTried = false;
    m->mapW = m->mapH = 0;
    m->tab = id;
    m->zoomIdx = 0;
    m->ox = m->oy = 0;
    m->selId[0] = m->pulseId[0] = '\0';
    self->showMap();
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
    moveMarkers(m);
    self->mapPlaceTag();
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

void HorizonUI::cbMapFormSave(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    self->mapSaveForm();
}

// Бирка с именем выбранной точки (над маркером, фон - по ширине имени).
void HorizonUI::mapPlaceTag(void)
{
    HzMapUi *m = mapUi;
    if (!m || !m->tag || !lv_obj_is_valid(m->tag))
        return;
    const HzPoi *p = nullptr;
    if (m->selId[0]) {
        p = view->getMapStore().find(m->selId);
        if (p && !mapShown(m, *p))
            p = nullptr; // точку скрыл фильтр
    }
    if (!p) {
        m->selId[0] = '\0';
        lv_obj_add_flag(m->tag, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    int sx = 0, sy = 0;
    poiToScreen(m, p, &sx, &sy);
    // Ширина бирки - оценка 7 px/символ (fontSmall), clamp в вид.
    int est = (int)hzUtf8Len(p->name.c_str()) * 7 + 12;
    int tx = sx - est / 2;
    if (tx < 2)
        tx = 2;
    if (tx + est > m->viewW - 2)
        tx = m->viewW - 2 - est;
    if (tx < 2)
        tx = 2;
    int ty = sy - 30;
    if (ty < 2)
        ty = sy + 14;
    lv_label_set_text(m->tag, p->name.c_str());
    lv_obj_set_pos(m->tag, tx, ty);
    lv_obj_remove_flag(m->tag, LV_OBJ_FLAG_HIDDEN);
}

void HorizonUI::mapToggleFilter(void)
{
    HzMapUi *m = mapUi;
    if (!m)
        return;
    if (!m->filterPanel || !lv_obj_is_valid(m->filterPanel))
        m->filterOpen = false;
    if (!m->filterOpen) {
        mapBuildFilterPanel(); // заново: свежие счётчики
        return;
    }
    m->filterOpen = false;
    if (m->filterPanel && lv_obj_is_valid(m->filterPanel))
        lv_obj_add_flag(m->filterPanel, LV_OBJ_FLAG_HIDDEN);
}

// Панель фильтров справа под кнопкой. Прокрутка нужна, чтобы все 7 строк
// и «Сбросить все» помещались даже на невысоком экране.
void HorizonUI::mapBuildFilterPanel(void)
{
    HzMapUi *m = mapUi;
    if (!m)
        return;
    if (m->filterPanel && lv_obj_is_valid(m->filterPanel))
        lv_obj_delete(m->filterPanel);
    m->filterPanel = nullptr;
    for (lv_obj_t *&row : m->filterRows)
        row = nullptr;
    HzMapStore &st = view->getMapStore();
    // Счётчики: по типам + общие/личные (могилы не считаем).
    int cntType[5] = {0, 0, 0, 0, 0};
    int cntShared = 0, cntPriv = 0;
    const std::vector<HzPoi> *pools[3] = {&st.baked(), &st.net(), &st.my()};
    for (int i = 0; i < 3; i++) {
        for (const HzPoi &p : *pools[i]) {
            if (p.tab != m->tab || (i == 0 && st.isTombstoned(p.id)))
                continue;
            if (p.type < HZ_POI_COUNT)
                cntType[p.type]++;
            if (hzPoiIsPrivateOnMap(p))
                cntPriv++;
            else
                cntShared++;
        }
    }

    lv_obj_t *panel = lv_obj_create(scr);
    HorizonTheme::stylePanel(panel);
    lv_obj_set_pos(panel, W() - 204, kStatusH + 38);
    lv_obj_set_size(panel, 200, m->viewH - kStatusH - 42);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(panel, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(panel, 4, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, hzColor(HorizonTheme::current().accent), LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE); // тапы гаснут в панели

    lv_obj_t *t = lv_label_create(panel);
    lv_label_set_text(t, "Фильтры");
    lv_obj_set_style_text_font(t, HorizonTheme::fontMedium(), LV_PART_MAIN);
    lv_obj_set_width(t, lv_pct(100));
    sectionLabel(panel, "По типу");

    for (int i = 0; i < 7; i++) {
        if (i == 5)
            sectionLabel(panel, "По доступу");
        bool on = i < 5 ? m->fType[i] : (i == 5 ? m->fShared : m->fPriv);
        int cnt = i < 5 ? cntType[i] : (i == 5 ? cntShared : cntPriv);
        const char *nm = i < 5 ? kPoiTypes[i] : (i == 5 ? "Общие" : "Личные");
        char cap[64];
        snprintf(cap, sizeof(cap), "%s %s (%d)", on ? "[x]" : "[ ]", nm, cnt);
        lv_obj_t *b = lv_btn_create(panel);
        HorizonTheme::styleButton(b, false);
        lv_obj_set_width(b, lv_pct(100));
        lv_obj_set_height(b, 34);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, cap);
        lv_obj_set_style_text_font(l, HorizonTheme::fontNormal(), LV_PART_MAIN);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 6, 0);
        lv_obj_set_user_data(b, (void *)(intptr_t)i);
        lv_obj_add_event_cb(b, cbMapFilterRow, LV_EVENT_CLICKED, this);
        m->filterRows[i] = b;
    }

    lv_obj_t *rst = lv_btn_create(panel);
    HorizonTheme::styleButton(rst, true);
    lv_obj_set_width(rst, lv_pct(100));
    lv_obj_set_height(rst, 40);
    lv_obj_t *rl = lv_label_create(rst);
    lv_label_set_text(rl, "Сбросить все");
    lv_obj_set_style_text_font(rl, HorizonTheme::fontNormal(), LV_PART_MAIN);
    lv_obj_center(rl);
    lv_obj_add_event_cb(rst, cbMapFilterReset, LV_EVENT_CLICKED, this);

    m->filterPanel = panel;
    m->filterOpen = true;
}

void HorizonUI::cbMapFilter(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    self->mapToggleFilter();
}

void HorizonUI::cbMapFilterRow(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    if (!self->mapUi)
        return;
    int id = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t *)lv_event_get_current_target(e));
    HzMapUi *m = self->mapUi;
    if (id >= 0 && id < 5)
        m->fType[id] = !m->fType[id];
    else if (id == 5)
        m->fShared = !m->fShared;
    else if (id == 6)
        m->fPriv = !m->fPriv;
    else
        return;
    // Не удалять панель вместе с нажатой кнопкой внутри её callback:
    // LVGL всё ещё обрабатывает событие на этом объекте.
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_current_target(e);
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    if (label) {
        bool on = id < 5 ? m->fType[id] : (id == 5 ? m->fShared : m->fPriv);
        int count = 0;
        HzMapStore &st = self->view->getMapStore();
        const std::vector<HzPoi> *pools[3] = {&st.baked(), &st.net(), &st.my()};
        for (int i = 0; i < 3; i++)
            for (const HzPoi &p : *pools[i])
                if (p.tab == m->tab && !(i == 0 && st.isTombstoned(p.id)) &&
                    (id < 5 ? p.type == id : (id == 6) == hzPoiIsPrivateOnMap(p)))
                    count++;
        lv_label_set_text_fmt(label, "%s %s (%d)", on ? "[x]" : "[ ]",
                              id < 5 ? kPoiTypes[id] : (id == 5 ? "Общие" : "Личные"), count);
    }
    self->mapRefreshMarkers();
    self->mapPlaceTag();
}

// Строки формы следуют за списками (дёргается из tick раз в 200 мс).
void HorizonUI::mapSyncFormRows(void)
{
    HzMapUi *m = mapUi;
    if (!m || current != Screen::MAP_FORM)
        return;
    if (!m->formVis || !lv_obj_is_valid(m->formVis))
        return;
    uint8_t vis = (uint8_t)lv_dropdown_get_selected(m->formVis);
    bool wantDest = (vis == 0);
    bool wantPeer = (vis == 2);
    if (wantDest != m->destRowShown && m->formDestRow && lv_obj_is_valid(m->formDestRow)) {
        m->destRowShown = wantDest;
        if (wantDest)
            lv_obj_remove_flag(m->formDestRow, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(m->formDestRow, LV_OBJ_FLAG_HIDDEN);
    }
    if (wantPeer != m->peerRowShown && m->formPeerRow && lv_obj_is_valid(m->formPeerRow)) {
        m->peerRowShown = wantPeer;
        if (wantPeer)
            lv_obj_remove_flag(m->formPeerRow, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(m->formPeerRow, LV_OBJ_FLAG_HIDDEN);
    }
    if (mapPickPeer != m->formPeerShown && m->formPeerLabel && lv_obj_is_valid(m->formPeerLabel)) {
        m->formPeerShown = mapPickPeer;
        char cap[80];
        if (mapPickPeer)
            snprintf(cap, sizeof(cap), "Контакт: %s", view->displayName(mapPickPeer));
        else
            snprintf(cap, sizeof(cap), "Выбрать контакт...");
        lv_label_set_text(m->formPeerLabel, cap);
    }
}

// Строка контакта: сохранить черновик и открыть выбор.
void HorizonUI::cbMapPeerPick(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    HzMapUi *m = self->mapUi;
    if (!m)
        return;
    const char *nm = m->formName ? lv_textarea_get_text(m->formName) : "";
    const char *cm = m->formComment ? lv_textarea_get_text(m->formComment) : "";
    hzUtf8Trunc(m->draftName, sizeof(m->draftName), nm ? nm : "", HzMapStore::kMaxNameChars);
    hzUtf8Trunc(m->draftComment, sizeof(m->draftComment), cm ? cm : "", HzMapStore::kMaxCommentChars);
    m->draftType = m->formType ? (uint8_t)lv_dropdown_get_selected(m->formType) : 0;
    m->draftVis = m->formVis ? (uint8_t)lv_dropdown_get_selected(m->formVis) : 0;
    m->draftDest = mapFormDestination(m->formDest);
    if (m->draftType >= HZ_POI_COUNT)
        m->draftType = 0;
    if (m->draftVis > 2)
        m->draftVis = 0;
    if (m->draftDest > 1)
        m->draftDest = 0;
    m->formDraft = true;
    self->mapPickMode = true;
    self->buildContacts();
}

void HorizonUI::cbMapFilterReset(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    if (!self->mapUi)
        return;
    HzMapUi *m = self->mapUi;
    for (int i = 0; i < 5; i++)
        m->fType[i] = true;
    m->fShared = true;
    m->fPriv = true;
    // Не удалять панель во время обработки нажатия на кнопку сброса.
    if (m->filterPanel && lv_obj_is_valid(m->filterPanel)) {
        for (int i = 0; i < 7; i++) {
            lv_obj_t *btn = m->filterRows[i]; // заголовки разделов не являются строками
            if (!btn || !lv_obj_is_valid(btn))
                continue;
            lv_obj_t *label = lv_obj_get_child(btn, 0);
            if (!label)
                continue;
            const char *old = lv_label_get_text(label);
            const char *count = old ? strrchr(old, '(') : nullptr;
            char cap[96];
            snprintf(cap, sizeof(cap), "[x] %s %s", i < 5 ? kPoiTypes[i] : (i == 5 ? "Общие" : "Личные"),
                     count ? count : "(0)");
            lv_label_set_text(label, cap);
        }
    }
    self->mapRefreshMarkers();
    self->mapPlaceTag();
}

#endif // HORIZON_UI
