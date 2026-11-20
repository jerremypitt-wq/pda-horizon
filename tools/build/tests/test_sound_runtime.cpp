// Host-only LEDC and settings regression test, compiled for Arduino-ESP32 2.x and 3.x.
#include "horizon/HorizonConfig.h"
#include "horizon/HorizonSound.h"
#include "LittleFS.h"
#include <cassert>
#include <cstdint>
#include <string>

fs::FS LittleFS;
static uint32_t clockMs = 0;
static uint32_t lastDuty = 0;
static uint32_t lastFreq = 0;
static uint8_t lastPin = 255;
#if TEST_ARDUINO_MAJOR < 3
static uint8_t lastChannel = 255;
#endif
static uint8_t resolution = 0;
static bool attached = false;
uint32_t millis() { return clockMs; }
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t, uint8_t) {}
#if TEST_ARDUINO_MAJOR >= 3
bool ledcAttach(uint8_t pin, uint32_t freq, uint8_t bits)
{
    lastPin = pin; lastFreq = freq; resolution = bits; attached = true; return true;
}
uint32_t ledcWriteTone(uint8_t pin, uint32_t freq)
{
    assert(pin == lastPin); lastFreq = freq; return freq;
}
bool ledcWrite(uint8_t pin, uint32_t duty)
{
    assert(pin == lastPin); lastDuty = duty; return true;
}
#else
double ledcSetup(uint8_t channel, double freq, uint8_t bits)
{
    lastChannel = channel; lastFreq = (uint32_t)freq; resolution = bits; return freq;
}
void ledcAttachPin(uint8_t pin, uint8_t channel)
{
    assert(channel == lastChannel); lastPin = pin; attached = true;
}
double ledcWriteTone(uint8_t channel, double freq)
{
    assert(channel == lastChannel); lastFreq = (uint32_t)freq; return freq;
}
void ledcWrite(uint8_t channel, uint32_t duty)
{
    assert(channel == lastChannel); lastDuty = duty;
}
#endif

int main()
{
    auto &cfg = HorizonConfig::instance();
    cfg.resetToDefaults();
    assert(cfg.soundVolume == 1 && cfg.soundSet == 0);
    // Migration: a saved pre-1.15 file without volume must sound exactly as before.
    LittleFS.files["/horizon.cfg"] = "soundSet=2\nsilent=0\n";
    cfg.load();
    assert(cfg.soundSet == 2 && cfg.soundVolume == 1);

    auto &sound = HorizonSound::instance();
    sound.init();
    assert(attached && lastPin == 8 && resolution == 10 && lastDuty == 0);
#if TEST_ARDUINO_MAJOR < 3
    assert(lastChannel == 0 && lastChannel != 7);
#endif
    sound.play(HorizonSound::MESSAGE);
    assert(sound.busy() && lastFreq == 1568 && lastDuty == 0x1FF);
    clockMs += 200;
    sound.tick();
    assert(!sound.busy() && lastDuty == 0);

    cfg.soundVolume = 0;
    cfg.soundSet = 0;
    cfg.save();
    assert(LittleFS.files["/horizon.cfg"].find("soundVolume=0\n") != std::string::npos);
    cfg.resetToDefaults();
    cfg.load();
    assert(cfg.soundVolume == 0 && cfg.soundSet == 0);
    sound.play(HorizonSound::DIRECT);
    assert(lastFreq == 784 && lastDuty == 64);
    clockMs += 90;
    sound.tick();
    assert(lastDuty == 0); // pause
    clockMs += 30;
    sound.tick();
    assert(lastDuty == 64); // next note
    cfg.silent = true;
    sound.tick();
    assert(!sound.busy() && lastDuty == 0);
    sound.play(HorizonSound::MESSAGE);
    assert(!sound.busy() && lastDuty == 0);

    LittleFS.files["/horizon.cfg"] = "soundVolume=99\n";
    cfg.load();
    assert(cfg.soundVolume == 1); // invalid persisted setting is bounded
    cfg.resetToDefaults();
    assert(cfg.soundVolume == 1);
}
