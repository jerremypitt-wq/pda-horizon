#include "horizon_target.h" // целевые флаги сборки (вкус: standalone/прошивка)
// ПДА «Горизонт» - загрузка/сохранение настроек.
#ifdef HORIZON_UI

#include "horizon/HorizonConfig.h"
#include "horizon/HorizonStrings.h"
#include "util/ILog.h"
#include <stdio.h>
#include <stdlib.h> // atoi
#include <string.h>

#if defined(ARCH_PORTDUINO)
#include "PortduinoFS.h"
static fs::FS &cfgFs = PortduinoFS;
#else
#include "LittleFS.h"
static fs::FS &cfgFs = LittleFS;
#endif

namespace {
constexpr const char *kPath = "/horizon.cfg";
constexpr const char *kTimePath = "/horizon.time";

void copyField(char *dst, size_t cap, const char *src)
{
    if (!src || !*src)
        return; // пустое значение - оставляем текущее
    HorizonStrings::copyUtf8(dst, cap, src);
    // убрать \r \n на конце
    size_t n = strlen(dst);
    while (n > 0 && (dst[n - 1] == '\r' || dst[n - 1] == '\n'))
        dst[--n] = '\0';
}

int parseInt(const char *s, int fallback, int lo, int hi)
{
    if (!s || !*s)
        return fallback;
    int v = atoi(s);
    if (v < lo)
        v = lo;
    if (v > hi)
        v = hi;
    return v;
}

// Обрезать UTF-8 строку до maxBytes байт, не разрывая символ.
std::string utf8Truncate(const std::string &s, size_t maxBytes)
{
    if (s.size() <= maxBytes)
        return s;
    size_t n = maxBytes;
    while (n > 0 && (s[n] & 0xC0) == 0x80)
        n--;
    return s.substr(0, n);
}
} // namespace

HorizonConfig &HorizonConfig::instance(void)
{
    static HorizonConfig cfg;
    return cfg;
}

void HorizonConfig::resetToDefaults(void)
{
    generalCh = 0;
    groupCh = 1;
    announceCh = 2;
    strcpy(masterPin, "0000");
    brightness = 180;
    timeoutSec = 60;
    orientation = 0;
    theme = 0;
    silent = false;
    soundSet = 0;
    strcpy(charName, "Сталкер");
    strcpy(callsign, "Новичок");
    rank = 0;
    group = 0;
}

void HorizonConfig::load(void)
{
    resetToDefaults();
    File f = cfgFs.open(kPath, "r");
    if (!f) {
        ILOG_INFO("Horizon: no config file, using defaults");
        return;
    }
    char line[96];
    while (f.available()) {
        size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
        line[n] = '\0';
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;
        if (!strcmp(key, "generalCh"))
            generalCh = (uint8_t)parseInt(val, 0, 0, 7);
        else if (!strcmp(key, "groupCh"))
            groupCh = (uint8_t)parseInt(val, 1, 0, 7);
        else if (!strcmp(key, "announceCh"))
            announceCh = (uint8_t)parseInt(val, 2, 0, 7);
        else if (!strcmp(key, "masterPin"))
            copyField(masterPin, sizeof(masterPin), val);
        else if (!strcmp(key, "brightness"))
            brightness = (uint8_t)parseInt(val, 180, 20, 255);
        else if (!strcmp(key, "timeoutSec"))
            timeoutSec = (uint16_t)parseInt(val, 60, 0, 3600);
        else if (!strcmp(key, "orientation"))
            orientation = (uint8_t)parseInt(val, 0, 0, 1);
        else if (!strcmp(key, "theme"))
            theme = (uint8_t)parseInt(val, 0, 0, (int)HorizonStrings::kThemeCount - 1);
        else if (!strcmp(key, "silent"))
            silent = parseInt(val, 0, 0, 1) != 0;
        else if (!strcmp(key, "soundSet"))
            soundSet = (uint8_t)parseInt(val, 0, 0, 2);
        else if (!strcmp(key, "charName"))
            copyField(charName, sizeof(charName), val);
        else if (!strcmp(key, "callsign"))
            copyField(callsign, sizeof(callsign), val);
        else if (!strcmp(key, "rank"))
            rank = (uint8_t)parseInt(val, 0, 0, (int)HorizonStrings::kRankCount - 1);
        else if (!strcmp(key, "group"))
            group = (uint8_t)parseInt(val, 0, 0, (int)HorizonStrings::kGroupCount - 1);
    }
    f.close();
    ILOG_INFO("Horizon: config loaded (ch %u/%u/%u, theme %u, orient %u)", generalCh, groupCh, announceCh, theme,
              orientation);
}

void HorizonConfig::save(void) const
{
    File f = cfgFs.open(kPath, "w");
    if (!f) {
        ILOG_ERROR("Horizon: cannot write config file");
        return;
    }
    f.printf("generalCh=%u\n", generalCh);
    f.printf("groupCh=%u\n", groupCh);
    f.printf("announceCh=%u\n", announceCh);
    f.printf("masterPin=%s\n", masterPin);
    f.printf("brightness=%u\n", brightness);
    f.printf("timeoutSec=%u\n", timeoutSec);
    f.printf("orientation=%u\n", orientation);
    f.printf("theme=%u\n", theme);
    f.printf("silent=%u\n", silent ? 1 : 0);
    f.printf("soundSet=%u\n", soundSet);
    f.printf("charName=%s\n", charName);
    f.printf("callsign=%s\n", callsign);
    f.printf("rank=%u\n", rank);
    f.printf("group=%u\n", group);
    f.close();
    ILOG_INFO("Horizon: config saved");
}

void HorizonConfig::loadTime(void)
{
    savedEpoch = 0;
    savedMs = 0;
    File f = cfgFs.open(kTimePath, "r");
    if (!f)
        return;
    char line[48];
    size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
    f.close();
    line[n] = '\0';
    unsigned long ep = 0, ms = 0;
    if (sscanf(line, "%lu %lu", &ep, &ms) == 2 && ep > 1000000) {
        savedEpoch = (uint32_t)ep;
        savedMs = (uint32_t)ms;
    }
}

void HorizonConfig::saveTime(uint32_t epoch, uint32_t ms) const
{
    if (epoch < 1000000)
        return; // мусор не персистим
    File f = cfgFs.open(kTimePath, "w");
    if (!f) {
        ILOG_ERROR("Horizon: cannot write time file");
        return;
    }
    f.printf("%lu %lu\n", (unsigned long)epoch, (unsigned long)ms);
    f.close();
}

bool HorizonConfig::checkPin(const char *pin) const
{
    return pin && !strcmp(pin, masterPin);
}

void HorizonConfig::setPin(const char *pin)
{
    copyField(masterPin, sizeof(masterPin), pin && *pin ? pin : "0000");
}

std::string HorizonConfig::meshLongName(void) const
{
    // Формат: "Позывной [ТЕГ]", лимит - 24 байта UTF-8 (прошивка обрежет лишнее).
    std::string tag = "[";
    tag += HorizonStrings::kGroupTags[group < HorizonStrings::kGroupCount ? group : 0];
    tag += "]";
    std::string cs = callsign;
    // Оставляем место под пробел и тег
    size_t room = 24 > tag.size() + 1 ? 24 - tag.size() - 1 : 8;
    cs = utf8Truncate(cs, room);
    if (cs.empty())
        cs = "Сталкер";
    return cs + " " + tag;
}

std::string HorizonConfig::meshShortName(void) const
{
    std::string cs = callsign;
    std::string s = utf8Truncate(cs, 4);
    if (s.empty())
        s = "ST";
    return s;
}

#endif // HORIZON_UI
