// ПДА «Горизонт» — звук уведомлений через активный зуммер платы.
#ifdef HORIZON_UI

#include "horizon/HorizonSound.h"

#if defined(ARCH_ESP32)
#include <Arduino.h>
#endif

namespace {
// Зуммер CrowPanel Advance 3.5 (B1) через Q4, активный HIGH.
constexpr uint8_t kBeepPin = 8;

// Паттерны: миллисекунды вкл/выкл по очереди, 0 — конец. Каждый звучит один раз.
constexpr uint16_t kMsgSteps[] = {90, 70, 90, 70, 180, 0}; // «ди-ди-дит» ПДА
constexpr uint16_t kDmSteps[] = {70, 90, 140, 0};           // двойной блип лички
} // namespace

HorizonSound &HorizonSound::instance(void)
{
    static HorizonSound s;
    return s;
}

void HorizonSound::beepOn(bool on)
{
#if defined(ELECROW_ADV_35) && defined(ARCH_ESP32)
    digitalWrite(kBeepPin, on ? HIGH : LOW);
#else
    (void)on;
#endif
}

void HorizonSound::init(void)
{
#if defined(ELECROW_ADV_35) && defined(ARCH_ESP32)
    pinMode(kBeepPin, OUTPUT);
    digitalWrite(kBeepPin, LOW);
#endif
    steps = nullptr;
    pos = 0;
}

void HorizonSound::play(Pattern p)
{
    steps = (p == DIRECT) ? kDmSteps : kMsgSteps;
    pos = 0;
    stepUntil = millis() + steps[0];
    beepOn(true);
}

void HorizonSound::tick(void)
{
    if (!steps)
        return;
    uint32_t now = millis();
    if ((int32_t)(now - stepUntil) < 0)
        return;
    pos++;
    if (steps[pos] == 0) {
        steps = nullptr;
        beepOn(false);
        return;
    }
    stepUntil = now + steps[pos];
    beepOn((pos % 2) == 0); // чётные шаги — вкл, нечётные — пауза
}

#endif // HORIZON_UI
