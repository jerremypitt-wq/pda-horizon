#pragma once
#include "horizon_target.h"
// MAX17048 на J13: общая шина LovyanGFX I2C1 с GT911/RTC
// (GPIO15 SDA / GPIO16 SCL). Порт инициализирует тач.
#ifdef HORIZON_UI

#include <stdint.h>

struct HzBatteryState {
    int battMv = -1;        // VCELL, мВ; -1 = нет достоверного замера
    int percent = -1;       // SOC, 0..100; -1 = нет достоверного замера
    bool found = false;     // VERSION ответил, данные VCELL/SOC валидны
    bool charging = false;  // положительный CRATE; недоступный CRATE = false
    bool rateValid = false; // CRATE ответил
    int16_t rateRaw = 0;    // signed, 0.208 %/ч на единицу; не ток!
    uint16_t socRaw = 0;    // 1/256 %, для статистики наблюдаемого набора SOC
};

// Возвращает false при NACK, обрыве, занятой шине или невалидных данных.
// Опрос вызывается раз в 30 секунд из HorizonView; на следующем опросе
// повторяется и обнаружение, и чтение (при возвращении модуля).
// Не использует ADC, калибровку или Wire; разделяет I2C-порт с GT911.
bool hzBatteryPoll(HzBatteryState &st);

#endif // HORIZON_UI
