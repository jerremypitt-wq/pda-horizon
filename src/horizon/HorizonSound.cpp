#include "horizon_target.h" // целевые флаги сборки (вкус: standalone/прошивка)
// ПДА «Горизонт» - мелодии и две ступени громкости пассивного зуммера.
#ifdef HORIZON_UI

#include "horizon/HorizonSound.h"
#include "horizon/HorizonConfig.h"

#if defined(ARCH_ESP32) || defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#if defined(ELECROW_ADV_35)
#include <esp_arduino_version.h>
#endif
#endif

namespace {
// Зуммер CrowPanel Advance 3.5 (B1) на GPIO8 через Q4, активный HIGH.
constexpr uint8_t kBeepPin = 8;
// LEDC, 10 бит: 0x1FF повторяет прежний tone(); 64/1024 — тихая ступень.
// У пьезо реальная разница зависит от платы: нужна проверка на устройстве.
constexpr uint32_t kLoudDuty = 0x1FF;
constexpr uint32_t kQuietDuty = 64;
#if defined(ELECROW_ADV_35) && (defined(ARCH_ESP32) || defined(ARDUINO_ARCH_ESP32))
bool sPwmReady = false;
#if ESP_ARDUINO_VERSION_MAJOR < 3
// В Arduino-ESP32 2.x tone() использовал канал 0; подсветка платы — канал 7.
constexpr uint8_t kBeepChannel = 0;
#endif
#endif

// Мелодии наборов [набор][MESSAGE/DIRECT]: {частота Гц, мс}, {0,0} - конец.
// «Тройной»: два низких (Соль2, 784 Гц) + высокий на уровне одиночного
// (Соль3, 1568 Гц). Паузы короткие (30 мс) - стучит, а не тянет.
const HorizonSound::ToneStep kSetTripleMsg[] = {
    {784, 100}, {0, 30}, {784, 100}, {0, 30}, {1568, 150}, {0, 0}};
const HorizonSound::ToneStep kSetTripleDm[] = {
    {784, 90}, {0, 30}, {784, 90}, {0, 30}, {1568, 120}, {0, 0}};
// «Двойной»: та же высота, что у одиночного (1568 Гц), пауза короткая.
const HorizonSound::ToneStep kSetDoubleMsg[] = {{1568, 100}, {0, 30}, {1568, 100}, {0, 0}};
const HorizonSound::ToneStep kSetDoubleDm[] = {
    {1568, 90}, {0, 30}, {1568, 90}, {0, 30}, {1568, 140}, {0, 0}};
const HorizonSound::ToneStep kSetSingleMsg[] = {{1568, 200}, {0, 0}};
const HorizonSound::ToneStep kSetSingleDm[] = {{1568, 110}, {0, 0}};
const HorizonSound::ToneStep *const kSets[HorizonSound::kSetCount][2] = {
    {kSetTripleMsg, kSetTripleDm}, {kSetDoubleMsg, kSetDoubleDm}, {kSetSingleMsg, kSetSingleDm}};

void hzToneOn(uint16_t freq)
{
#if defined(ELECROW_ADV_35) && (defined(ARCH_ESP32) || defined(ARDUINO_ARCH_ESP32))
    if (!sPwmReady)
        return;
    const uint32_t duty = HorizonConfig::instance().soundVolume == 0 ? kQuietDuty : kLoudDuty;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWriteTone(kBeepPin, freq);
    ledcWrite(kBeepPin, duty);
#else
    ledcWriteTone(kBeepChannel, freq);
    ledcWrite(kBeepChannel, duty);
#endif
#else
    (void)freq;
#endif
}

void hzToneOff(void)
{
#if defined(ELECROW_ADV_35) && (defined(ARCH_ESP32) || defined(ARDUINO_ARCH_ESP32))
    if (!sPwmReady)
        return;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(kBeepPin, 0);
#else
    ledcWrite(kBeepChannel, 0);
#endif
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
#if defined(ELECROW_ADV_35) && (defined(ARCH_ESP32) || defined(ARDUINO_ARCH_ESP32))
    pinMode(kBeepPin, OUTPUT);
    digitalWrite(kBeepPin, LOW);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    // 3.x сам выделяет свободный канал: не отбирать канал 7 у подсветки.
    sPwmReady = ledcAttach(kBeepPin, 1568, 10);
#else
    sPwmReady = ledcSetup(kBeepChannel, 1568, 10) > 0;
    if (sPwmReady)
        ledcAttachPin(kBeepPin, kBeepChannel);
#endif
    hzToneOff();
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

void HorizonSound::stop(void)
{
    steps = nullptr;
    hzToneOff();
}

void HorizonSound::tick(void)
{
    if (!steps)
        return;
    if (HorizonConfig::instance().silent) {
        stop();
        return;
    }
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
