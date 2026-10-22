#pragma once
// ПДА «Горизонт» - целевые флаги сборки (вкус standalone / прошивка).
//
// Назначение: общий include-ПЕРВЫМ-файлом для всех единиц трансляции Горизонта.
// Дерево прошивки Meshtastic собирается стоковым env elecrow-adv-35-tft БЕЗ
// правок platformio.ini: fw/overlay.py поверх копирования перезаписывает этот
// файл версией fw/horizon_target_firmware.h (добавляется HORIZON_PACKET_LINK).
//
// В standalone-сборке те же флаги уже заданы через -D в platformio.ini - здесь
// они продублированы под #ifndef как страховка (безвредно, варнингов нет).
//
// Что здесь НЕ определяется и почему:
// - ARCH_ESP32: в дереве прошивки он не задан глобально, а локальное
//   определение рассинхронизировало бы lv_conf.h (LV_ATTRIBUTE_FAST_MEM /
//   IRAM_ATTR) между lib device-ui и нашими TU. Вместо этого все наши
//   #if defined(ARCH_ESP32) продублированы через ARDUINO_ARCH_ESP32,
//   который Arduino-платформа задаёт глобально в обоих вкусах.
// - USE_ILOG: в standalone он идёт через -D, в прошивке логирование
//   включается глобально парой USE_LOG_DEBUG + LOG_DEBUG_INC из ini
//   варианта elecrow_panel; оба пути дают рабочие макросы ILOG_*.
//
// Файл обязан оставаться чистым Си (инклудится и из HorizonLogo.c).
#ifndef HORIZON_UI
#define HORIZON_UI
#endif
#ifndef ELECROW_ADV_35
#define ELECROW_ADV_35
#endif
