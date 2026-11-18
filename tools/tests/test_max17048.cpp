// Host test for the real bit-banged MAX17048 driver.
// Build: g++ -std=c++17 -Wall -Wextra -Werror -Isrc -Itools/tests/stubs
// src/horizon/HorizonBattery.cpp tools/tests/test_max17048.cpp -o /tmp/test_max17048
#include "horizon/HorizonBattery.h"
#include "Arduino.h"
#include <cassert>
#include <cstdio>
#include <deque>

namespace {
std::deque<int> sdaReads;
bool sclStuck = false;
bool sdaStuck = false;
unsigned releaseAfterClocks = 0; // имитация slave, ожидающего такты после сбоя ESP
unsigned recoveryClocks = 0;
unsigned delays = 0;
uint8_t sdaMode = INPUT, sclMode = INPUT;

void reg(uint16_t word, bool ack = true)
{
    sdaReads.push_back(HIGH);           // bus idle at START
    sdaReads.push_back(ack ? LOW : HIGH); // address+write ACK/NACK
    if (!ack)
        return;
    sdaReads.push_back(LOW);            // register ACK
    sdaReads.push_back(HIGH);           // bus idle at repeated START
    sdaReads.push_back(LOW);            // address+read ACK
    for (int bit = 15; bit >= 0; --bit)
        sdaReads.push_back((word >> bit) & 1);
}

void good(uint16_t vcell = 0xBC00, uint16_t soc = 0x5080, uint16_t crate = 4)
{
    reg(0x0011); // VERSION
    reg(vcell);
    reg(soc);
    reg(crate);
}

void drained(void)
{
    assert(sdaReads.empty());
    assert(sdaMode == INPUT && sclMode == INPUT);
    assert(delays < 30000); // driver must not hang on bus faults
    delays = 0;
}
} // namespace

void pinMode(uint8_t pin, uint8_t mode)
{
    assert(mode == INPUT || mode == OUTPUT);
    assert(pin == 17 || pin == 18); // no accidental access to GT911
    if (pin == 18 && mode == INPUT && sclMode == OUTPUT && recoveryClocks < releaseAfterClocks)
        ++recoveryClocks;
    (pin == 17 ? sdaMode : sclMode) = mode;
}
void digitalWrite(uint8_t pin, uint8_t value)
{
    assert((pin == 17 || pin == 18) && value == LOW); // open-drain only
}
int digitalRead(uint8_t pin)
{
    assert(pin == 17 || pin == 18);
    if (pin == 18)
        return sclStuck ? LOW : HIGH;
    if (sdaStuck || (releaseAfterClocks && recoveryClocks < releaseAfterClocks))
        return LOW;
    assert(!sdaReads.empty());
    int value = sdaReads.front();
    sdaReads.pop_front();
    return value;
}
void delayMicroseconds(unsigned us)
{
    assert(us == 5);
    ++delays;
}

int main(void)
{
    HzBatteryState st;
    good();
    assert(hzBatteryPoll(st));
    assert(st.found && st.battMv == 3760 && st.percent == 81 && st.charging);
    drained();

    // 0% and voltage boundaries are real values, not 'sensor missing'.
    good(0x7D00, 0, 0); // 2500 mV, 0%
    assert(hzBatteryPoll(st));
    assert(st.found && st.battMv == 2500 && st.percent == 0 && !st.charging);
    drained();
    good(0xE100, 0x6400, (uint16_t)-10); // 4500mV, 100%, discharge
    assert(hzBatteryPoll(st));
    assert(st.battMv == 4500 && st.percent == 100 && !st.charging);
    drained();
    good(0xBC00, 0x6600, 3); // >100% clamps; CRATE noise ignored
    assert(hzBatteryPoll(st));
    assert(st.percent == 100 && !st.charging);
    drained();

    // The crate register is optional, unlike VERSION/VCELL/SOC.
    reg(0x0012); reg(0xBC00); reg(0x3200); reg(0, false);
    assert(hzBatteryPoll(st));
    assert(st.found && st.percent == 50 && !st.charging);
    drained();

    reg(0, false); // absent chip: NACK, invalidate previous reading
    assert(!hzBatteryPoll(st));
    assert(!st.found && st.percent == -1 && st.battMv == -1 && !st.charging);
    drained();
    good(); // recovery at next poll
    assert(hzBatteryPoll(st));
    drained();
    reg(0xFFFF); // floating/invalid VERSION
    assert(!hzBatteryPoll(st) && !st.found && st.percent == -1);
    drained();
    reg(0x0011); reg(0x6000); reg(0x6400); // invalid low VCELL
    assert(!hzBatteryPoll(st) && !st.found);
    drained();
    reg(0x0011); reg(0xBC00); reg(0xFFFF); // invalid SOC
    assert(!hzBatteryPoll(st) && !st.found);
    drained();
    reg(0x0011); reg(0, false); // NACK during VCELL
    assert(!hzBatteryPoll(st) && st.percent == -1);
    drained();
    // Сбой посреди чтения: MAX отпустит SDA только после трёх тактов.
    // Первый START ещё не читает очередь; дополнительный HIGH — проба
    // освобождения, затем обычный START и четыре регистра.
    recoveryClocks = 0;
    releaseAfterClocks = 3;
    sdaReads.push_back(HIGH);
    good();
    assert(hzBatteryPoll(st) && st.found);
    assert(recoveryClocks == 3);
    releaseAfterClocks = 0;
    drained();
    sdaStuck = true; // короткое замыкание: 9 тактов, STOP, не зависать
    assert(!hzBatteryPoll(st) && !st.found);
    sdaStuck = false;
    drained();
    sclStuck = true;
    assert(!hzBatteryPoll(st) && !st.found);
    assert(delays <= 430); // две ограниченные попытки SCL
    sclStuck = false;
    drained();
    std::puts("MAX17048 driver: PASS");
}
