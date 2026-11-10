// Host-only test: g++ -std=c++17 -Isrc tools/tests/test_poi_presentation.cpp -o /tmp/test_poi_presentation && /tmp/test_poi_presentation
#include "horizon/HorizonPoiPresentation.h"
#include <assert.h>
#include <string.h>

int main()
{
    HzPoi p;
    strcpy(p.id, "B001");
    p.sender = "Мастер";
    p.name = "Склад";
    p.ch = 0xFF; // исходные точки не имеют канала
    assert(hzPoiAccess(p, 0, 1) == HzPoiAccess::General);
    assert(!hzPoiIsPrivateOnMap(p));
    p.ch = 0;
    assert(hzPoiAccess(p, 0, 1) == HzPoiAccess::General);
    p.ch = 1;
    assert(hzPoiAccess(p, 0, 1) == HzPoiAccess::Group);
    assert(hzPoiAccess(p, 0, 0) == HzPoiAccess::General); // одинаковые каналы неразличимы
    p.shared = false;
    assert(hzPoiAccess(p, 0, 1) == HzPoiAccess::Personal);
    assert(hzPoiIsPrivateOnMap(p));
    p.shared = true;
    p.dmPeer = 123;
    assert(hzPoiAccess(p, 0, 1) == HzPoiAccess::Personal); // отправлена лично
    assert(hzPoiIsPrivateOnMap(p));
    strcpy(p.sourceId, "123-4");
    assert(hzPoiAccess(p, 0, 1) == HzPoiAccess::Received); // независимая копия
    assert(hzPoiIsPrivateOnMap(p));
    p.dmPeer = 0;
    assert(hzPoiIsPrivateOnMap(p)); // исходный ID достаточен и без dmPeer
    p.shared = false;
    assert(hzPoiAccess(p, 0, 1) == HzPoiAccess::Received); // даже после правки

    HzPoi copy = p;
    assert(hzPoiSameContent(p, copy)); // ретрансляция не должна пищать
    copy.name = "Склад-2";
    assert(!hzPoiSameContent(p, copy)); // обновление точки — должно
    copy = p;
    copy.ch = 2;
    assert(!hzPoiSameContent(p, copy));
    copy = p;
    copy.comment = "Новое описание";
    assert(!hzPoiSameContent(p, copy));
    copy = p;
    strcpy(copy.sourceId, "123-5");
    assert(!hzPoiSameContent(p, copy));
    return 0;
}
