#include "horizon_target.h"
#ifdef HORIZON_UI

#include "horizon/HorizonBattery.h"
#include "horizon/HorizonI2cBus.h"

#if !defined(ARCH_PORTDUINO)
#include <LovyanGFX.hpp>

namespace {
// J13: SDA=GPIO15, SCL=GPIO16. GT911 уже инициализирует I2C-порт 1
// LovyanGFX на этих пинах. Нельзя запускать Wire/TwoWire или битбэнг на
// общей шине: транзакции MAX должны брать тот же lock, что и тач.
constexpr int kAddr = 0x36;
constexpr uint32_t kFreq = 100000; // тач использует 400 кГц; частота задаётся на транзакцию
constexpr uint8_t kVcell = 0x02;
constexpr uint8_t kSoc = 0x04;
constexpr uint8_t kVersion = 0x08;
constexpr uint8_t kCrate = 0x16;

bool readReg(uint8_t reg, uint16_t &value)
{
    uint8_t bytes[2] = {};
    // На неисправной/неинициализированной конфигурации не переназначать
    // GPIO и не трогать чужую шину. Порт GT911 инициализируется дисплеем.
    auto sda = lgfx::i2c::getPinSDA(HorizonI2cBus::kPort);
    auto scl = lgfx::i2c::getPinSCL(HorizonI2cBus::kPort);
    if (!sda.has_value() || !scl.has_value() || sda.value() != HorizonI2cBus::kSda || scl.value() != HorizonI2cBus::kScl)
        return false;
    // Тот же transactionWriteRead, что использует GT911: захват lock порта,
    // repeated START, чтение двух байт и STOP даже при NACK/ошибке.
    if (!lgfx::i2c::transactionWriteRead(HorizonI2cBus::kPort, kAddr, &reg, 1,
                                         bytes, sizeof(bytes), kFreq).has_value())
        return false;
    value = (uint16_t)((bytes[0] << 8) | bytes[1]); // MSB первым
    return true;
}
} // namespace
#endif

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
