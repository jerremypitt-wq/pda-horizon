#pragma once
#include "horizon_target.h" // целевые флаги сборки (вкус: standalone/прошивка)
// ПДА «Горизонт» - создание драйвера дисплея в отдельной единице трансляции.
//
// Причина: LovyanGFX объявляет глобальный `namespace fonts`
// (lgfx_fonts.hpp, глобальный using), а device-ui - глобальный массив
// `fonts[]` (generated/ui_320x240/fonts.h, тянется через HorizonTheme.h).
// Вместе в одной единице трансляции они не компилируются, поэтому весь
// LovyanGFX живёт только в HorizonDisplay.cpp, а остальной код его не видит
// (и наоборот: сюда нельзя тащить ui.h/fonts.h).

#ifdef HORIZON_UI

#include <stdint.h>

class DisplayDriver;

DisplayDriver *horizonCreateDisplayDriver(uint16_t width, uint16_t height);
// Только для Elecrow: на экране карты временно альбомная ориентация,
// остальные экраны возвращаются к ориентации из настроек.
void horizonSetMapLandscape(DisplayDriver *driver, bool enabled);

#endif // HORIZON_UI
