#pragma once
#include "horizon_target.h"
// MAX17048 на J15: программная I2C-шина (GPIO17 SDA / GPIO18 SCL).
// Аппаратные I2C0/I2C1 заняты прошивкой и тачем GT911.
#ifdef HORIZON_UI

#include <stdint.h>

struct HzBatteryState {
    int battMv = -1;        // VCELL, мВ; -1 = нет достоверного замера
    int percent = -1;       // SOC, 0..100; -1 = нет достоверного замера
    bool found = false;     // VERSION ответил, данные VCELL/SOC валидны
    bool charging = false;  // положительный CRATE; недоступный CRATE = false
};

// Возвращает false при NACK, обрыве, занятой шине или невалидных данных.
// Опрос вызывается раз в 30 секунд из HorizonView; на следующем опросе
// повторяется и обнаружение, и чтение (при возвращении модуля).
// Не использует ADC, калибровку или контроллеры Wire/GT911.
bool hzBatteryPoll(HzBatteryState &st);

#endif // HORIZON_UI
