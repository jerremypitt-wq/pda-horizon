#include "horizon_target.h"
#ifdef HORIZON_UI
#include "horizon/HorizonContacts.h"
#if defined(ARCH_PORTDUINO)
#include "PortduinoFS.h"
static fs::FS &contactsFs = PortduinoFS;
#else
#include "LittleFS.h"
static fs::FS &contactsFs = LittleFS;
#endif
#include <cstdio>
#include <cstring>

namespace {
constexpr const char *kPath = "/horizon.contacts";
constexpr const char *kTmp = "/horizon.contacts.tmp";
}

const HzContact *HzContacts::find(uint32_t id) const
{
    auto it = entries.find(id);
    return it == entries.end() ? nullptr : &it->second;
}

void HzContacts::copyName(char (&dst)[40], const char *src)
{
    if (!src) return;
    size_t i = 0;
    for (; i < sizeof(dst) - 1 && src[i]; ++i)
        dst[i] = (src[i] == '\n' || src[i] == '\r') ? ' ' : src[i];
    dst[i] = '\0';
}

bool HzContacts::remember(uint32_t id, uint8_t channel, const char *name, uint32_t lastEpoch, uint32_t trustedNow)
{
    if (!id || id == 0xFFFFFFFFu) return false;
    auto it = entries.find(id);
    if (it == entries.end() && entries.size() >= kMaxContacts) return false;
    // Данные о старых узлах из базового NodeInfo не должны возвращать в
    // контакты записи, срок которых уже вышел. Избранные добавляются отдельно.
    if (it == entries.end() && validEpoch(lastEpoch) && validEpoch(trustedNow) &&
        (lastEpoch > trustedNow || trustedNow - lastEpoch > kKeepSec)) return false;
    auto result = entries.try_emplace(id);
    HzContact &c = result.first->second;
    bool changed = result.second;
    c.id = id;
    if (channel < 8 && c.channel != channel) { c.channel = channel; changed = true; }
    if (name && *name && strcmp(c.name, name) != 0) { copyName(c.name, name); changed = true; }
    if (validEpoch(lastEpoch) && (!validEpoch(c.lastEpoch) || lastEpoch > c.lastEpoch) &&
        (!validEpoch(trustedNow) || lastEpoch <= trustedNow)) {
        c.lastEpoch = lastEpoch;
        changed = true;
    }
    if (changed) { dirty = true; needsSave = true; }
    return result.second;
}

bool HzContacts::heard(uint32_t id, uint8_t channel, const char *name, uint32_t nowMs, uint32_t trustedNow)
{
    bool inserted = remember(id, channel, name, 0, 0);
    auto it = entries.find(id);
    if (it == entries.end()) return false;
    HzContact &c = it->second;
    c.lastMs = nowMs;
    c.heardThisBoot = true;
    if (validEpoch(trustedNow)) {
        // ОЗУ обновляем на каждый пакет; запись во flash ограничена таймером View.
        if (c.lastEpoch != trustedNow) dirty = true;
        c.lastEpoch = trustedNow;
    }
    return inserted;
}

bool HzContacts::age(uint32_t id, uint32_t nowMs, uint32_t trustedNow, uint32_t &seconds) const
{
    const HzContact *c = find(id);
    if (!c) return false;
    // millis не зависит от перестановки игровых/системных часов.
    if (c->heardThisBoot) {
        seconds = (nowMs - c->lastMs) / 1000u;
        return true;
    }
    if (validEpoch(trustedNow) && validEpoch(c->lastEpoch) && c->lastEpoch <= trustedNow) {
        seconds = trustedNow - c->lastEpoch;
        return true;
    }
    return false;
}

bool HzContacts::online(uint32_t id, uint32_t nowMs, uint32_t trustedNow) const
{
    uint32_t seconds;
    return age(id, nowMs, trustedNow, seconds) && seconds <= kOnlineSec;
}

bool HzContacts::expire(uint32_t nowMs, uint32_t trustedNow, bool (*isFavorite)(uint32_t, void *), void *ctx)
{
    if (!validEpoch(trustedNow)) return false;
    bool removed = false;
    for (auto it = entries.begin(); it != entries.end();) {
        HzContact &c = it->second;
        // Если часы уточнили уже после пакета, дата приёма могла остаться
        // старой с прошлой загрузки. Восстанавливаем её из millis текущей
        // загрузки до удаления и до записи на диск.
        if (c.heardThisBoot) {
            uint32_t elapsed = (nowMs - c.lastMs) / 1000u;
            if (elapsed <= trustedNow - kMinEpoch) {
                uint32_t inferred = trustedNow - elapsed;
                if (!validEpoch(c.lastEpoch) || c.lastEpoch > trustedNow ||
                    (inferred > c.lastEpoch && inferred - c.lastEpoch > kOnlineSec)) {
                    c.lastEpoch = inferred;
                    dirty = needsSave = true;
                }
            }
        }
        uint32_t seconds;
        bool overdue = age(c.id, nowMs, trustedNow, seconds) && seconds > kKeepSec;
        if (overdue && !(isFavorite && isFavorite(c.id, ctx))) {
            it = entries.erase(it);
            removed = dirty = needsSave = true;
        } else ++it;
    }
    return removed;
}

void HzContacts::load()
{
    File f = contactsFs.open(kPath, "r");
    if (!f) return;
    char line[128];
    while (f.available() && entries.size() < kMaxContacts) {
        size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
        line[n] = '\0';
        unsigned long id = 0, epoch = 0, channel = 0;
        char name[40] = {};
        if (sscanf(line, "HZC1 %lx %lu %lu %39[^\n]", &id, &epoch, &channel, name) != 4 ||
            !id || id > UINT32_MAX || channel > 7 || (epoch && !validEpoch((uint32_t)epoch))) continue;
        HzContact c;
        c.id = (uint32_t)id;
        c.channel = (uint8_t)channel;
        c.lastEpoch = (uint32_t)epoch;
        if (strcmp(name, "-") != 0) copyName(c.name, name);
        entries.emplace(c.id, c);
    }
    f.close();
    dirty = needsSave = false;
}

bool HzContacts::save()
{
    if (!dirty) return true;
    File f = contactsFs.open(kTmp, "w");
    if (!f) return false;
    for (const auto &pair : entries) {
        const HzContact &c = pair.second;
        if (!f.printf("HZC1 %08lx %lu %u %s\n", (unsigned long)c.id, (unsigned long)c.lastEpoch,
                      (unsigned)c.channel, c.name[0] ? c.name : "-")) { f.close(); return false; }
    }
    f.flush();
    f.close();
    if (!contactsFs.rename(kTmp, kPath)) return false;
    dirty = needsSave = false;
    return true;
}
#endif
