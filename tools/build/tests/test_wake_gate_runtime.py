"""Host C++17 state-machine test: only an unambiguous released single tap wakes."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
SOURCE = r'''#include "horizon/HorizonWakeGate.h"
#include <cassert>
#include <cstdint>
int main() {
    HorizonWakeGate g;
    auto touch = [&](uint32_t t, uint8_t count, int x = 40, int y = 70) {
        return g.sample(t, count, x, y);
    };
    // A grab: multiple fingers stay down, then peel away one by one.
    assert(!touch(100, 2));
    assert(!touch(300, 3));
    assert(!touch(400, 1));
    assert(!touch(420, 0));
    assert(!touch(500, 1)); // quiet period, even a new finger cannot wake
    assert(!touch(501, 0));
    assert(!touch(802, 0));
    assert(!touch(810, 1));
    assert(!touch(870, 1, 46, 73));
    assert(!touch(880, 0)); // a release must stay stable for two read cycles
    assert(!touch(900, 0));
    assert(touch(920, 0));
    assert(!touch(950, 0)); // only one wake per sleep session
    // A second finger added before releasing invalidates the whole gesture.
    g.reset();
    assert(!touch(1000, 1));
    assert(!touch(1020, 2));
    assert(!touch(1050, 1));
    assert(!touch(1100, 0));
    assert(!touch(1410, 0));
    // A late second touch during release stabilization is still a grab.
    assert(!touch(1500, 1));
    assert(!touch(1550, 0));
    assert(!touch(1570, 1));
    assert(!touch(1590, 0));
    assert(!touch(1930, 0));
    // Long press, drag, and noise are not wake gestures.
    assert(!touch(2000, 1));
    assert(!touch(2300, 1));
    assert(!touch(2310, 0));
    assert(!touch(2620, 0));
    assert(!touch(2700, 1));
    assert(!touch(2720, 1, 80, 70));
    assert(!touch(2750, 0));
    assert(!touch(3060, 0));
    assert(!touch(3100, 1));
    assert(!touch(3110, 0)); // shorter than 35 ms
    assert(!touch(3150, 0));
    // millis() wraparound and another valid tap.
    g.reset();
    assert(!touch(UINT32_MAX - 50, 1));
    assert(!touch(12, 0));
    assert(touch(55, 0));
}
'''

class WakeGateRuntime(unittest.TestCase):
    def test_single_tap_vs_grab_and_noise(self):
        cxx = shutil.which("g++")
        if not cxx:
            self.skipTest("g++ is not installed")
        with tempfile.TemporaryDirectory(prefix="hz-wake-gate-") as td:
            path = Path(td)
            (path / "main.cpp").write_text(SOURCE)
            compile_result = subprocess.run([cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I",
                                             str(ROOT / "src"), str(path / "main.cpp"), "-o", str(path / "test")],
                                            capture_output=True, text=True)
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            result = subprocess.run([str(path / "test")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

if __name__ == "__main__":
    unittest.main()
