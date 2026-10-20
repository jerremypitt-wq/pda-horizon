#pragma once
// variants/elecrow-adv-35/config.h — плата Elecrow CrowPanel Advance 3.5"
// для репозитория standalone-ui. Зеркалит известную распиновку из
// meshtastic/firmware variant elecrow_panel.
//
// Дисплей: ILI9488 (SPI) 480x320 ландшафт
// Тач:     GT911 (I2C) addr 0x5D
// ПРОВЕРИТЬ ПО ПРИБОРУ: host/vsync-режим, инверсия цвета, канал PWM подсветки.

#ifndef LGFX_DRIVER
#define LGFX_DRIVER LGFX_ELECROW_ADV_35
#endif

#include <LovyanGFX.hpp>

class LGFX_ELECROW_ADV_35 : public lgfx::LGFX_Device
{
    lgfx::Panel_ILI9488 _panel;
    lgfx::Bus_SPI       _bus;
    lgfx::Light_PWM     _light;
    lgfx::Touch_GT911   _touch;

  public:
    LGFX_ELECROW_ADV_35(void)
    {
        { // SPI-шина дисплея (из firmware-варианта: SCK42 MOSI39 DC41 CS40)
            auto cfg = _bus.config();
            cfg.spi_host    = SPI3_HOST;     // на S3 свободен (SPI2 занят радио/прочим)
            cfg.spi_mode    = 0;
            cfg.freq_write  = 40000000;
            cfg.freq_read   = 16000000;
            cfg.spi_3wire   = true;
            cfg.use_lock    = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk    = 42;
            cfg.pin_mosi    = 39;
            cfg.pin_miso    = -1;
            cfg.pin_dc      = 41;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        { // Панель ILI9488 480x320
            auto cfg = _panel.config();
            cfg.pin_cs   = 40;
            cfg.pin_rst  = -1;               // у проекта сброс на EN-плате; проверить
            cfg.pin_busy = -1;
            cfg.memory_width  = 320;
            cfg.memory_height = 480;
            cfg.panel_width   = 320;
            cfg.panel_height  = 480;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.invert   = true;             // типично для ILI9488; при «негативе» -> false
            cfg.rgb_order = false;
            _panel.config(cfg);
            _panel.setRotation(1);           // ландшафт 480x320
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
        { // Тач GT911 (SDA15 SCL16 INT47 RST48, 0x5D)
            auto cfg = _touch.config();
            cfg.x_min = 0;  cfg.x_max = 479;
            cfg.y_min = 0;  cfg.y_max = 319;
            cfg.pin_int = 47;
            cfg.pin_rst = 48;
            cfg.i2c_port = 1;
            cfg.i2c_addr = 0x5D;
            cfg.pin_sda = 15;
            cfg.pin_scl = 16;
            cfg.freq = 400000;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }
};

// Серийный линк к LoRa-узлу (режим PROTO) — вывести на свободные GPIO,
// который подтверждён схемой CrowPanel Advance (см. silk-печать разъёмов).
#ifndef SERIAL_RX
#define SERIAL_RX 44
#endif
#ifndef SERIAL_TX
#define SERIAL_TX 43
#endif
