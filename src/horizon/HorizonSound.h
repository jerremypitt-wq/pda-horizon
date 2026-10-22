#pragma once
// ПДА «Горизонт» - звук уведомлений через пассивный пьезозуммер платы.
// CrowPanel Advance 3.5: BUZZER на GPIO8 - ПАССИВНЫЙ (от постоянного уровня
// только щёлкает: проверено на слух и по распиновке семейства Advance).
// Поэтому пищим через tone()/noTone() - можно играть мелодии, а не только ритм.

#ifdef HORIZON_UI

#include <stddef.h>
#include <stdint.h>

class HorizonSound
{
  public:
    enum Pattern : uint8_t {
        MESSAGE = 0, // входящее в канал
        DIRECT = 1,  // личное сообщение
    };
    static constexpr uint8_t kSetCount = 3; // наборов: Сталкер, Двойной, Одиночный

    // Шаг мелодии: частота в Гц (0 - пауза) + длительность в мс. Конец - {0, 0}.
    struct ToneStep {
        uint16_t freq;
        uint16_t ms;
    };

    static HorizonSound &instance(void);

    void init(void);      // настроить пин (вызывать один раз при старте)
    void play(Pattern p); // запустить мелодию набора (одиночное проигрывание)
    void tick(void);      // двигать проигрывание (вызывать из task_handler)
    bool busy(void) const { return steps != nullptr; }

  private:
    HorizonSound() = default;

    const ToneStep *steps = nullptr;
    size_t pos = 0;
    uint32_t stepUntil = 0;

    HorizonSound(const HorizonSound &) = delete;
    HorizonSound &operator=(const HorizonSound &) = delete;
};

#endif // HORIZON_UI
