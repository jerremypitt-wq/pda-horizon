"""Compile the real HorizonMap store with a host-only FS to check copy persistence."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
FS_STUB = r'''#pragma once
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
class File {
    std::shared_ptr<std::string> contents;
    size_t pos = 0;
public:
    File() = default;
    explicit File(std::shared_ptr<std::string> str) : contents(str) {}
    explicit operator bool() const { return !!contents; }
    size_t available() const { return contents && pos < contents->size(); }
    size_t readBytesUntil(char delimiter, char *out, size_t maxLen) {
        size_t n = 0;
        while (available() && (*contents)[pos] != delimiter && n < maxLen)
            out[n++] = (*contents)[pos++];
        if (available() && (*contents)[pos] == delimiter) ++pos;
        return n;
    }
    size_t printf(const char *fmt, ...) {
        char buf[2048];
        va_list args;
        va_start(args, fmt);
        int len = vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        if (contents && len > 0 && len < (int)sizeof(buf)) contents->append(buf, len);
        return len > 0 ? len : 0;
    }
    void close() {}
};
namespace fs {
class FS {
public:
    std::map<std::string, std::shared_ptr<std::string>> files;
    bool mkdir(const char *) { return true; }
    File open(const char *path, const char *mode) {
        if (mode[0] == 'w') files[path] = std::make_shared<std::string>();
        auto it = files.find(path);
        return it == files.end() ? File() : File(it->second);
    }
    void seed(const char *path, const char *data) {
        files[path] = std::make_shared<std::string>(data);
    }
};
}
extern fs::FS PortduinoFS;
'''
HARNESS = r'''#include "PortduinoFS.h"
#include "horizon/HorizonMap.h"
#include <cassert>
#include <cstring>
fs::FS PortduinoFS;
int main() {
    HzMapStore empty;
    empty.loadAll(); // factory image has no game map or baked points
    assert(empty.baked().empty() && empty.my().empty() && empty.net().empty());
    assert(empty.tabs().size() == 1 && empty.tabs().front().id == 0);
    assert(!empty.resetToBaked()); // no map project yet: never erase user points
    PortduinoFS.seed("/hzmap/pois.csv", "B001|0|Start|100|200|Maker|base\n");
    PortduinoFS.seed("/hzmap/del.csv", "B001\n");
    PortduinoFS.seed("/hzmap/map.rgb565", "image data");
    PortduinoFS.seed("/hzmap/my.csv", "200-1|0|Old|100|200|Me|a|0|1\n"
                                      "200-2|0|Old DM|100|200|Me|b|1|1|100\n"
                                      "200-3|0|Copy|100|200|Xren|c|0|1|0|100-7\n");
    PortduinoFS.seed("/hzmap/net.csv", "100-7|0|Public|100|200|Xren|d|0\n");
    HzMapStore store;
    store.loadAll();
    assert(store.my().size() == 3);
    assert(store.findMut("100-7") == nullptr); // public point not editable
    assert(store.findMut("200-3") != nullptr);
    assert(store.findBySource("100-7") && store.findBySource("100-7")->sender == "Xren");
    assert(store.find("200-2")->dmPeer == 100);
    assert(store.removeEverywhere("100-7")); // author deletes public original
    assert(store.find("200-3") && store.findBySource("100-7"));
    store.saveMy();
    HzMapStore reloaded;
    reloaded.loadAll();
    assert(reloaded.findBySource("100-7") && !strcmp(reloaded.findBySource("100-7")->id, "200-3"));
    assert(reloaded.removeEverywhere("200-3"));
    assert(reloaded.directDismissed("100-7"));
    HzMapStore again;
    again.loadAll();
    assert(!again.findBySource("100-7") && again.directDismissed("100-7"));
    assert(!again.find("B001")); // tombstone hides the initial point before reset
    assert(again.resetToBaked());
    assert(again.find("B001") && again.find("B001")->name == "Start");
    assert(again.my().empty() && again.net().empty() && again.privCount() == 0);
    assert(!again.directDismissed("100-7") && !again.isTombstoned("B001"));
    for (const char *path : {"/hzmap/my.csv", "/hzmap/net.csv", "/hzmap/del.csv", "/hzmap/dm_del.csv"})
        assert(PortduinoFS.files.count(path) && PortduinoFS.files.at(path)->empty());
    assert(*PortduinoFS.files.at("/hzmap/map.rgb565") == "image data");
    HzMapStore afterReset;
    afterReset.loadAll(); // reboot must preserve the reset state
    assert(afterReset.find("B001") && afterReset.my().empty() && afterReset.net().empty());
    PortduinoFS.seed("/hzmap/tabs.csv", "12|Второй этаж\n0|Улица\n");
    PortduinoFS.seed("/hzmap/pois.csv", "B001|0|Start|100|200|Maker|base|0\n"
                                      "F12-01|1|Hall|500|500|Maker||12\n");
    PortduinoFS.seed("/hzmap/my.csv", "200-4|0|My floor|100|200|Me||0|1|0||12\n");
    PortduinoFS.seed("/hzmap/net.csv", "100-9|0|Net floor|100|200|Peer||0|12\n");
    HzMapStore floors;
    floors.loadAll();
    assert(floors.tabs().size() == 2 && floors.tabs().front().id == 12);
    assert(floors.hasTab(0) && floors.hasTab(12) && !floors.hasTab(1));
    assert(floors.find("F12-01") && floors.find("F12-01")->tab == 12);
    assert(floors.find("200-4") && floors.find("200-4")->tab == 12);
    assert(floors.find("100-9") && floors.find("100-9")->tab == 12);
    floors.saveMy();
    floors.saveNet();
    HzMapStore rebooted;
    rebooted.loadAll();
    assert(rebooted.find("200-4")->tab == 12 && rebooted.find("100-9")->tab == 12);
    PortduinoFS.files.erase("/hzmap/pois.csv");
    PortduinoFS.seed("/hzmap/my.csv", "200-1|0|Old|100|200|Me|a|0|1\n");
    assert(!afterReset.resetToBaked()); // without initial POIs do not erase user points
    assert(!PortduinoFS.files.at("/hzmap/my.csv")->empty());
    char id[28] = {};
    assert(hzPoiLinkId("HZ:MAPLINK:100-7|Точка", id) && !strcmp(id, "100-7"));
    assert(!hzPoiLinkId("HZ:MAPLINK:100-7|Точка|extra", id));
    assert(!hzPoiLinkId("HZ:MAPLINK:bad/id|Точка", id));
}
'''

class MapStoreRuntime(unittest.TestCase):
    def test_copy_persistence_and_link(self):
        cxx = shutil.which("g++")
        if not cxx:
            self.skipTest("g++ is not installed")
        with tempfile.TemporaryDirectory(prefix="hz-map-store-") as td:
            temp = Path(td)
            (temp / "PortduinoFS.h").write_text(FS_STUB)
            (temp / "horizon_target.h").write_text("#define HORIZON_UI\n#define ARCH_PORTDUINO\n")
            (temp / "harness.cpp").write_text(HARNESS)
            (temp / "util").mkdir()
            (temp / "util" / "ILog.h").write_text("#pragma once\n#define ILOG_INFO(...) ((void)0)\n#define ILOG_ERROR(...) ((void)0)\n")
            result = subprocess.run([cxx, "-std=c++17", "-DHORIZON_UI", "-DARCH_PORTDUINO", "-I", str(temp), "-I", str(ROOT / "src"),
                                     str(ROOT / "src/horizon/HorizonMap.cpp"), str(temp / "harness.cpp"),
                                     "-o", str(temp / "map_store_test")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            run = subprocess.run([str(temp / "map_store_test")], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)

if __name__ == "__main__":
    unittest.main()
