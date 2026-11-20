#pragma once
// Общая шина J13 / GT911 / RTC на CrowPanel Advance 3.5.
// Инициализацией владеет GT911 (LovyanGFX), MAX только совершает транзакции.
namespace HorizonI2cBus {
constexpr int kPort = 1;
constexpr int kSda = 15;
constexpr int kScl = 16;
} // namespace HorizonI2cBus
