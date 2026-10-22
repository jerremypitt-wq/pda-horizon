#pragma once
// ПДА «Горизонт» - целевые флаги сборки, ВКУС ПРОШИВКИ (single-MCU).
//
// Этот файл fw/overlay.py кладёт поверх src/horizon/horizon_target.h в дереве
// прошивки Meshtastic v2.8.1. От standalone-версии отличается только флагом
// HORIZON_PACKET_LINK: данные идут по внутреннему PacketAPI (PacketServer +
// PacketClient), а не по UART от внешнего радиомодуля.
//
// Следствия HORIZON_PACKET_LINK в коде Горизонта:
// - HorizonView: плоские сигнатуры setMyInfo/updateMetrics апстримного
//   MeshtasticView (f77bb70), без hasKnownNodeForPacket;
// - notifyConnected("packet") в конце init (PacketClient колбэков не шлёт);
// - строки про «радиомодуль»/«узел» заменены на «ПДА», About без UART-счётчиков.
//
// ARCH_ESP32 намеренно НЕ определяется (см. комментарий в standalone-версии).
#ifndef HORIZON_UI
#define HORIZON_UI
#endif
#ifndef ELECROW_ADV_35
#define ELECROW_ADV_35
#endif
#ifndef HORIZON_PACKET_LINK
#define HORIZON_PACKET_LINK
#endif
