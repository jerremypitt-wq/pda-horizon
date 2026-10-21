#pragma once
// ПДА «Горизонт» — строки интерфейса, группировки, ранги, темы.
// Всё в UTF-8. Шрифты ui_font_montserrat_* содержат кириллицу и базовые эмодзи.

#ifdef HORIZON_UI

#include <stdint.h>

namespace HorizonStrings {

// Аппарат/версия
constexpr const char *kAppName = "ПДА «ГОРИЗОНТ»";
constexpr const char *kAppSub = "MESH-СВЯЗЬ // STALKER NET";
constexpr const char *kFirmwareVersion = "1.0.0";

// Ранги персонажа
constexpr uint8_t kRankCount = 4;
extern const char *const kRanks[kRankCount];

// Группировки (досье)
constexpr uint8_t kGroupCount = 10;
extern const char *const kGroups[kGroupCount];
// Короткие теги группировок для отображения в сети (видны другим игрокам)
extern const char *const kGroupTags[kGroupCount];

// Цветовые темы оформления
constexpr uint8_t kThemeCount = 5;
extern const char *const kThemes[kThemeCount];

// Регионы LoRa (сокращённый список самых ходовых) + коды meshtastic
struct RegionEntry {
    const char *name;
    int code; // meshtastic_Config_LoRaConfig_RegionCode
};
extern const RegionEntry kRegions[];
extern const uint8_t kRegionCount;

// Модем-пресеты (сокращённый список)
struct PresetEntry {
    const char *name;
    int code; // meshtastic_Config_LoRaConfig_ModemPreset
};
extern const PresetEntry kPresets[];
extern const uint8_t kPresetCount;

// Роли устройства (сокращённый список)
struct RoleEntry {
    const char *name;
    int code; // meshtastic_Config_DeviceConfig_Role
};
extern const RoleEntry kRoles[];
extern const uint8_t kRoleCount;

// Таймауты экрана
struct TimeoutEntry {
    const char *name;
    int seconds; // 0 — никогда
};
extern const TimeoutEntry kTimeouts[];
extern const uint8_t kTimeoutCount;

// Простые ключи каналов (PSK shorthand): 0=без ключа, 1=default, 2..11=simple1..10
constexpr uint8_t kSimpleKeyCount = 12;
extern const char *const kSimpleKeys[kSimpleKeyCount];

// Глифы для плиток и значков.
// ВАЖНО: шрифты ui_font_montserrat_16/20 содержат только латиницу, Latin-1,
// греческий и кириллицу — БЕЗ символов и эмодзи. Шрифты 12/14 дополнительно
// содержат эмодзи U+1F300..U+1F6C6, но НЕ содержат Misc Symbols/Georgian/Dingbats
// (★ ☢ ☰ ⚠ ⚙ ✉ отсутствуют ВО ВСЕХ размерах — проверено по cmap шрифтов).
// Поэтому: значки — только эмодзи из U+1F300..U+1F6C6 и только шрифтами 12/14.
constexpr const char *kGlyphGeneral = "📢"; // общий чат (мегафон)
constexpr const char *kGlyphGroup = "⭐";    // чат группировки (звезда)
constexpr const char *kGlyphAnnounce = "📣"; // объявления (рупор)
constexpr const char *kGlyphContacts = "👥"; // контакты (люди)
constexpr const char *kGlyphChats = "💬";    // открытые чаты
constexpr const char *kGlyphDossier = "📋";  // досье
constexpr const char *kGlyphSettings = "🔧"; // настройки (ключ)
constexpr const char *kGlyphLock = "🔒";
constexpr const char *kGlyphUnlock = "🔓";
constexpr const char *kGlyphMail = "📩";  // непрочитанные/тосты
constexpr const char *kGlyphClock = "🕐"; // часы в статусбаре
constexpr const char *kGlyphWarn = "!";   // ASCII: misc-символов нет в шрифтах

} // namespace HorizonStrings

#endif // HORIZON_UI
