#pragma once
// ПДА «Горизонт» - звук уведомлений через активный зуммер платы.
// CrowPanel Advance 3.5: BEEP_5025 (активный, фиксированный тон ~2.7 кГц),
// включается транзистором Q4 от GPIO8 (HIGH = пищит). Мелодий нет - только
// ритм: короткие паттерны вкл/выкл, каждый проигрывается ОДИН раз.

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
    static constexpr uint8_t kSetCount = 3; // наборов звуков: Сталкер, Двойной, Одиночный

    static HorizonSound &instance(void);

    void init(void);        // настроить пин (вызывать один раз при старте)
    void play(Pattern p);   // запустить паттерн (одиночное проигрывание)
    void tick(void);        // двигать проигрывание (вызывать из task_handler)
    bool busy(void) const { return steps != nullptr; }

  private:
    HorizonSound() = default;

    static void beepOn(bool on);

    const uint16_t *steps = nullptr; // чередование мс вкл/выкл, начинается с вкл, 0 - конец
    size_t pos = 0;
    uint32_t stepUntil = 0;

    HorizonSound(const HorizonSound &) = delete;
    HorizonSound &operator=(const HorizonSound &) = delete;
};

#endif // HORIZON_UI
