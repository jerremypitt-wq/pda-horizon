// Build: g++ -std=c++17 -Wall -Wextra -Werror -Isrc -Itools/tests/stubs
// src/horizon/HorizonBatteryStats.cpp tools/tests/test_security_battery_stats.cpp -o /tmp/test_security_battery_stats
#include "horizon/HorizonSecurity.h"
#include "horizon/HorizonBatteryStats.h"
#include "LittleFS.h"
#include <cassert>
#include <cstdio>

fs::FS LittleFS;

int main()
{
    assert(hzValidFourDigitPin("0000") && hzValidFourDigitPin("9421"));
    assert(!hzValidFourDigitPin("") && !hzValidFourDigitPin("123") &&
           !hzValidFourDigitPin("12345") && !hzValidFourDigitPin("12a4") &&
           !hzValidFourDigitPin(nullptr));
    assert(!hzShouldLock(false, 60, 999999, false));
    assert(!hzShouldLock(true, 60, 59999, false));
    assert(!hzShouldLock(true, 60, 60000, true)); // finger held
    assert(hzShouldLock(true, 60, 60000, false));
    assert(!hzShouldLock(true, 0, 999999, false));

    HzBatteryStats s;
    HzBatteryState b;
    b.found = b.rateValid = true;
    b.rateRaw = 5;
    b.battMv = 3500;
    b.socRaw = 50 * 256;
    assert(s.observe(b, 1000));
    assert(s.chargeStarts == 1 && s.chargeSeconds == 0 && s.minMv == 3500);
    b.socRaw += 128; b.battMv = 3550;
    assert(!s.observe(b, 31000));
    assert(s.chargeSeconds == 30 && s.charged256 == 128 && s.maxMv == 3550);
    b.rateRaw = -8; b.socRaw -= 128;
    assert(s.observe(b, 61000));
    assert(s.dischargeStarts == 1 && s.dischargeSeconds == 0 && s.charged256 == 128);
    assert(!s.observe(b, 91000));
    assert(s.dischargeSeconds == 30);
    b.rateValid = false;
    assert(!s.observe(b, 121000));
    b.rateValid = true; b.rateRaw = 4; b.socRaw = 90 * 256;
    assert(s.observe(b, 151000)); // gap: no invented capacity/time
    assert(s.chargeStarts == 2 && s.charged256 == 128);
    b.socRaw = 96 * 256;
    assert(!s.observe(b, 181000)); // >5% step ignored, no invented cycles
    assert(s.charged256 == 128);
    b.socRaw = 97 * 256;
    assert(!s.observe(b, 400000)); // >2 min gap ignored
    assert(s.chargeSeconds == 60 && s.charged256 == 128);
    assert(s.dirty && s.save() && !s.dirty);
    assert(LittleFS.files.count("/horizon.battery") == 1);
    HzBatteryStats voltageOnly;
    b.rateValid = false; b.battMv = 3210;
    assert(!voltageOnly.observe(b, 2000));
    assert(voltageOnly.minMv == 3210 && voltageOnly.maxMv == 3210 && voltageOnly.dirty);
    HzBatteryStats restored;
    restored.load();
    assert(restored.chargeStarts == 2 && restored.dischargeStarts == 1);
    assert(restored.chargeSeconds == 60 && restored.dischargeSeconds == 30);
    assert(restored.minMv == 3500 && restored.maxMv == 3550);
    assert(restored.charged256 == 128 && !restored.dirty);
    LittleFS.files["/horizon.battery"] = "broken";
    HzBatteryStats corrupt;
    corrupt.load();
    assert(corrupt.chargeStarts == 0 && corrupt.minMv == 0);
    std::puts("Security and battery observations: PASS");
}
