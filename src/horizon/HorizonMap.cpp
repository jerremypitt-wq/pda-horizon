#include "horizon_target.h" // целевые флаги сборки (вкус: standalone/прошивка)
// ПДА «Горизонт» - точки карты: файлы /hzmap, пулы, протокол (см. docs/MAP.md).
#ifdef HORIZON_UI

#include "horizon/HorizonMap.h"
#include "util/ILog.h"
#include <stdio.h>
#include <string.h>

#if defined(ARCH_PORTDUINO)
#include "PortduinoFS.h"
static fs::FS &mapFs = PortduinoFS;
#else
#include "LittleFS.h"
static fs::FS &mapFs = LittleFS;
#endif

namespace {
constexpr const char *kDir = "/hzmap";
constexpr const char *kBakedPath = "/hzmap/pois.csv";
constexpr const char *kMyPath = "/hzmap/my.csv";
constexpr const char *kNetPath = "/hzmap/net.csv";
constexpr const char *kDelPath = "/hzmap/del.csv";
constexpr const char *kDirectDelPath = "/hzmap/dm_del.csv";

// Разбить строку по '|' (in place). Возврат: число полей, 0 = битая
// (разделителей больше, чем влезает в maxF).
size_t splitFields(char *line, char **f, size_t maxF)
{
    size_t n = 1;
    f[0] = line;
    for (char *p = line; *p && n < maxF; p++) {
        if (*p == '|') {
            *p = '\0';
            f[n++] = p + 1;
        }
    }
    if (n == maxF && strchr(f[n - 1], '|'))
        return 0;
    return n;
}

// Строгое число: только цифры, целиком (пустое и с хвостом - брак).
bool parseU(const char *s, uint32_t &out)
{
    if (!s || !*s)
        return false;
    char *e = nullptr;
    unsigned long v = strtoul(s, &e, 10);
    if (!e || e == s || *e)
        return false;
    out = (uint32_t)v;
    return true;
}

// mode: 0 - запечённые (7 полей), 1 - свои (11: +shared|ch|peer|sourceId;
// старые 9 и 10 полей читаются), 2 - принятые (8: +ch).
bool loadCsv(const char *path, std::vector<HzPoi> &out, int mode)
{
    File f = mapFs.open(path, "r");
    if (!f)
        return false;
    char line[1024]; // своя строка: коммент 600 + имя/позывной + поля
    size_t want = mode == 1 ? 11 : (mode == 2 ? 8 : 7);
    size_t bad = 0;
    while (f.available()) {
        size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
        line[n] = '\0';
        while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == '\n'))
            line[--n] = '\0';
        if (!line[0])
            continue;
        char *fld[11] = {nullptr};
        size_t nf = splitFields(line, fld, want);
        // Свои: 9/10 полей из прежних прошивок либо 11 (+sourceId).
        if (mode == 1 ? (nf != 9 && nf != 10 && nf != 11) : nf != want) {
            bad++;
            continue;
        }
        uint32_t type = 0, x = 0, y = 0, shared = 1, ch = 0, peer = 0;
        if (!hzPoiIdOk(fld[0]) || !parseU(fld[1], type) || type > 4 || !parseU(fld[3], x) || x > 1000 ||
            !parseU(fld[4], y) || y > 1000 || strlen(fld[2]) == 0 || strlen(fld[2]) > 128 ||
            strlen(fld[5]) > 96 || strlen(fld[6]) > 768) {
            bad++;
            continue;
        }
        if (mode == 1 && (!parseU(fld[7], shared) || shared > 1 || !parseU(fld[8], ch) || ch > 7 ||
                          (nf >= 10 && !parseU(fld[9], peer)) ||
                          (nf == 11 && *fld[10] && (!hzPoiIdOk(fld[10]) || !strcmp(fld[10], fld[0]))))) {
            bad++;
            continue;
        }
        if (mode == 2 && (!parseU(fld[7], ch) || ch > 7)) {
            bad++;
            continue;
        }
        HzPoi p;
        strcpy(p.id, fld[0]); // hzPoiIdOk гарантирует ≤27
        p.type = (uint8_t)type;
        p.name = fld[2];
        p.x = (uint16_t)x;
        p.y = (uint16_t)y;
        p.sender = fld[5];
        p.comment = fld[6];
        p.shared = mode == 1 ? (shared != 0) : true;
        p.ch = mode == 0 ? 0xFF : (uint8_t)ch; // у запечённых канала нет (удаление - в общий)
        p.dmPeer = mode == 1 ? peer : 0;
        if (mode == 1 && nf == 11 && *fld[10])
            strcpy(p.sourceId, fld[10]); // проверено выше
        out.push_back(p);
    }
    f.close();
    if (bad)
        ILOG_INFO("Horizon: map %s: %u bad line(s) skipped", path, (unsigned)bad);
    return true;
}

void saveCsv(const char *path, const std::vector<HzPoi> &v, int mode)
{
    File f = mapFs.open(path, "w");
    if (!f) {
        ILOG_ERROR("Horizon: cannot write %s", path);
        return;
    }
    for (const HzPoi &p : v) {
        // Поля уже без '|' и переводов строк (санитизация на входе) - пишем как есть.
        f.printf("%s|%u|%s|%u|%u|%s|%s", p.id, p.type, p.name.c_str(), p.x, p.y, p.sender.c_str(),
                 p.comment.c_str());
        if (mode == 1)
            f.printf("|%u|%u|%u|%s", p.shared ? 1 : 0, p.ch, (unsigned)p.dmPeer, p.sourceId);
        else if (mode == 2)
            f.printf("|%u", p.ch);
        f.printf("\n");
    }
    f.close();
}
} // namespace

void HzMapStore::loadAll(void)
{
    baked_.clear();
    net_.clear();
    my_.clear();
    del_.clear();
    directDismissed_.clear();
    mapFs.mkdir(kDir); // buildfs и так создаёт; страховка после вайпа
    loadCsv(kBakedPath, baked_, 0);
    loadCsv(kMyPath, my_, 1);
    loadCsv(kNetPath, net_, 2);
    File f = mapFs.open(kDelPath, "r");
    if (f) {
        char line[48];
        while (f.available()) {
            size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
            line[n] = '\0';
            while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == '\n'))
                line[--n] = '\0';
            if (hzPoiIdOk(line))
                del_.push_back(line);
        }
        f.close();
    }
    File dismissed = mapFs.open(kDirectDelPath, "r");
    if (dismissed) {
        char line[48];
        while (dismissed.available()) {
            size_t n = dismissed.readBytesUntil('\n', line, sizeof(line) - 1);
            line[n] = '\0';
            while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == '\n'))
                line[--n] = '\0';
            if (hzPoiIdOk(line))
                directDismissed_.push_back(line);
        }
        dismissed.close();
    }
    loaded = true;
    ILOG_INFO("Horizon: map loaded (baked %u, net %u, my %u, del %u)", (unsigned)baked_.size(),
              (unsigned)net_.size(), (unsigned)my_.size(), (unsigned)del_.size());
}

bool HzMapStore::resetToBaked(void)
{
    // Без исходного набора данных нельзя обещать восстановление запечённых точек.
    File baked = mapFs.open(kBakedPath, "r");
    if (!baked) {
        ILOG_ERROR("Horizon: map reset refused: missing %s", kBakedPath);
        return false;
    }
    baked.close();
    // Не трогаем карту, исходные точки, чаты и настройки. Пустые файлы удобнее
    // удалённых: следующий запуск читает ровно то же состояние, что сейчас.
    const char *const paths[] = {kMyPath, kNetPath, kDelPath, kDirectDelPath};
    for (const char *path : paths) {
        File f = mapFs.open(path, "w");
        if (!f) {
            ILOG_ERROR("Horizon: map reset failed: %s", path);
            loadAll(); // часть файлов могла очиститься: синхронизировать память с диском
            return false;
        }
        f.close();
    }
    loadAll(); // сбросить пулы/могилы и перечитать неизменённый pois.csv
    return true;
}

void HzMapStore::saveMy(void)
{
    saveCsv(kMyPath, my_, 1);
}

void HzMapStore::saveNet(void)
{
    saveCsv(kNetPath, net_, 2);
}

void HzMapStore::saveDel(void)
{
    File f = mapFs.open(kDelPath, "w");
    if (!f) {
        ILOG_ERROR("Horizon: cannot write %s", kDelPath);
        return;
    }
    for (const std::string &id : del_)
        f.printf("%s\n", id.c_str());
    f.close();
}

void HzMapStore::saveDirectDismissed(void)
{
    File f = mapFs.open(kDirectDelPath, "w");
    if (!f)
        return;
    for (const std::string &id : directDismissed_)
        f.printf("%s\n", id.c_str());
    f.close();
}

const HzPoi *HzMapStore::findBySource(const char *sourceId) const
{
    if (!sourceId || !*sourceId)
        return nullptr;
    for (const HzPoi &p : my_) {
        if (!strcmp(p.sourceId, sourceId))
            return &p;
    }
    return nullptr;
}

bool HzMapStore::directDismissed(const char *sourceId) const
{
    if (!sourceId || !*sourceId)
        return false;
    for (const std::string &id : directDismissed_) {
        if (id == sourceId)
            return true;
    }
    return false;
}

size_t HzMapStore::sharedCount(void) const
{
    size_t n = net_.size();
    for (const HzPoi &b : baked_) {
        if (!isTombstoned(b.id))
            n++;
    }
    for (const HzPoi &m : my_) {
        if (m.shared)
            n++;
    }
    return n;
}

size_t HzMapStore::privCount(void) const
{
    size_t n = 0;
    for (const HzPoi &m : my_) {
        if (!m.shared)
            n++;
    }
    return n;
}

const HzPoi *HzMapStore::find(const char *id) const
{
    if (!id || !*id)
        return nullptr;
    for (const HzPoi &m : my_) {
        if (!strcmp(m.id, id))
            return &m;
    }
    for (const HzPoi &e : net_) {
        if (!strcmp(e.id, id))
            return &e;
    }
    for (const HzPoi &b : baked_) {
        if (!strcmp(b.id, id))
            return isTombstoned(id) ? nullptr : &b;
    }
    return nullptr;
}

HzPoi *HzMapStore::findMut(const char *id)
{
    if (!id || !*id)
        return nullptr;
    for (HzPoi &m : my_) {
        if (!strcmp(m.id, id))
            return &m;
    }
    return nullptr;
}

bool HzMapStore::isMine(const char *id) const
{
    if (!id || !*id)
        return false;
    for (const HzPoi &m : my_) {
        if (!strcmp(m.id, id))
            return true;
    }
    return false;
}

bool HzMapStore::isTombstoned(const char *id) const
{
    if (!id)
        return false;
    for (const std::string &d : del_) {
        if (d == id)
            return true;
    }
    return false;
}

bool HzMapStore::upsertNet(const HzPoi &p)
{
    for (HzPoi &e : net_) {
        if (!strcmp(e.id, p.id)) {
            e = p;
            return false;
        }
    }
    net_.push_back(p);
    return true;
}

bool HzMapStore::addMy(const HzPoi &p)
{
    if (p.shared) {
        if (sharedCount() >= kMaxShared)
            return false;
    } else if (privCount() >= kMaxPriv) {
        return false;
    }
    my_.push_back(p);
    return true;
}

bool HzMapStore::removeEverywhere(const char *id)
{
    if (!id || !*id)
        return false;
    bool gone = false;
    for (auto it = my_.begin(); it != my_.end(); ++it) {
        if (!strcmp(it->id, id)) {
            if (it->sourceId[0] && !directDismissed(it->sourceId)) {
                directDismissed_.push_back(it->sourceId);
                saveDirectDismissed();
            }
            my_.erase(it);
            saveMy();
            gone = true;
            break;
        }
    }
    for (auto it = net_.begin(); it != net_.end(); ++it) {
        if (!strcmp(it->id, id)) {
            net_.erase(it);
            saveNet();
            gone = true;
            break;
        }
    }
    for (const HzPoi &b : baked_) {
        if (!strcmp(b.id, id) && !isTombstoned(id)) {
            del_.push_back(id);
            saveDel();
            gone = true;
            break;
        }
    }
    return gone;
}

// ---------------- протокол ----------------

bool hzPoiLinkId(const char *text, char out[28])
{
    if (!text || !out || strncmp(text, kHzPoiLinkPrefix, strlen(kHzPoiLinkPrefix)))
        return false;
    const char *start = text + strlen(kHzPoiLinkPrefix);
    const char *end = strchr(start, '|');
    if (!end || end == start || (size_t)(end - start) >= 28 || !end[1] || strchr(end + 1, '|') ||
        strchr(end + 1, '\n') || strchr(end + 1, '\r'))
        return false;
    memcpy(out, start, (size_t)(end - start));
    out[end - start] = '\0';
    return hzPoiIdOk(out) && hzUtf8Len(end + 1) <= HzMapStore::kMaxNameChars;
}

size_t hzUtf8Len(const char *s)
{
    if (!s)
        return 0;
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if ((*p & 0xC0) != 0x80)
            n++;
    }
    return n;
}

void hzUtf8Trunc(char *dst, size_t dstBytes, const char *src, size_t maxChars)
{
    if (!dst || dstBytes == 0)
        return;
    if (!src)
        src = "";
    size_t chars = 0;
    size_t i = 0;
    while (src[i] && chars < maxChars) {
        unsigned char c = (unsigned char)src[i];
        size_t len = 1;
        if ((c & 0xE0) == 0xC0)
            len = 2;
        else if ((c & 0xF0) == 0xE0)
            len = 3;
        else if ((c & 0xF8) == 0xF0)
            len = 4;
        if (i + len >= dstBytes)
            break; // целиком не влезет - стоп
        size_t k = 0;
        while (k < len && src[i + k])
            k++;
        if (k < len)
            break; // хвост обрезан - стоп
        memcpy(dst + i, src + i, len);
        i += len;
        chars++;
    }
    dst[i] = '\0';
}

size_t hzChunkLen(const char *s, size_t maxBytes)
{
    size_t n = strlen(s);
    if (n <= maxBytes)
        return n;
    n = maxBytes;
    while (n > 0 && (s[n] & 0xC0) == 0x80)
        n--;
    return n;
}

void hzSanitizeField(char *s)
{
    if (!s)
        return;
    for (; *s; s++) {
        if (*s == '|' || *s == '\n' || *s == '\r')
            *s = ' ';
    }
}

uint32_t hzPoiAuthorNode(const char *id)
{
    if (!id)
        return 0;
    const char *dash = strchr(id, '-');
    if (!dash || dash == id)
        return 0;
    char *e = nullptr;
    unsigned long v = strtoul(id, &e, 10);
    if (e != dash)
        return 0;
    return (uint32_t)v;
}

bool hzPoiIdOk(const char *id)
{
    if (!id || !*id)
        return false;
    size_t n = 0;
    for (const char *p = id; *p; p++, n++) {
        char c = *p;
        bool ok =
            (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-';
        if (!ok || n >= 27)
            return false;
    }
    return true;
}

#endif // HORIZON_UI
