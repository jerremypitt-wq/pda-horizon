// Host contract for MAX17048 on the GT911 LovyanGFX I2C port (not a hardware test).
// Build: g++ -std=c++17 -Wall -Wextra -Werror -Isrc -Itools/tests/stubs
// src/horizon/HorizonBattery.cpp tools/tests/test_max17048.cpp -o /tmp/test_max17048
#include "horizon/HorizonBattery.h"
#include "LovyanGFX.hpp"
#include <cassert>
#include <cstdio>
#include <deque>

namespace {
struct Step { uint8_t reg; uint16_t word; bool ok; };
std::deque<Step> steps;
bool portReady = true;
bool pinMismatch = false;
bool busFault = false;
unsigned transactions = 0;

void reg(uint8_t addr, uint16_t word, bool ok = true) { steps.push_back({addr, word, ok}); }
void good(uint16_t vcell = 0xBC00, uint16_t soc = 0x5080, uint16_t crate = 4)
{
    reg(0x08, 0x0011); // VERSION
    reg(0x02, vcell);
    reg(0x04, soc);
    reg(0x16, crate);
}
void drained(void) { assert(steps.empty()); }
} // namespace

namespace lgfx { namespace i2c {
PinResult getPinSDA(int port)
{
    assert(port == 1); // same port as the GT911 touch driver, never Wire0/J15
    return {portReady, portReady ? (pinMismatch ? 17 : 15) : -1};
}
PinResult getPinSCL(int port)
{
    assert(port == 1);
    return {portReady, portReady ? 16 : -1};
}
TransactionResult transactionWriteRead(int port, int addr, const uint8_t *data,
                                        uint8_t writeLen, uint8_t *out,
                                        size_t readLen, uint32_t freq)
{
    assert(port == 1 && addr == 0x36 && writeLen == 1 && readLen == 2);
    assert(freq == 100000); // frequency is set per transaction, touch stays at 400 kHz
    ++transactions;
    if (busFault) return {false}; // simulated NACK / stuck bus; no infinite retry
    assert(!steps.empty());
    Step step = steps.front();
    steps.pop_front();
    assert(data[0] == step.reg);
    if (!step.ok) return {false};
    out[0] = (uint8_t)(step.word >> 8);
    out[1] = (uint8_t)step.word;
    return {true};
}
}} // namespace lgfx::i2c

int main(void)
{
    HzBatteryState st;
    portReady = false; // display/GT911 not initialized: do not reinit its port
    assert(!hzBatteryPoll(st) && transactions == 0);
    assert(!st.found && st.percent == -1);
    portReady = true;
    pinMismatch = true; // another device changed the port pins: fail closed
    assert(!hzBatteryPoll(st) && transactions == 0);
    pinMismatch = false;

    good();
    assert(hzBatteryPoll(st));
    assert(st.found && st.battMv == 3760 && st.percent == 81 && st.charging);
    drained();
    good(0x7D00, 0, 0); // valid 2500 mV, 0%
    assert(hzBatteryPoll(st) && st.found && st.percent == 0 && !st.charging);
    drained();
    good(0xE100, 0x6400, (uint16_t)-10); // valid 4500 mV, discharge
    assert(hzBatteryPoll(st) && st.battMv == 4500 && st.percent == 100 && !st.charging);
    drained();
    good(0xBC00, 0x6600, 3); // clamp >100%, ignore CRATE noise
    assert(hzBatteryPoll(st) && st.percent == 100 && !st.charging);
    drained();

    reg(0x08, 0x0012); reg(0x02, 0xBC00); reg(0x04, 0x3200);
    reg(0x16, 0, false); // optional CRATE failure does not hide SOC/voltage
    assert(hzBatteryPoll(st) && st.found && st.percent == 50 && !st.rateValid);
    drained();
    reg(0x08, 0, false); // absent sensor: clear previous reading
    assert(!hzBatteryPoll(st));
    assert(!st.found && st.percent == -1 && st.battMv == -1 && !st.charging);
    drained();
    good(); // sensor returns on next poll
    assert(hzBatteryPoll(st) && st.found);
    drained();
    reg(0x08, 0xFFFF); // invalid/floating VERSION
    assert(!hzBatteryPoll(st) && !st.found && st.percent == -1);
    drained();
    reg(0x08, 0x0011); reg(0x02, 0x6000); reg(0x04, 0x6400);
    assert(!hzBatteryPoll(st) && !st.found); // voltage out of range
    drained();
    reg(0x08, 0x0011); reg(0x02, 0xBC00); reg(0x04, 0xFFFF);
    assert(!hzBatteryPoll(st) && !st.found); // invalid SOC
    drained();
    reg(0x08, 0x0011); reg(0x02, 0, false);
    assert(!hzBatteryPoll(st) && st.percent == -1); // NACK mid-poll
    drained();
    busFault = true;
    unsigned before = transactions;
    assert(!hzBatteryPoll(st) && !st.found && transactions == before + 1);
    busFault = false;
    good();
    assert(hzBatteryPoll(st) && st.found); // bus recovers on next attempt
    drained();
    std::puts("MAX17048 shared-I2C driver: PASS");
}
