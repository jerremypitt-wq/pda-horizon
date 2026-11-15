#pragma once
#include <stdint.h>
#include <string.h>

// Четыре цифры, включая ведущие нули. Не является шифрованием хранилища.
inline bool hzValidFourDigitPin(const char *pin)
{
    if (!pin || strlen(pin) != 4)
        return false;
    for (int i = 0; i < 4; ++i)
        if (pin[i] < '0' || pin[i] > '9')
            return false;
    return true;
}

// Не блокировать экран, пока пользователь держит палец. Таймер LVGL
// обновляется по касаниям и не привязан к гашению подсветки.
inline bool hzShouldLock(bool enabled, uint16_t timeoutSec, uint32_t inactiveMs, bool fingerPressed)
{
    return enabled && timeoutSec > 0 && !fingerPressed && inactiveMs >= (uint32_t)timeoutSec * 1000;
}
