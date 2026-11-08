"""Compile HorizonDisplay.cpp against minimal LVGL/LGFX API doubles and exercise wake input.

This is a host contract test, NOT a firmware build or a CrowPanel touch test.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
CONFIG = r'''#pragma once
#include <cstdint>
namespace lgfx { struct touch_point_t { int x = 40, y = 70, id = 0, size = 0; }; }
bool horizonIsPortrait(void);
class LGFX_ELECROW_ADV_35 {
public:
    uint8_t count = 0;
    lgfx::touch_point_t points[5];
    uint8_t getTouch(lgfx::touch_point_t *out, uint8_t limit) {
        for (uint8_t i = 0; i < count && i < limit; ++i) out[i] = points[i];
        return count < limit ? count : limit;
    }
    void setRotation(int) {}
};
'''
DRIVER = r'''#pragma once
#include <cstdint>
using lv_coord_t = int;
struct lv_point_t { lv_coord_t x, y; };
struct lv_display_t {};
struct lv_indev_t {};
struct lv_indev_data_t { int state = 0; lv_point_t point = {0, 0}; };
constexpr int LV_INDEV_STATE_RELEASED = 0, LV_INDEV_STATE_PRESSED = 1;
using read_cb_t = void (*)(lv_indev_t *, lv_indev_data_t *);
extern read_cb_t read_callback;
extern void *read_user_data;
extern uint32_t tick_now, inactive_ms;
extern int activity_count;
inline void lv_indev_set_user_data(lv_indev_t *, void *data) { read_user_data = data; }
inline void *lv_indev_get_user_data(lv_indev_t *) { return read_user_data; }
inline void lv_indev_set_read_cb(lv_indev_t *, read_cb_t fn) { read_callback = fn; }
inline uint32_t lv_display_get_inactive_time(lv_display_t *) { return inactive_ms; }
inline uint32_t lv_tick_get() { return tick_now; }
inline void lv_display_trigger_activity(lv_display_t *) { inactive_ms = 0; ++activity_count; }
inline void lv_display_set_resolution(lv_display_t *, int, int) {}
struct ISpiLock { struct Guard { Guard() {} }; };
class DeviceGUI { public: virtual bool isScreenLocked() { return false; } };
class DisplayDriver {
protected:
    lv_indev_t *touch = nullptr;
    DeviceGUI *view = nullptr;
public:
    virtual ~DisplayDriver() = default;
    virtual void init(DeviceGUI *v) { view = v; touch = new lv_indev_t; }
    virtual void forceWakeup() {}
    lv_display_t *getDisplay() { static lv_display_t d; return &d; }
};
template<class T> class LGFXDriver : public DisplayDriver {
protected:
    T *tft = new T;
    uint32_t screenTimeout = 30000;
    bool powerSaving = false;
public:
    LGFXDriver(uint16_t, uint16_t) {}
    void init(DeviceGUI *v) override { DisplayDriver::init(v); }
    int forced = 0;
    void forceWakeup() override { ++forced; }
    T *panel() { return tft; }
    void setSleeping(bool value) { powerSaving = value; }
};
'''
HARNESS = r'''#include "graphics/driver/LGFXDriver.h"
#include "config.h"
#include "horizon/HorizonDisplay.h"
#include <cassert>
read_cb_t read_callback = nullptr;
void *read_user_data = nullptr;
uint32_t tick_now = 0, inactive_ms = 0;
int activity_count = 0;
bool horizonIsPortrait() { return false; }
int main() {
    DeviceGUI gui;
    auto *driver = static_cast<LGFXDriver<LGFX_ELECROW_ADV_35> *>(horizonCreateDisplayDriver(480, 320));
    driver->init(&gui);
    assert(read_callback);
    lv_indev_t touch;
    lv_indev_data_t data;
    auto read = [&](uint32_t now, uint8_t fingers) {
        tick_now = now;
        driver->panel()->count = fingers;
        read_callback(&touch, &data);
    };
    // Ordinary touches while awake still reach LVGL.
    read(1, 1);
    assert(data.state == LV_INDEV_STATE_PRESSED);
    read(5, 0);
    assert(data.state == LV_INDEV_STATE_RELEASED);
    // A multi-finger grip while dark must not trigger activity or menu buttons.
    inactive_ms = 30001;
    driver->setSleeping(true);
    read(100, 2);
    read(130, 1);
    read(160, 0);
    read(500, 0);
    assert(data.state == LV_INDEV_STATE_RELEASED && activity_count == 0 && driver->forced == 0);
    // One brief tap wakes only after stable release; no click leaks through.
    read(520, 1);
    assert(data.state == LV_INDEV_STATE_RELEASED);
    read(585, 0);
    assert(activity_count == 0);
    read(630, 0);
    assert(activity_count == 1 && driver->forced == 1 && data.state == LV_INDEV_STATE_RELEASED);
    driver->setSleeping(false);
    read(660, 1); // unexpected still-held finger after waking must stay ignored
    assert(data.state == LV_INDEV_STATE_RELEASED);
    read(680, 0);
    read(710, 1); // now a new intentional press works
    assert(data.state == LV_INDEV_STATE_PRESSED);
    read(720, 0);
    // A tap during gradual dimming restores activity without leaving a stale forceWakeup.
    inactive_ms = 30001;
    read(800, 1);
    read(860, 0);
    read(910, 0);
    assert(activity_count == 2 && driver->forced == 1);
}
'''

class WakeDriverHost(unittest.TestCase):
    def test_sleeping_driver_never_forwards_grab(self):
        cxx = shutil.which("g++")
        if not cxx:
            self.skipTest("g++ is not installed")
        with tempfile.TemporaryDirectory(prefix="hz-wake-driver-") as directory:
            tmp = Path(directory)
            (tmp / "graphics" / "driver").mkdir(parents=True)
            (tmp / "graphics" / "driver" / "LGFXDriver.h").write_text(DRIVER)
            (tmp / "config.h").write_text(CONFIG)
            (tmp / "main.cpp").write_text(HARNESS)
            result = subprocess.run([cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(tmp),
                                     "-I", str(ROOT / "src"), str(ROOT / "src/horizon/HorizonDisplay.cpp"),
                                     str(tmp / "main.cpp"), "-o", str(tmp / "test")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            run = subprocess.run([str(tmp / "test")], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)

if __name__ == "__main__":
    unittest.main()
