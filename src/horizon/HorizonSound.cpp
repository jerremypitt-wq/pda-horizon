// ПДА «Горизонт» - звук уведомлений через активный зуммер платы.
#ifdef HORIZON_UI

#include "horizon/HorizonSound.h"
#include "horizon/HorizonConfig.h"

#if defined(ARCH_ESP32)
#include <Arduino.h>
#endif

namespace {
// Зуммер CrowPanel Advance 3.5 (B1) через Q4, активный HIGH.
constexpr uint8_t kBeepPin = 8;

// Наборы звуков [набор][MESSAGE/DIRECT]: миллисекунды вкл/выкл по очереди,
// 0 - конец. Зуммер активный (тон один) - различаются только ритмом.
constexpr uint16_t kSetStalkerMsg[] = {70, 60, 70, 60, 240, 0}; // ди-ди-даа ПДА
constexpr uint16_t kSetStalkerDm[] = {70, 60, 150, 0};           // ди-даа лички
constexpr uint16_t kSetDoubleMsg[] = {110, 90, 110, 0};          // бип-бип
constexpr uint16_t kSetDoubleDm[] = {110, 90, 110, 90, 180, 0};  // бип-бип-бип
constexpr uint16_t kSetSingleMsg[] = {170, 0};                   // один бип
constexpr uint16_t kSetSingleDm[] = {90, 0};                     // короткий блип
const uint16_t *const kSets[HorizonSound::kSetCount][2] = {
    {kSetStalkerMsg, kSetStalkerDm}, {kSetDoubleMsg, kSetDoubleDm}, {kSetSingleMsg, kSetSingleDm}};
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
    if (HorizonConfig::instance().silent)
        return; // бесшумный режим: ни звука
    uint8_t s = HorizonConfig::instance().soundSet;
    if (s >= kSetCount)
        s = 0;
    steps = kSets[s][p == DIRECT ? 1 : 0];
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
    beepOn((pos % 2) == 0); // чётные шаги - вкл, нечётные - пауза
}

#endif // HORIZON_UI
