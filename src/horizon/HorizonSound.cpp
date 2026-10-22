// ПДА «Горизонт» - звук уведомлений через пассивный зуммер (tone/noTone).
#ifdef HORIZON_UI

#include "horizon/HorizonSound.h"
#include "horizon/HorizonConfig.h"

#if defined(ARCH_ESP32)
#include <Arduino.h>
#endif

namespace {
// Зуммер CrowPanel Advance 3.5 (B1) на GPIO8 через Q4, активный HIGH.
constexpr uint8_t kBeepPin = 8;

// Мелодии наборов [набор][MESSAGE/DIRECT]: {частота Гц, мс}, {0,0} - конец.
const HorizonSound::ToneStep kSetStalkerMsg[] = {
    {880, 90}, {0, 60}, {1174, 90}, {0, 60}, {784, 240}, {0, 0}}; // ди-ди-даа ПДА
const HorizonSound::ToneStep kSetStalkerDm[] = {{880, 80}, {0, 60}, {659, 170}, {0, 0}};
const HorizonSound::ToneStep kSetDoubleMsg[] = {{2093, 100}, {0, 90}, {2093, 100}, {0, 0}};
const HorizonSound::ToneStep kSetDoubleDm[] = {{2093, 90}, {0, 70}, {2093, 90}, {0, 70}, {2093, 140}, {0, 0}};
const HorizonSound::ToneStep kSetSingleMsg[] = {{1568, 200}, {0, 0}};
const HorizonSound::ToneStep kSetSingleDm[] = {{1568, 110}, {0, 0}};
const HorizonSound::ToneStep *const kSets[HorizonSound::kSetCount][2] = {
    {kSetStalkerMsg, kSetStalkerDm}, {kSetDoubleMsg, kSetDoubleDm}, {kSetSingleMsg, kSetSingleDm}};

void hzToneOn(uint16_t freq)
{
#if defined(ELECROW_ADV_35) && defined(ARCH_ESP32)
    tone(kBeepPin, freq);
#else
    (void)freq;
#endif
}

void hzToneOff(void)
{
#if defined(ELECROW_ADV_35) && defined(ARCH_ESP32)
    noTone(kBeepPin);
#endif
}

void hzApplyStep(const HorizonSound::ToneStep &st)
{
    if (st.freq > 0)
        hzToneOn(st.freq);
    else
        hzToneOff();
}
} // namespace

HorizonSound &HorizonSound::instance(void)
{
    static HorizonSound s;
    return s;
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
    stepUntil = millis() + steps[0].ms;
    hzApplyStep(steps[0]);
}

void HorizonSound::tick(void)
{
    if (!steps)
        return;
    uint32_t now = millis();
    if ((int32_t)(now - stepUntil) < 0)
        return;
    pos++;
    if (steps[pos].ms == 0) {
        steps = nullptr;
        hzToneOff();
        return;
    }
    stepUntil = now + steps[pos].ms;
    hzApplyStep(steps[pos]);
}

#endif // HORIZON_UI
