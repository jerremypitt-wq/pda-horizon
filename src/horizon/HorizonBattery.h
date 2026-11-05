#pragma once
#include "horizon_target.h" // целевые флаги сборки (вкус: standalone/прошивка)
// ПДА «Горизонт» - замер батареи через внешний делитель 100к/100к на J15.
// Делитель: GPIO17 (штатно) или GPIO18 (запасной), Uзам = Uбат/2.
#ifdef HORIZON_UI

#include <stdint.h>

struct HzBatteryState {
    int pin = -2;      // -2 - не искали; -1 - делителя нет; 17/18 - найден
    int rawMv = -1;    // сырой замер АЦП, мВ (-1 - нет)
    int battMv = -1;   // напряжение батареи, мВ (-1 - нет)
    int percent = -1;  // 0..100 (-1 - нет)
    uint8_t badReads = 0; // подряд невалидных чтений (3 - перепоиск пина)
};

// Медиана 7 чтений analogReadMilliVolts с отбросом нулей (нули - конфликт
// ADC2/WiFi, ядро их молча отдаёт). -1: мусор/обрыв (вне 1000..2500 мВ
// или годных меньше трёх).
int hzBatteryReadPinMv(int pin);
// Автопоиск делителя: 17, иначе 18, иначе -1 (окно 1400..2250 мВ).
int hzBatteryDetectPin(void);
// Проценты по напряжению батареи (линейная интерполяция таблицы Li-ion).
int hzBatteryPercent(int battMv);
// Один цикл опроса: поиск/чтение/пересчёт. scale - калибровка, промилле.
void hzBatteryPoll(HzBatteryState &st, uint16_t scale);

#endif // HORIZON_UI
