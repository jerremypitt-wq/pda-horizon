#include "horizon_target.h"
#ifdef HORIZON_UI
#include "horizon/HorizonBatteryStats.h"
#include <stdio.h>
#if defined(ARCH_PORTDUINO)
#include "PortduinoFS.h"
static fs::FS &batteryFs = PortduinoFS;
#else
#include "LittleFS.h"
static fs::FS &batteryFs = LittleFS;
#endif

namespace {
constexpr const char *kStatsPath = "/horizon.battery";
constexpr const char *kTmpPath = "/horizon.battery.tmp";
}

void HzBatteryStats::load(void)
{
    File f = batteryFs.open(kStatsPath, "r");
    if (!f)
        return;
    char line[160] = {};
    size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
    f.close();
    line[n] = '\0';
    unsigned long cs, ds, ct, dt, soc;
    int lo, hi;
    if (sscanf(line, "HZBAT1 %lu %lu %lu %lu %lu %d %d", &cs, &ds, &ct, &dt, &soc, &lo, &hi) != 7 ||
        lo < 0 || lo > 4500 || hi < lo || hi > 4500)
        return; // повреждённую статистику не загружаем; данных чатов не касается
    chargeStarts = cs;
    dischargeStarts = ds;
    chargeSeconds = ct;
    dischargeSeconds = dt;
    charged256 = soc;
    minMv = lo;
    maxMv = hi;
    dirty = false;
}

bool HzBatteryStats::save(void)
{
    if (!dirty)
        return true;
    File f = batteryFs.open(kTmpPath, "w");
    if (!f)
        return false;
    size_t n = f.printf("HZBAT1 %lu %lu %lu %lu %lu %d %d\n", (unsigned long)chargeStarts,
                        (unsigned long)dischargeStarts, (unsigned long)chargeSeconds,
                        (unsigned long)dischargeSeconds, (unsigned long)charged256, minMv, maxMv);
    f.flush();
    f.close();
    if (n == 0 || !batteryFs.rename(kTmpPath, kStatsPath))
        return false; // старый файл не удаляем при неудаче обновления
    dirty = false;
    return true;
}
#endif // HORIZON_UI
