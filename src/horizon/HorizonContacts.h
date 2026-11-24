#pragma once
#include <cstddef>
#include <cstdint>
#include <map>

// Отдельный от радиобазы справочник контактов: отсутствие NodeInfo, молчание
// узла и очистка базы Meshtastic не стирают контакт или переписку.
struct HzContact {
    uint32_t id = 0;
    uint32_t lastEpoch = 0; // UTC последнего приёма, 0 = достоверного времени тогда не было
    uint32_t lastMs = 0;    // millis() последнего приёма только в этой загрузке
    uint8_t channel = 0;
    bool heardThisBoot = false;
    char name[40] = {};
};

class HzContacts {
  public:
    static constexpr uint32_t kMinEpoch = 1704067200u; // 01.01.2024
    static constexpr uint32_t kOnlineSec = 30u * 60u; // > интервала маяка 15 мин
    static constexpr uint32_t kKeepSec = 48u * 60u * 60u;
    static constexpr size_t kMaxContacts = 250;

    const std::map<uint32_t, HzContact> &records() const { return entries; }
    const HzContact *find(uint32_t id) const;
    // Исторические сведения о NodeInfo: не делают узел «в сети».
    bool remember(uint32_t id, uint8_t channel, const char *name, uint32_t lastEpoch, uint32_t trustedNow);
    // Только реально принятый пакет обновляет присутствие.
    bool heard(uint32_t id, uint8_t channel, const char *name, uint32_t nowMs, uint32_t trustedNow);
    bool online(uint32_t id, uint32_t nowMs, uint32_t trustedNow) const;
    // false означает неизвестную давность, а не «нулевую».
    bool age(uint32_t id, uint32_t nowMs, uint32_t trustedNow, uint32_t &seconds) const;
    // Удалять можно лишь при достоверных часах; избранное бессрочно.
    bool expire(uint32_t nowMs, uint32_t trustedNow, bool (*isFavorite)(uint32_t, void *), void *ctx);
    void load();
    bool save();
    bool changed() const { return dirty; }
    bool urgent() const { return needsSave; }

  private:
    std::map<uint32_t, HzContact> entries;
    bool dirty = false;
    bool needsSave = false;
    static bool validEpoch(uint32_t value) { return value >= kMinEpoch && value < 2082758400u; }
    static void copyName(char (&dst)[40], const char *src);
};
