// Offline contacts, expiry and persistence contract (host-only, no radio/display).
#include "horizon/HorizonContacts.h"
#include "LittleFS.h"
#include <cassert>
#include <cstdio>
#include <set>

fs::FS LittleFS;
static bool favorite(uint32_t id, void *ctx)
{
    return static_cast<std::set<uint32_t> *>(ctx)->count(id) != 0;
}

int main()
{
    constexpr uint32_t t = 1800000000u;
    HzContacts c;
    assert(c.heard(1, 0, "Первый", 1000, t));
    assert(c.heard(2, 0, "Второй", 1000, t));
    assert(c.heard(3, 0, nullptr, 1000, t)); // без NodeInfo тоже контакт
    assert(c.records().size() == 3 && c.online(3, 1000, t));
    assert(c.online(2, 1800000, t + 1799));
    assert(!c.online(2, 1802000, t + 1801)); // 30 минут, но контакт остаётся
    assert(c.find(2) && c.save());
    assert(!c.changed());
    HzContacts restarted;
    restarted.load();
    assert(restarted.records().size() == 3);
    assert(!restarted.online(3, 0, 0)); // до уточнения часов никого не объявлять онлайн
    uint32_t age = 0;
    assert(!restarted.age(3, 0, 0, age));
    std::set<uint32_t> favorites{1};
    assert(!restarted.expire(0, 0, favorite, &favorites)); // без времени не удалять
    assert(!restarted.expire(0, t + HzContacts::kKeepSec, favorite, &favorites));
    assert(restarted.expire(0, t + HzContacts::kKeepSec + 1, favorite, &favorites));
    assert(restarted.find(1) && !restarted.find(2) && !restarted.find(3));
    assert(restarted.save());
    HzContacts after;
    after.load();
    assert(after.records().size() == 1 && after.find(1));
    assert(!after.online(1, 0, t + HzContacts::kKeepSec + 1));
    assert(after.heard(3, 0, "Третий", 1000, t + HzContacts::kKeepSec + 2));
    assert(after.online(3, 1000, t + HzContacts::kKeepSec + 2));
    assert(!after.expire(2000, 2082758399u, favorite, &favorites)); // во время текущего сеанса не забывать живого
    HzContacts unknown;
    assert(unknown.heard(4, 0, nullptr, 1000, 0));
    assert(!unknown.expire(2000, 0, favorite, &favorites));
    assert(!unknown.expire(2000, t, favorite, &favorites));
    assert(unknown.find(4)->lastEpoch == t - 1);
    HzContacts corrected;
    corrected.heard(6, 0, "старые часы", 1000, t);
    corrected.heard(6, 0, nullptr, 2000, 0); // время этой загрузки пока не подтверждено
    assert(!corrected.expire(3000, t + HzContacts::kKeepSec + 20, favorite, &favorites));
    assert(corrected.find(6)->lastEpoch == t + HzContacts::kKeepSec + 19);
    assert(!unknown.remember(5, 0, "очень старый", t, t + HzContacts::kKeepSec + 10));
    LittleFS.files["/horizon.contacts"] = "HZC1 00000007 123 0 несовместимые часы\n"
                                          "HZC1 00000008 1800000000 0 правильные часы\n";
    HzContacts fromFile;
    fromFile.load();
    assert(!fromFile.find(7) && fromFile.find(8));
    std::puts("Contacts retention: PASS");
}
