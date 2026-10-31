#include "horizon_target.h" // целевые флаги сборки (вкус: standalone/прошивка)
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

#if defined(ELECROW_ADV_35)
// Не меняем форк device-ui: его драйвер хранит панель в protected TFTDriver::tft.
// На Elecrow offset_rotation=0 в портретной настройке: setRotation(3) даёт
// нужный альбомный вид и одновременно поворачивает координаты тача GT911.
class HorizonElecrowDriver : public LGFXDriver<LGFX_ELECROW_ADV_35> {
  public:
    using LGFXDriver<LGFX_ELECROW_ADV_35>::LGFXDriver;
    void mapLandscape(bool enabled)
    {
        if (!horizonIsPortrait() || enabled == mapMode)
            return;
        mapMode = enabled;
        {
            ISpiLock::Guard bus; // панель сидит на общей SPI-шине
            this->tft->setRotation(enabled ? 3 : 0);
        }
        lv_display_set_resolution(this->getDisplay(), enabled ? 480 : 320, enabled ? 320 : 480);
    }

  private:
    bool mapMode = false;
};
static HorizonElecrowDriver *elecrowDriver = nullptr;
#endif

DisplayDriver *horizonCreateDisplayDriver(uint16_t width, uint16_t height)
{
#if defined(ELECROW_ADV_35)
    elecrowDriver = new HorizonElecrowDriver(width, height);
    return elecrowDriver;
#else
    return DisplayDriverFactory::create(width, height);
#endif
}

void horizonSetMapLandscape(DisplayDriver *driver, bool enabled)
{
#if defined(ELECROW_ADV_35)
    if (driver && driver == elecrowDriver)
        elecrowDriver->mapLandscape(enabled);
#else
    (void)driver;
    (void)enabled;
#endif
}

#endif // HORIZON_UI
