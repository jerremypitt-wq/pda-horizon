// Build: g++ -std=c++17 -Wall -Wextra -Werror -Isrc -Itools/tests/stubs
// src/horizon/HorizonConfig.cpp src/horizon/HorizonStrings.cpp tools/tests/test_security_config.cpp -o /tmp/test_security_config
#include "horizon/HorizonConfig.h"
#include "LittleFS.h"
#include <cassert>
#include <cstdio>
#include <cstring>

fs::FS LittleFS;

int main()
{
    HorizonConfig &cfg = HorizonConfig::instance();
    cfg.resetToDefaults();
    assert(cfg.checkPin("0000") && !cfg.lockEnabled && !cfg.userPin[0]);
    cfg.setPin("12345"); // new master PIN must be 4 digits
    assert(cfg.checkPin("0000"));
    cfg.setPin("0183");
    cfg.setUserPin("9021");
    cfg.lockEnabled = true;
    cfg.lockTimeoutSec = 120;
    cfg.save();
    assert(LittleFS.files["/horizon.cfg"].find("userPin=9021") != std::string::npos);
    cfg.resetToDefaults();
    cfg.load();
    assert(cfg.lockEnabled && cfg.lockTimeoutSec == 120);
    assert(cfg.checkPin("0183") && cfg.checkUnlockPin("9021") && cfg.checkUnlockPin("0183"));
    assert(!cfg.checkPin("9021") && !cfg.checkUnlockPin("9000"));

    LittleFS.files["/horizon.cfg"] = "masterPin=123456\nuserPin=99X1\nlockEnabled=1\nlockTimeoutSec=123\n";
    cfg.load();
    assert(cfg.checkPin("123456") && cfg.userPin[0] == '\0'); // old PIN migration
    assert(cfg.lockTimeoutSec == 300 && cfg.checkUnlockPin("123456"));
    cfg.setPin("4321");
    assert(cfg.checkPin("4321") && !cfg.checkPin("123456"));
    cfg.resetToDefaults();
    assert(cfg.checkPin("0000") && !cfg.lockEnabled && !cfg.userPin[0]);

    // Битая длинная строка не должна превращать её хвост в новую настройку.
    LittleFS.files["/horizon.cfg"] = "silent=1\n" + std::string(159, 'X') +
        "silent=0\nbrightness=99999999999999999999\nsoundVolume=bad\n";
    cfg.load();
    assert(cfg.silent && cfg.brightness == 255 && cfg.soundVolume == 1);
    LittleFS.files["/horizon.cfg"] = "brightness=12oops\n";
    cfg.load();
    assert(cfg.brightness == 180); // значение с хвостом отвергнуто
    std::puts("Security config persistence: PASS");
}
