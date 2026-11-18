#include "horizon_target.h" // целевые флаги сборки (вкус: standalone/прошивка)
// ПДА «Горизонт» - создание драйвера дисплея (только таргет: нужен LovyanGFX,
// хостовый syntax-check этот файл пропускает). Не включать сюда ui.h/fonts.h
// и заголовки Горизонта, тянущие LVGL-шрифты (см. HorizonDisplay.h).
#ifdef HORIZON_UI

#include "horizon/HorizonDisplay.h"
#include "config.h" // класс панели, -I variants/elecrow-adv-35

#if defined(ELECROW_ADV_35)
#include "graphics/driver/LGFXDriver.h"
#include "horizon/HorizonWakeGate.h"
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

    void init(DeviceGUI *gui) override
    {
        LGFXDriver<LGFX_ELECROW_ADV_35>::init(gui);
        // Стандартный драйвер читает только первую точку GT911. Перехват в
        // нашем подклассе нужен ДО LVGL: иначе multi-touch обновит таймер
        // активности и базовый драйвер включит подсветку ещё до проверки.
        if (this->touch) {
            lv_indev_set_user_data(this->touch, this);
            lv_indev_set_read_cb(this->touch, readWakeTouch);
            // WakeGate ждёт повторный опрос ПОСЛЕ отпускания (40 мс).
            // В event-only режиме GT911 может не выдать второй IRQ, и
            // экран останется тёмным до перезагрузки. Опрос по таймеру
            // обязателен даже в сборке с USE_TOUCH_EVENTS.
            lv_indev_set_mode(this->touch, LV_INDEV_MODE_TIMER);
        }
    }

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
    static void readWakeTouch(lv_indev_t *indev, lv_indev_data_t *data)
    {
        auto *self = static_cast<HorizonElecrowDriver *>(lv_indev_get_user_data(indev));
        if (!self) {
            data->state = LV_INDEV_STATE_RELEASED;
            return;
        }
        lgfx::touch_point_t points[5];
        uint8_t count;
        {
            ISpiLock::Guard bus;
            count = self->tft->getTouch(points, 5); // GT911: координаты уже с учётом поворота
        }
        uint32_t inactive = lv_display_get_inactive_time(self->getDisplay());
        bool dark = self->powerSaving || (self->screenTimeout && inactive >= self->screenTimeout);
        if (dark) {
            if (!self->wakeSession) {
                self->wakeSession = true;
                self->wakeGate.reset();
            }
            // НИ ОДНО касание во время сна не передаём LVGL. Даже первое
            // нажатие тогда не обновит его таймер и не нажмёт кнопку под ним.
            if (!self->view->isScreenLocked() &&
                self->wakeGate.sample(lv_tick_get(), count, count ? points[0].x : 0, count ? points[0].y : 0)) {
                lv_display_trigger_activity(self->getDisplay());
                // Пока подсветка лишь гаснет, активности достаточно. Флаг
                // forceWakeup нужен только после фактического powersave:
                // иначе он останется висеть до следующего засыпания.
                if (self->powerSaving)
                    self->forceWakeup();
            }
            data->state = LV_INDEV_STATE_RELEASED;
            data->point = self->lastTouch;
            return;
        }
        if (self->wakeSession) {
            // Даже если подсветку включила внешняя активность, не отдаём
            // удерживаемую руку меню до полного отпускания всех пальцев.
            data->state = LV_INDEV_STATE_RELEASED;
            data->point = self->lastTouch;
            if (count == 0) {
                self->wakeSession = false;
                self->wakeGate.reset();
            }
            return;
        }
        data->state = count ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        if (count) {
            self->lastTouch = {(lv_coord_t)points[0].x, (lv_coord_t)points[0].y};
            data->point = self->lastTouch;
        } else {
            data->point = self->lastTouch;
        }
    }

    HorizonWakeGate wakeGate;
    lv_point_t lastTouch = {0, 0};
    bool wakeSession = false;
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
