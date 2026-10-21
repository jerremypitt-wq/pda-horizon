// ПДА «Горизонт» - создание драйвера дисплея (только таргет: нужен LovyanGFX,
// хостовый syntax-check этот файл пропускает). Не включать сюда ui.h/fonts.h
// и заголовки Горизонта, тянущие LVGL-шрифты (см. HorizonDisplay.h).
#ifdef HORIZON_UI

#include "horizon/HorizonDisplay.h"
#include "config.h" // класс панели, -I variants/elecrow-adv-35

#if defined(ELECROW_ADV_35)
#include "graphics/driver/LGFXDriver.h"
#else
#include "graphics/driver/DisplayDriverFactory.h"
#endif

DisplayDriver *horizonCreateDisplayDriver(uint16_t width, uint16_t height)
{
#if defined(ELECROW_ADV_35)
    return new LGFXDriver<LGFX_ELECROW_ADV_35>(width, height);
#else
    return DisplayDriverFactory::create(width, height);
#endif
}

#endif // HORIZON_UI
