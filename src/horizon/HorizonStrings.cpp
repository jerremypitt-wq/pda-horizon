#include "horizon_target.h" // целевые флаги сборки (вкус: standalone/прошивка)
// ПДА «Горизонт» - таблицы строк.
#ifdef HORIZON_UI

#include "horizon/HorizonStrings.h"
#include <string.h>

namespace HorizonStrings {

const char *const kRanks[kRankCount] = {"Новичок", "Опытный", "Ветеран", "Мастер"};

const char *const kGroups[kGroupCount] = {"Одиночки",  "Долг",    "Свобода", "Наёмники", "Учёные",
                                          "Бандиты",   "Монолит", "Грех",    "Военные",  "Чистое небо"};

const char *const kGroupTags[kGroupCount] = {"ОДН", "ДОЛГ", "СВБ", "НАЁМ", "УЧН",
                                             "БНД", "МНЛ",  "ГРЕХ", "ВС",   "ЧН"};

const char *const kThemes[kThemeCount] = {"Тёмная (Сталкер)", "Светлая", "Долг", "Свобода", "Одиночки (хаки)"};

// Коды должны совпадать с meshtastic_Config_LoRaConfig_RegionCode
// UNSET=0 US=1 EU_433=2 EU_868=3 CN=4 JP=5 ANZ=6 KR=7 TW=8 RU=9 IN=10 NZ_865=11 ...
const RegionEntry kRegions[] = {
    {"Россия (RU)", 9}, {"Европа 868 (EU)", 3}, {"Европа 433", 2}, {"Украина 868", 15},
    {"Украина 433", 14}, {"США (US)", 1}, {"Казахстан 433", 23}, {"Казахстан 863", 24},
};
const uint8_t kRegionCount = sizeof(kRegions) / sizeof(kRegions[0]);

// Коды должны совпадать с meshtastic_Config_LoRaConfig_ModemPreset
const PresetEntry kPresets[] = {
    {"LongFast (стандарт)", 0},
    {"LongSlow (дальний)", 1},
    {"VeryLongSlow", 2},
    {"MedSlow", 3},
    {"MedFast", 4},
    {"ShortSlow", 5},
    {"ShortFast", 6},
    {"LongModerate", 7},
};
const uint8_t kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);

// Коды должны совпадать с meshtastic_Config_DeviceConfig_Role
const RoleEntry kRoles[] = {
    {"Клиент", 0}, {"Клиент (мут)", 1}, {"Роутер", 2}, {"Роутер+клиент", 3}, {"Ретранслятор", 4}, {"Трекер", 5},
};
const uint8_t kRoleCount = sizeof(kRoles) / sizeof(kRoles[0]);

const TimeoutEntry kTimeouts[] = {
    {"15 секунд", 15}, {"30 секунд", 30}, {"1 минута", 60}, {"5 минут", 300}, {"Никогда", 0},
};
const uint8_t kTimeoutCount = sizeof(kTimeouts) / sizeof(kTimeouts[0]);

const char *const kSimpleKeys[kSimpleKeyCount] = {
    "Без ключа", "Default", "Simple1", "Simple2", "Simple3", "Simple4",
    "Simple5",   "Simple6", "Simple7", "Simple8", "Simple9", "Simple10",
};

void copyUtf8(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0)
        return;
    if (!src)
        src = "";
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
    if (strlen(src) < cap)
        return; // влезло целиком - символ цел, трогать нечего
    // Резанули по живому: проверить, цел ли последний символ.
    size_t n = strlen(dst);
    if (n == 0)
        return;
    size_t start = n;
    while (start > 0 && n - start < 4 && ((unsigned char)dst[start - 1] & 0xC0) == 0x80)
        start--;
    // Отступить на ведущий байт символа (если он есть): после цикла
    // start указывает ПОСЛЕ пропущенных продолжений, а не на начало символа.
    if (start > 0 && ((unsigned char)dst[start - 1] & 0xC0) == 0xC0)
        start--;
    if (start >= n)
        return; // последний байт ASCII - всё цело
    unsigned char lead = (unsigned char)dst[start];
    size_t need = 1;
    if ((lead & 0xE0) == 0xC0)
        need = 2;
    else if ((lead & 0xF0) == 0xE0)
        need = 3;
    else if ((lead & 0xF8) == 0xF0)
        need = 4;
    else if (lead & 0x80)
        need = 5; // битый ведущий байт - убрать
    if (n - start < need)
        dst[start] = '\0'; // символ резан - убрать хвост
}

} // namespace HorizonStrings

#endif // HORIZON_UI
