// ПДА «Горизонт» - хранилище сообщений.
#ifdef HORIZON_UI

#include "horizon/HorizonStore.h"
#include "horizon/HorizonStrings.h"
#include <algorithm>
#include <string.h>

#if defined(ARCH_PORTDUINO)
#include "PortduinoFS.h"
static fs::FS &msgFs = PortduinoFS;
#else
#include "LittleFS.h"
static fs::FS &msgFs = LittleFS;
#endif

namespace {
constexpr const char *kMsgPath = "/horizon.msg";
constexpr size_t kMsgCompactSize = 96 * 1024; // больше - пережимать файл

void putU16(uint8_t *b, uint16_t v)
{
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
}
void putU32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
}
uint16_t getU16(const uint8_t *b)
{
    return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}
uint32_t getU32(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

// Запись лога: 'M' + len u16 (байт дальше) + from u32 + to u32 + ch u8 +
// time u32 + status u8 + текст. Текст обрезан до 230 байт отправителем.
void writeRecord(File &f, const HorizonMessage &m)
{
    size_t tn = m.text.size();
    if (tn > 230)
        tn = 230;
    uint8_t hdr[17];
    hdr[0] = 'M';
    putU16(hdr + 1, (uint16_t)(14 + tn));
    putU32(hdr + 3, m.from);
    putU32(hdr + 7, m.to);
    hdr[11] = m.ch;
    putU32(hdr + 12, m.time);
    // pending в файл не пишем: после перезагрузки его не резолвнуть.
    hdr[16] = (m.sendStatus == kHzSendPending) ? kHzSendOk : m.sendStatus;
    f.write(hdr, sizeof(hdr));
    if (tn > 0)
        f.write((const uint8_t *)m.text.data(), tn);
}
} // namespace

void HorizonStore::setMyNode(uint32_t n)
{
    myNode = n;
    // История грузится раньше, чем известен свой номер: переразметить.
    for (uint8_t i = 0; i < kMaxChannels; i++) {
        for (auto &m : chanConvos[i].messages)
            m.outgoing = (m.from == n);
    }
    for (auto &c : dms) {
        for (auto &m : c.messages)
            m.outgoing = (m.from == n);
    }
}

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
            break; // все непрочитаны - оставляем
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
                                       bool countUnread, uint8_t sendStatus, uint32_t reqId, bool persist)
{
    bool isBroadcast = (to == 0xFFFFFFFFu);
    HorizonConvo *c = nullptr;
    if (isBroadcast) {
        c = &channelConvo(ch);
    } else {
        uint32_t peer = (from == myNode) ? to : from;
        c = &dmConvo(peer);
    }
    // Дедуп переигрываний: то же сообщение уже лежит последним (свой лог
    // грузится первым, следом - восстановление штатного /messages).
    // Живые добавления (persist) не дедуплицируем: повторный текст
    // в ту же секунду - нормальное сообщение.
    if (!persist && !c->messages.empty()) {
        const HorizonMessage &last = c->messages.back();
        if (last.from == from && last.to == to && last.time == time && last.text == (text ? text : ""))
            return *c;
    }
    HorizonMessage m;
    m.from = from;
    m.to = to;
    m.ch = ch;
    m.time = time;
    m.text = text ? text : "";
    m.outgoing = (from == myNode);
    m.sendStatus = sendStatus;
    m.reqId = reqId;
    c->messages.push_back(m);
    while (c->messages.size() > kMaxPerConvo)
        c->messages.erase(c->messages.begin());
    if (time > c->lastActivity)
        c->lastActivity = time;
    if (countUnread && !m.outgoing)
        c->unread++;
    if (persist)
        saveMessage(c->messages.back());
    return *c;
}

bool HorizonStore::markTextDelivered(uint32_t reqId, bool ok)
{
    if (reqId == 0)
        return false;
    auto sweep = [reqId, ok](HorizonConvo &c) {
        for (auto &m : c.messages) {
            if (m.outgoing && m.reqId == reqId && m.sendStatus == kHzSendPending) {
                m.sendStatus = ok ? kHzSendOk : kHzSendFailed;
                m.reqId = 0;
                return true;
            }
        }
        return false;
    };
    bool hit = false;
    for (uint8_t i = 0; i < kMaxChannels && !hit; i++)
        hit = sweep(chanConvos[i]);
    for (auto &c : dms) {
        if (hit)
            break;
        hit = sweep(c);
    }
    if (hit)
        rewriteMessages(); // статус тоже пережил бы перезагрузку
    return hit;
}

const HorizonMessage *HorizonStore::findTextByReq(uint32_t reqId) const
{
    if (reqId == 0)
        return nullptr;
    for (uint8_t i = 0; i < kMaxChannels; i++) {
        for (auto &m : chanConvos[i].messages) {
            if (m.reqId == reqId)
                return &m;
        }
    }
    for (auto &c : dms) {
        for (auto &m : c.messages) {
            if (m.reqId == reqId)
                return &m;
        }
    }
    return nullptr;
}

void HorizonStore::saveMessage(const HorizonMessage &m)
{
    File f = msgFs.open(kMsgPath, "a");
    if (!f)
        return;
    writeRecord(f, m);
    f.close();
}

void HorizonStore::loadMessages(void)
{
    File f = msgFs.open(kMsgPath, "r");
    if (!f)
        return;
    size_t fsize = f.size();
    bool corrupt = false;
    while (f.available() > 0) {
        uint8_t hdr[3];
        if (f.read(hdr, 3) != 3 || hdr[0] != 'M') {
            corrupt = true;
            break;
        }
        uint16_t len = getU16(hdr + 1);
        if (len < 14 || len > 244) {
            corrupt = true;
            break;
        }
        uint8_t rec[244];
        if (f.read(rec, len) != len) {
            corrupt = true;
            break;
        }
        uint8_t st = rec[13];
        if (st > kHzSendFailed || st == kHzSendPending)
            st = kHzSendOk;
        size_t tn = (size_t)len - 14;
        char txt[231];
        memcpy(txt, rec + 14, tn);
        txt[tn] = '\0';
        addMessage(getU32(rec), getU32(rec + 4), rec[8], getU32(rec + 9), txt, false, st, 0, false);
    }
    f.close();
    if (corrupt || fsize > kMsgCompactSize)
        rewriteMessages(); // выкинуть мусор / ужаться
}

void HorizonStore::rewriteMessages(void)
{
    File f = msgFs.open(kMsgPath, "w");
    if (!f)
        return;
    for (uint8_t i = 0; i < kMaxChannels; i++) {
        for (auto &m : chanConvos[i].messages)
            writeRecord(f, m);
    }
    for (auto &c : dms) {
        for (auto &m : c.messages)
            writeRecord(f, m);
    }
    f.close();
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
    HorizonStrings::copyUtf8(d.callsign, sizeof(d.callsign), callsign);
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
    rewriteMessages();
}

void HorizonStore::clearAllDms(void)
{
    dms.clear();
    rewriteMessages();
}

void HorizonStore::clearAll(void)
{
    dms.clear();
    for (uint8_t i = 0; i < kMaxChannels; i++) {
        chanConvos[i].messages.clear();
        chanConvos[i].unread = 0;
    }
    msgFs.remove(kMsgPath);
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
