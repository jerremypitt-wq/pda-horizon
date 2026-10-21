#pragma once
// variants/elecrow-adv-35/config.h — плата Elecrow CrowPanel Advance 3.5"
// для репозитория standalone-ui / ПДА «Горизонт».
//
// Дисплей: ILI9488 (SPI) 480x320 ландшафт (или 320x480 книжный для Горизонта)
// Тач:     GT911 (I2C) addr 0x14
// Пины и тайминги сверены с официальным LovyanGFX_Driver.h Elecrow (V1.2/1.3/1.4,
// lesson-03 3_5LVGL) и схемой V1.4: RST LCD = GPIO2, BL = GPIO38 (active-high).
// ПРОВЕРИТЬ ПО ПРИБОРУ: книжная ориентация (setRotation(0) может дать перевёртыш
// на 180 градусов — тогда нужен 2); тач на i2c_port=1 (у вендора 0, но шина 1
// свободна от Arduino Wire и потому безопаснее — см. комментарий ниже).
//
// Этот файл force-include'ится сборкой -mui окружения (см. platformio.ini),
// поэтому C++-код спрятан за #ifdef __cplusplus, чтобы не ломать Си-файлы.

#include <stdint.h>
#include <string.h>

#ifndef LGFX_DRIVER
#define LGFX_DRIVER LGFX_ELECROW_ADV_35
#endif

// config.h is force-included by the -mui build. Keep C translation units from
// parsing the C++-only LovyanGFX headers and class declaration.
#ifdef __cplusplus
#include <LovyanGFX.hpp>

#if defined(HORIZON_UI) && defined(ARCH_ESP32)
#include "LittleFS.h"
#endif

class LGFX_ELECROW_ADV_35 : public lgfx::LGFX_Device
{
    lgfx::Panel_ILI9488 _panel;
    lgfx::Bus_SPI       _bus;
    lgfx::Light_PWM     _light;
    lgfx::Touch_GT911   _touch;

  public:
    // Logical panel dimensions after rotation (may be swapped for portrait).
    uint16_t screenWidth = 480;
    uint16_t screenHeight = 320;

    // This display has touch input but no physical button exposed by this driver.
    bool hasButton(void) { return false; }

    LGFX_ELECROW_ADV_35(void)
    {
        bool portrait = false;
#if defined(HORIZON_UI) && defined(ARCH_ESP32)
        portrait = horizonPortrait();
#endif
        { // SPI-шина дисплея (из firmware-варианта: SCK42 MOSI39 DC41 CS40)
            auto cfg = _bus.config();
            cfg.spi_host    = SPI2_HOST;
            cfg.spi_mode    = 0;
            cfg.freq_write  = 40000000;
            cfg.freq_read   = 16000000;
            cfg.spi_3wire   = false;
            cfg.use_lock    = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk    = 42;
            cfg.pin_mosi    = 39;
            cfg.pin_miso    = -1;
            cfg.pin_dc      = 41;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        { // Панель ILI9488 480x320 (пины/флаги — как у вендора)
            auto cfg = _panel.config();
            cfg.pin_cs   = 40;
            cfg.pin_rst  = 2;
            cfg.pin_busy = -1;
            cfg.memory_width  = 320;
            cfg.memory_height = 480;
            cfg.panel_width   = 320;
            cfg.panel_height  = 480;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            // Вендор: offset_rotation=3 (эффективная ротация 3 = ландшафт).
            // LovyanGFX считает internal=((r+offset)&3)|((r&4)^(offset&4)),
            // поэтому offset_rotation=3 при setRotation(0) == наш старый план
            // setRotation(3). Старый setRotation(1) давал перевёртыш на 180°.
            cfg.offset_rotation = portrait ? 0 : 3;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits  = 1;
            cfg.readable    = false;
            cfg.invert      = true;
            cfg.rgb_order   = false;
            cfg.dlen_16bit  = false;
            cfg.bus_shared  = true;
            _panel.config(cfg);
        }
        if (portrait) {
            screenWidth = 320;
            screenHeight = 480;
        }
        { // Подсветка BL=38
            auto cfg = _light.config();
            cfg.pin_bl      = 38;
            cfg.invert      = false;
            cfg.freq        = 44100;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        { // Тач GT911 (SDA15 SCL16 INT47 RST48, addr 0x14)
            auto cfg = _touch.config();
            // Сырые координаты GT911 на этой плате — всегда портрет 320x480;
            // в экранные их пересчитывает сам LovyanGFX по ротации панели.
            // У вендора те же 319/479 при ландшафте — дублируем как есть.
            cfg.x_min = 0;  cfg.x_max = 319;
            cfg.y_min = 0;  cfg.y_max = 479;
            cfg.offset_rotation = 0;
            cfg.bus_shared = false;
            cfg.pin_int = 47;
            cfg.pin_rst = 48;
            // У вендора i2c_port=0. У нас 1: шина 0 зарезервирована за Arduino
            // Wire (хоть он у нас и не стартует — I2C_SDA не задан), а тач на
            // отдельной шине гарантированно ни с чем не конфликтует.
            cfg.i2c_port = 1;
            cfg.i2c_addr = 0x14;
            cfg.pin_sda = 15;
            cfg.pin_scl = 16;
            cfg.freq = 400000;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }

  private:
#if defined(HORIZON_UI) && defined(ARCH_ESP32)
    // Читать ориентацию из настроек Горизонта (LittleFS уже смонтирована:
    // HorizonView::instance() вызывается после FSBegin()). Формат файла —
    // key=value, ищем строку "orientation=1".
    static bool horizonPortrait(void)
    {
        File f = LittleFS.open("/horizon.cfg", "r");
        if (!f)
            return false;
        char line[64];
        bool portrait = false;
        while (f.available()) {
            size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
            line[n] = '\0';
            if (line[0] == 'o' && !strncmp(line, "orientation=1", 13) &&
                (line[13] == '\0' || line[13] == '\r')) {
                portrait = true;
                break;
            }
        }
        f.close();
        return portrait;
    }
#endif
};
#endif // __cplusplus

// Серийный линк к LoRa-узлу (режим PROTO).
#ifndef SERIAL_RX
#define SERIAL_RX 44
#endif
#ifndef SERIAL_TX
#define SERIAL_TX 43
#endif
