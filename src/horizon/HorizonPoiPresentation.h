#pragma once
#include "horizon/HorizonMap.h"
#include <string.h>

#ifdef HORIZON_UI
// Подпись доступа в карточке. sourceId сохраняется у лично полученной копии
// независимо от дальнейших правок; dmPeer — адресат своей отправленной точки.
enum class HzPoiAccess : uint8_t { General, Group, Personal, Received };

inline bool hzPoiIsPrivateOnMap(const HzPoi &p)
{
    return !p.shared || p.dmPeer || p.sourceId[0];
}

inline HzPoiAccess hzPoiAccess(const HzPoi &p, uint8_t generalCh, uint8_t groupCh)
{
    if (p.sourceId[0])
        return HzPoiAccess::Received;
    if (hzPoiIsPrivateOnMap(p))
        return HzPoiAccess::Personal;
    // Когда оба назначения совпадают, по одному номеру канала восстановить
    // исходный выбор нельзя: общий канал — безопасная подпись по умолчанию.
    if (groupCh != generalCh && p.ch == groupCh)
        return HzPoiAccess::Group;
    return HzPoiAccess::General; // в том числе стартовые точки (ch == 0xFF)
}

// Повтор того же маяка может полностью собраться заново. Он не должен
// перезапускать мелодию; содержательная правка точки, наоборот, уведомляет.
inline bool hzPoiSameContent(const HzPoi &a, const HzPoi &b)
{
    return !strcmp(a.id, b.id) && a.type == b.type && a.name == b.name && a.x == b.x && a.y == b.y &&
           a.sender == b.sender && a.comment == b.comment && a.shared == b.shared && a.ch == b.ch &&
           a.dmPeer == b.dmPeer && !strcmp(a.sourceId, b.sourceId);
}
#endif // HORIZON_UI
