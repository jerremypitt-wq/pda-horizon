#include "horizon_target.h"
#ifdef HORIZON_UI

#include "horizon/HorizonBattery.h"
#include "Arduino.h"

namespace {
#if !defined(ARCH_PORTDUINO)
constexpr uint8_t kSda = 17; // J15.2
constexpr uint8_t kScl = 18; // J15.1
constexpr uint8_t kAddr = 0x36;
constexpr uint8_t kVcell = 0x02;
constexpr uint8_t kSoc = 0x04;
constexpr uint8_t kVersion = 0x08;
constexpr uint8_t kCrate = 0x16;
constexpr unsigned kHalfPeriodUs = 5;
constexpr unsigned kStretchTimeoutUs = 1000;

// Только открытый сток: никогда не выводить HIGH на GPIO, чтобы не подать
// напряжение батареи/логики обратно в другую цепь. Подтяжки SDA/SCL модуля
// должны идти к J15.3 (3V3), а не к BAT: это проверяют ДО подключения.
void release(uint8_t pin) { pinMode(pin, INPUT); }
void low(uint8_t pin)
{
    digitalWrite(pin, LOW);
    pinMode(pin, OUTPUT);
}
void pause(void) { delayMicroseconds(kHalfPeriodUs); }

bool highClock(void)
{
    release(kScl);
    // Если slave держит SCL (clock stretching или неисправность) — не висеть.
    for (unsigned i = 0; i < kStretchTimeoutUs / kHalfPeriodUs; ++i) {
        if (digitalRead(kScl) == HIGH) {
            pause();
            return true;
        }
        pause();
    }
    return false;
}

void stop(void)
{
    low(kSda);
    pause();
    (void)highClock();
    release(kSda);
    pause();
}

bool start(void)
{
    release(kSda);
    if (!highClock())
        return false;
    if (digitalRead(kSda) != HIGH) // шина занята / SDA застряла
        return false;
    low(kSda);
    pause();
    low(kScl);
    return true;
}

// Repeated START после отправки адреса регистра (SCL уже низкий).
bool restart(void)
{
    release(kSda);
    pause();
    if (!highClock() || digitalRead(kSda) != HIGH)
        return false;
    low(kSda);
    pause();
    low(kScl);
    return true;
}

bool writeByte(uint8_t value)
{
    for (int i = 7; i >= 0; --i) {
        if (value & (1u << i))
            release(kSda);
        else
            low(kSda);
        pause();
        if (!highClock())
            return false;
        low(kScl);
    }
    release(kSda); // ACK передаёт slave, master никогда не ведёт SDA
    pause();
    if (!highClock())
        return false;
    bool ack = digitalRead(kSda) == LOW;
    low(kScl);
    return ack;
}

bool readByte(uint8_t &value, bool ack)
{
    value = 0;
    release(kSda);
    for (int i = 0; i < 8; ++i) {
        pause();
        if (!highClock())
            return false;
        value = (uint8_t)((value << 1) | (digitalRead(kSda) == HIGH ? 1 : 0));
        low(kScl);
    }
    if (ack)
        low(kSda);
    else
        release(kSda); // NACK после младшего байта
    pause();
    if (!highClock())
        return false;
    low(kScl);
    release(kSda);
    return true;
}

bool readReg(uint8_t reg, uint16_t &value)
{
    // Не делать Wire.begin/TwoWire(1): в single-MCU I2C0 занят firmware
    // на 15/16, а I2C1 напрямую занят LovyanGFX GT911 на тех же пинах.
    if (!start()) {
        release(kSda);
        release(kScl);
        return false;
    }
    bool ok = writeByte(kAddr << 1) && writeByte(reg) && restart() &&
              writeByte((kAddr << 1) | 1);
    uint8_t msb = 0, lsb = 0;
    if (ok)
        ok = readByte(msb, true) && readByte(lsb, false);
    stop(); // отпустить шину и при NACK/таймауте
    if (ok)
        value = (uint16_t)((msb << 8) | lsb);
    return ok;
}
#endif // !ARCH_PORTDUINO
} // namespace

bool hzBatteryPoll(HzBatteryState &st)
{
    // Нет остаточных процентов при обрыве: телеметрию радио за своё
    // напряжение тоже не принимаем. Следующий опрос повторит детект.
    st = HzBatteryState{};
#if defined(ARCH_PORTDUINO)
    return false;
#else
    uint16_t version = 0, vcell = 0, soc = 0;
    if (!readReg(kVersion, version) || version == 0 || version == 0xFFFF ||
        !readReg(kVcell, vcell) || !readReg(kSoc, soc))
        return false;

    // VCELL: 78.125 мкВ/LSB (мВ = raw*5/64). Проверяем рабочий
    // диапазон MAX17048, чтобы пустую шину не показать как 100%.
    int mv = ((uint32_t)vcell * 5 + 32) / 64;
    if (mv < 2500 || mv > 4500 || soc == 0xFFFF)
        return false;
    st.found = true;
    st.battMv = mv;
    st.socRaw = soc;
    int pct = (soc + 128) / 256; // SOC: 1/256 процента на LSB
    st.percent = pct > 100 ? 100 : pct;

    uint16_t crate = 0;
    if (readReg(kCrate, crate)) {
        st.rateValid = true;
        st.rateRaw = (int16_t)crate;
        st.charging = st.rateRaw >= 4; // ~0.8 %/ч; отсекаем шум
    }
    return true;
#endif
}

#endif // HORIZON_UI
