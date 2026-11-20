#pragma once
// Minimal LovyanGFX I2C surface used by the MAX17048 host test.
#include <cstddef>
#include <cstdint>

namespace lgfx { namespace i2c {
struct PinResult {
    bool ok;
    int pin;
    bool has_value() const { return ok; }
    int value() const { return pin; }
};
struct TransactionResult {
    bool ok;
    bool has_value() const { return ok; }
};
PinResult getPinSDA(int port);
PinResult getPinSCL(int port);
TransactionResult transactionWriteRead(int port, int addr, const uint8_t *data,
                                        uint8_t writeLen, uint8_t *out,
                                        size_t readLen, uint32_t freq);
}} // namespace lgfx::i2c
