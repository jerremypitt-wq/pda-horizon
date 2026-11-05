#if HAS_TFT

// ПДА «Горизонт» - клей single-MCU для дерева прошивки Meshtastic.
//
// Заменяет штатный src/graphics/tftSetup.cpp (оригинал сохраняется рядом как
// tftSetup.cpp.stock, см. fw/overlay.py): вместо DeviceScreen поднимается
// HorizonView на внутреннем PacketAPI (PacketServer + PacketClient).
// Структура повторяет штатный файл v2.8.1: инициализация в tftSetup(),
// прокачка LVGL в отдельной задаче FreeRTOS "tft" (ядро 0, приоритет 1).
//
// Осознанные отличия от штатного файла:
// - экземпляр DeviceScreen НЕ создаётся: драйвер дисплея (LGFX_ELECROW_ADV_35,
//   тач 0x14) и весь UI принадлежат HorizonView. Глобал `deviceScreen`
//   определён как nullptr только ради extern-символа (WakeKey.cpp; там есть
//   null-check, для нашего варианта файл вообще не собирается);
// - нет обсерверов light sleep: они привязаны к методам DeviceScreen,
//   а HorizonView - соседний класс. Дисплеем управляет сам DisplayDriver
//   (яркость/таймаут из настроек Горизонта); CrowPanel питается от USB,
//   light sleep для неё нехарактерен;
// - нет веток Portduino/SenseCap: этот файл - только для esp32-s3.

#include <Arduino.h>

#include "api/PacketAPI.h"
#include "comms/PacketClient.h"
#include "comms/PacketServer.h"
#include "graphics/DeviceScreen.h" // только тип для глобала deviceScreen
#include "horizon/HorizonView.h"
#include "util/ILog.h"

// Символ для WakeKey.cpp (см. выше). Экраном владеет HorizonView.
DeviceScreen *deviceScreen = nullptr;

#ifndef TFT_TASK_STACK_SIZE
#define TFT_TASK_STACK_SIZE 16384
#endif

static void hz_tft_task_handler(void *param)
{
    (void)param;
    for (;;) {
        HorizonView::instance().task_handler();
        vTaskDelay(pdMS_TO_TICKS(5)); // как delay(5) в loop() standalone
    }
}

void tftSetup(void)
{
    ILOG_INFO("Horizon: tftSetup (packet link)");
    PacketAPI::create(PacketServer::init());
    HorizonView::instance().init(new PacketClient);
    xTaskCreatePinnedToCore(hz_tft_task_handler, "tft", TFT_TASK_STACK_SIZE, NULL, 1, NULL, 0);
    ILOG_INFO("Horizon: tft task started");
}

#endif
