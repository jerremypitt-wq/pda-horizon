// ПДА «Горизонт» - таблицы строк.
#ifdef HORIZON_UI

#include "horizon/HorizonStrings.h"

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

} // namespace HorizonStrings

#endif // HORIZON_UI
