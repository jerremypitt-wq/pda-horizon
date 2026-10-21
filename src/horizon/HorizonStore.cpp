// ПДА «Горизонт» — хранилище сообщений.
#ifdef HORIZON_UI

#include "horizon/HorizonStore.h"
#include <algorithm>
#include <string.h>

HorizonConvo &HorizonStore::channelConvo(uint8_t ch)
{
    if (ch >= kMaxChannels)
        ch = 0;
    HorizonConvo &c = chanConvos[ch];
    c.isChannel = true;
    c.ch = ch;
    return c;
}

const HorizonConvo *HorizonStore::findChannelConvo(uint8_t ch) const
{
    if (ch >= kMaxChannels)
        return nullptr;
    return &chanConvos[ch];
}

HorizonConvo &HorizonStore::dmConvo(uint32_t peer)
{
    for (auto &c : dms) {
        if (!c.isChannel && c.peer == peer)
            return c;
    }
    HorizonConvo c;
    c.isChannel = false;
    c.peer = peer;
    dms.push_back(c);
    // Чистим старые прочитанные лички, чтобы не раздувать ОЗУ
    while (dms.size() > kMaxDmConvos) {
        size_t victim = dms.size();
        uint32_t oldest = 0xFFFFFFFFu;
        for (size_t i = 0; i < dms.size(); i++) {
            if (dms[i].peer != peer && dms[i].unread == 0 && dms[i].lastActivity < oldest) {
                oldest = dms[i].lastActivity;
                victim = i;
            }
        }
        if (victim >= dms.size())
            break; // все непрочитаны — оставляем
        dms.erase(dms.begin() + victim);
    }
    for (auto &cc : dms) {
        if (!cc.isChannel && cc.peer == peer)
            return cc;
    }
    return dms.back();
}

const HorizonConvo *HorizonStore::findDmConvo(uint32_t peer) const
{
    for (auto &c : dms) {
        if (!c.isChannel && c.peer == peer)
            return &c;
    }
    return nullptr;
}

HorizonConvo &HorizonStore::addMessage(uint32_t from, uint32_t to, uint8_t ch, uint32_t time, const char *text,
                                       bool countUnread)
{
    bool isBroadcast = (to == 0xFFFFFFFFu);
    HorizonConvo *c = nullptr;
    if (isBroadcast) {
        c = &channelConvo(ch);
    } else {
        uint32_t peer = (from == myNode) ? to : from;
        c = &dmConvo(peer);
    }
    HorizonMessage m;
    m.from = from;
    m.to = to;
    m.ch = ch;
    m.time = time;
    m.text = text ? text : "";
    m.outgoing = (from == myNode);
    c->messages.push_back(m);
    while (c->messages.size() > kMaxPerConvo)
        c->messages.erase(c->messages.begin());
    if (time > c->lastActivity)
        c->lastActivity = time;
    if (countUnread && !m.outgoing)
        c->unread++;
    return *c;
}

std::vector<const HorizonConvo *> HorizonStore::dmList(void) const
{
    std::vector<const HorizonConvo *> out;
    for (auto &c : dms) {
        if (!c.messages.empty())
            out.push_back(&c);
    }
    std::sort(out.begin(), out.end(),
              [](const HorizonConvo *a, const HorizonConvo *b) { return a->lastActivity > b->lastActivity; });
    return out;
}

void HorizonStore::upsertDossier(uint32_t node, const char *callsign, uint8_t rank, uint8_t group, uint32_t now)
{
    if (!node || !callsign || !*callsign)
        return;
    HorizonDossierInfo &d = dossierDir[node];
    strncpy(d.callsign, callsign, sizeof(d.callsign) - 1);
    d.callsign[sizeof(d.callsign) - 1] = '\0';
    // Не разрывать UTF-8 символ на границе буфера.
    size_t n = strlen(d.callsign);
    while (n > 0 && (((unsigned char)d.callsign[n - 1] & 0xC0) == 0x80))
        n--;
    if (n > 0 && ((unsigned char)d.callsign[n - 1] & 0x80))
        n--;
    d.callsign[n] = '\0';
    d.rank = rank;
    d.group = group;
    d.updated = now;
    // Не раздувать ОЗУ: вытеснить самую старую запись.
    while (dossierDir.size() > kMaxDossierDir) {
        auto victim = dossierDir.begin();
        for (auto it = dossierDir.begin(); it != dossierDir.end(); ++it) {
            if (it->second.updated < victim->second.updated)
                victim = it;
        }
        if (victim->first == node)
            break;
        dossierDir.erase(victim);
    }
}

const HorizonDossierInfo *HorizonStore::findDossier(uint32_t node) const
{
    auto it = dossierDir.find(node);
    return it != dossierDir.end() ? &it->second : nullptr;
}

void HorizonStore::markRead(HorizonConvo &c)
{
    c.unread = 0;
}

void HorizonStore::clearConvo(HorizonConvo &c)
{
    c.messages.clear();
    c.unread = 0;
}

void HorizonStore::clearAllDms(void)
{
    dms.clear();
}

void HorizonStore::clearAll(void)
{
    dms.clear();
    for (uint8_t i = 0; i < kMaxChannels; i++) {
        chanConvos[i].messages.clear();
        chanConvos[i].unread = 0;
    }
}

uint32_t HorizonStore::unreadTotal(void) const
{
    return unreadChannels() + unreadDms();
}

uint32_t HorizonStore::unreadChannels(void) const
{
    uint32_t n = 0;
    for (uint8_t i = 0; i < kMaxChannels; i++)
        n += chanConvos[i].unread;
    return n;
}

uint32_t HorizonStore::unreadDms(void) const
{
    uint32_t n = 0;
    for (auto &c : dms)
        n += c.unread;
    return n;
}

#endif // HORIZON_UI
