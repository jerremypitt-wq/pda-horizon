#pragma once
#include "horizon_target.h" // целевые флаги сборки (вкус: standalone/прошивка)
// ПДА «Горизонт» - настройки и досье игрока, хранение в /horizon.cfg (LittleFS).
// Формат файла - простые строки key=value в UTF-8.

#ifdef HORIZON_UI

#include <stdint.h>
#include <string>
#include <vector>

class HorizonConfig
{
  public:
    // --- Каналы чатов (индексы 0..7) ---
    uint8_t generalCh = 0;  // общий чат (primary)
    uint8_t groupCh = 1;    // чат группировки (закрытый)
    uint8_t announceCh = 2; // объявления

    // --- Мастер-пароль (PIN для редактирования досье и опасных действий) ---
    char masterPin[9] = "0000"; // до 8 цифр

    // --- Экран ---
    uint8_t brightness = 180; // 20..255
    uint16_t timeoutSec = 60; // 0 - никогда
    uint8_t orientation = 0;  // 0 - альбомная, 1 - книжная (применяется после перезагрузки)
    uint8_t theme = 0;        // 0..4, см. HorizonStrings::kThemes

    // --- Звук ---
    bool silent = false;   // бесшумный режим (без бипов уведомлений)
    uint8_t soundSet = 0;  // набор звуков 0..2 (см. HorizonSound)
    uint32_t dmsClearedAt = 0; // метка чистки личек: рестор старше отсекает (0 - не чистили)

    // --- Батарея (делитель 100к/100к на J15, замер в 1.7.0+) ---
    uint16_t vbatScale = 1000; // калибровка, промилле (800..1200)

    // --- Время (персист между перезагрузками, отдельный файл /horizon.time) ---
    uint32_t savedEpoch = 0; // последний известный epoch (UTC)
    uint32_t savedMs = 0;    // millis() в момент записи savedEpoch

    // --- Досье персонажа ---
    char charName[40] = "Сталкер";
    char callsign[40] = "Новичок";
    uint8_t rank = 0;  // 0..3
    uint8_t group = 0; // 0..9

    // --- Избранные контакты (номера узлов, персист в /horizon.cfg) ---
    static constexpr size_t kMaxFavorites = 16;
    std::vector<uint32_t> favorites;
    bool isFavorite(uint32_t node) const;
    void toggleFavorite(uint32_t node); // добавить/убрать (save отдельно!)

    HorizonConfig() = default;

    void load(void);
    void save(void) const;
    void loadTime(void);  // прочитать /horizon.time в savedEpoch/savedMs
    void saveTime(uint32_t epoch, uint32_t ms) const; // записать /horizon.time
    void resetToDefaults(void);

    bool checkPin(const char *pin) const;
    void setPin(const char *pin);

    // Имя узла в mesh-сети, видимое другим игрокам: "Позывной [ТЕГ]"
    std::string meshLongName(void) const;
    // Короткое имя узла (до 4 байт UTF-8 без разрыва символа)
    std::string meshShortName(void) const;

    static HorizonConfig &instance(void);

  private:
    HorizonConfig(const HorizonConfig &) = delete;
    HorizonConfig &operator=(const HorizonConfig &) = delete;
};

#endif // HORIZON_UI
