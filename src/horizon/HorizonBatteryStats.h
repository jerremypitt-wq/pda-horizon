#pragma once
#include "horizon/HorizonBattery.h"
#include <stdint.h>

// Наблюдения ПДА, а НЕ счётчик циклов/ёмкости самой MAX17048.
// Во время выключения и пропусков I2C ничего не досчитываем.
struct HzBatteryStats {
    uint32_t chargeStarts = 0;
    uint32_t dischargeStarts = 0;
    uint32_t chargeSeconds = 0;
    uint32_t dischargeSeconds = 0;
    uint32_t charged256 = 0; // набранный SOC в единицах 1/256% (условные циклы /25600)
    int minMv = 0;
    int maxMv = 0;
    bool dirty = false;

    void load(void);
    bool save(void);

    // true при новой наблюдаемой фазе; остальные изменения сохраняем периодически.
    bool observe(const HzBatteryState &b, uint32_t nowMs)
    {
        if (!b.found) {
            phase = 0;
            lastValid = false;
            return false;
        }
        bool changed = false;
        if (!minMv || b.battMv < minMv) { minMv = b.battMv; changed = true; }
        if (b.battMv > maxMv) { maxMv = b.battMv; changed = true; }
        if (!b.rateValid) {
            phase = 0;
            lastValid = false;
            dirty |= changed;
            return false;
        }
        int next = b.rateRaw >= 4 ? 1 : b.rateRaw <= -4 ? -1 : 0;
        bool transition = next != 0 && phase != next;
        if (transition) {
            if (next == 1 && chargeStarts != UINT32_MAX) ++chargeStarts;
            if (next == -1 && dischargeStarts != UINT32_MAX) ++dischargeStarts;
            changed = true;
        }
        // Засчитываем только два последовательных валидных замера одной фазы,
        // без пробела >2 мин и без скачка SOC >5% за 1 опрос.
        if (lastValid && phase == next && next != 0 &&
            nowMs - lastMs <= 120000 && nowMs - lastMs >= 1000) {
            uint32_t secs = (nowMs - lastMs) / 1000;
            uint32_t &total = next == 1 ? chargeSeconds : dischargeSeconds;
            total = secs > UINT32_MAX - total ? UINT32_MAX : total + secs;
            if (next == 1 && b.socRaw >= lastSoc && b.socRaw - lastSoc <= 5 * 256) {
                uint32_t delta = b.socRaw - lastSoc;
                charged256 = delta > UINT32_MAX - charged256 ? UINT32_MAX : charged256 + delta;
            }
            changed = true;
        }
        lastMs = nowMs;
        lastSoc = b.socRaw;
        lastValid = true;
        phase = next;
        dirty |= changed;
        return transition;
    }

  private:
    uint32_t lastMs = 0;
    uint16_t lastSoc = 0;
    int phase = 0;
    bool lastValid = false;
};
