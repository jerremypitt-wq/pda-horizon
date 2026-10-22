// ПДА «Горизонт» - реализация вида.
#ifdef HORIZON_UI

#include "horizon/HorizonView.h"
#include "graphics/common/ViewController.h"
#include "graphics/driver/DisplayDriver.h"
#include "horizon/HorizonDisplay.h"
#include "horizon/HorizonSound.h"
#include "horizon/HorizonUI.h"
#include "util/ILog.h"
#include <stdlib.h> // strtoul для разбора маяков
#include <string.h>
#include <time.h>
#include <utility>

HorizonView *HorizonView::gui = nullptr;

namespace {
// Маяк досье: широковещательный текст в общем канале, который Горизонты
// перехватывают и не показывают. Формат: HZ:DOSSIER:<rank>:<group>:<callsign>
// rank 0..3, group 0..9, позывной - остаток строки (может содержать ':').
constexpr const char *kDossierBeaconPrefix = "HZ:DOSSIER:";
constexpr size_t kDossierBeaconPrefixLen = sizeof("HZ:DOSSIER:") - 1;
constexpr uint32_t kBeaconMinIntervalMs = 5000; // rate-limit маяков
} // namespace

// Ориентация для конструктора панели (variants/elecrow-adv-35/config.h).
// Вызывается из LGFX-ctor при создании драйвера - к этому моменту
// HorizonView::instance() уже выполнил HorizonConfig::load(), так что
// читаем значение из памяти, без файловой системы.
bool horizonIsPortrait(void)
{
    return HorizonConfig::instance().orientation == 1;
}

HorizonView &HorizonView::instance(void)
{
    if (!gui) {
        // Конфиг грузим до создания драйвера: от него зависит ориентация.
        HorizonConfig::instance().load();
        uint16_t w = 480, h = 320;
        if (HorizonConfig::instance().orientation == 1) {
            w = 320;
            h = 480;
        }
        ILOG_INFO("Horizon: creating view %dx%d", w, h);
        gui = new HorizonView(nullptr, horizonCreateDisplayDriver(w, h));
    }
    return *gui;
}

HorizonView::HorizonView(const DisplayDriverConfig *cfg, DisplayDriver *driver)
    : MeshtasticView(cfg, driver, new ViewController)
{
    memset(radioCh, 0, sizeof(radioCh));
    time(&bootTime);
}

void HorizonView::init(IClientBase *client)
{
    ILOG_INFO("Horizon: init...");
    MeshtasticView::init(client);
    // У Горизонта своя заставка, а ViewController шлёт want_config только при
    // state >= eBootScreenDone (см. ViewController::runOnce). Штатный TFTView
    // выставляет это состояние из таймера заставки - делаем то же самое здесь,
    // иначе конфиг/ноды/сообщения никогда не придут.
    state = MeshtasticView::eBootScreenDone;
    // Яркость/таймаут из конфига
    if (displaydriver) {
        displaydriver->setBrightness(HorizonConfig::instance().brightness);
        displaydriver->setScreenTimeout(HorizonConfig::instance().timeoutSec);
    }
    HorizonSound::instance().init();
    // Время: восстановить сохранённое (перезагрузка без сети не должна
    // ронять часы в нули). Mesh-синхронизация позже уточнит через updateTime.
    HorizonConfig::instance().loadTime();
    {
        HorizonConfig &tcfg = HorizonConfig::instance();
        if (tcfg.savedEpoch > 1000000) {
            uint32_t now = millis();
            // Дельту засчитываем только вперёд и не больше 2 суток: после
            // перезагрузки millis сбрасывается - тогда время просто «стоит».
            uint32_t dt = 0;
            if (now >= tcfg.savedMs && now - tcfg.savedMs < 172800000UL)
                dt = (now - tcfg.savedMs) / 1000;
            setActTime(tcfg.savedEpoch + dt);
            // Время «стоит» с момента выключения: без батарейки RTC узнать простой
            // нельзя. Помечаем неточным - первое же время из сети (вперёд) уточнит.
            timeStale = true;
            ILOG_INFO("Horizon: time restored %u (+%us) STALE", (unsigned)tcfg.savedEpoch, (unsigned)dt);
        }
    }
    store.loadMessages(); // история с прошлого запуска (своего в /messages нет)
    ui = new HorizonUI(this);
    ui->init();
    ILOG_INFO("Horizon: init done");
}

void HorizonView::task_handler(void)
{
    MeshtasticView::task_handler();
    HorizonSound::instance().tick();
    if (ui)
        ui->tick();
}

bool HorizonView::bootReady(void) const
{
    if (configComplete && messagesRestored)
        return true;
    time_t now;
    time(&now);
    return (now - bootTime) > 14; // не держим на заставке дольше 14 c
}

// ---------------- входящие данные ----------------

void HorizonView::setMyInfo(uint32_t nodeNum, meshtastic_MyNodeInfo_device_id_t device_id)
{
    (void)device_id;
    ownNode = nodeNum;
    store.setMyNode(nodeNum);
    ILOG_INFO("Horizon: my node 0x%08x", nodeNum);
}

void HorizonView::upsertNodeUser(uint32_t nodeNum, uint8_t channel, uint32_t lastHeard, const meshtastic_User &cfg,
                                 bool viaMqtt)
{
    if (!nodeStore.find(nodeNum) && nodeStore.size() >= MAX_NUM_NODES_VIEW) {
        uint32_t purge = nodeStore.selectPurgeCandidate(nodeNum, ownNode, (uint32_t)curtime);
        if (purge)
            nodeStore.remove(purge);
        else
            return;
    }
    uint32_t lh = lastHeard ? lastHeard : (uint32_t)curtime;
    nodeStore.upsertUser(nodeNum, channel, lh, cfg, viaMqtt);
    nodesDirty = true;
}

void HorizonView::addOrUpdateNode(uint32_t nodeNum, uint8_t channel, uint32_t lastHeard, eRole role, bool hasKey,
                                  bool viaMqtt)
{
    if (!nodeStore.find(nodeNum) && nodeStore.size() >= MAX_NUM_NODES_VIEW) {
        uint32_t purge = nodeStore.selectPurgeCandidate(nodeNum, ownNode, (uint32_t)curtime);
        if (purge)
            nodeStore.remove(purge);
        else
            return;
    }
    uint32_t lh = lastHeard ? lastHeard : (uint32_t)curtime;
    nodeStore.upsertUnknown(nodeNum, channel, lh, (uint8_t)role, hasKey, viaMqtt);
    nodesDirty = true;
}

void HorizonView::addOrUpdateNode(uint32_t nodeNum, uint8_t channel, uint32_t lastHeard, const meshtastic_User &cfg)
{
    upsertNodeUser(nodeNum, channel, lastHeard, cfg, false);
}

void HorizonView::updateNode(uint32_t nodeNum, uint8_t channel, const meshtastic_User &cfg)
{
    const NodeRecord *r = nodeStore.find(nodeNum);
    uint32_t lh = r ? r->lastHeard : (uint32_t)curtime;
    upsertNodeUser(nodeNum, channel, lh, cfg, r ? r->viaMqtt : false);
}

void HorizonView::updatePosition(uint32_t nodeNum, int32_t lat, int32_t lon, int32_t alt, uint32_t sats,
                                 uint32_t precision)
{
    NodePosition pos;
    pos.known = true;
    pos.latitude = lat;
    pos.longitude = lon;
    pos.altitude = alt;
    pos.satellites = sats;
    pos.precision = precision;
    nodeStore.updatePosition(nodeNum, pos);
}

void HorizonView::updateMetrics(uint32_t nodeNum, const meshtastic_DeviceMetrics &metrics)
{
    nodeStore.updateDeviceMetrics(nodeNum, metrics);
    if (nodeNum == ownNode && metrics.has_battery_level && metrics.battery_level <= 100)
        battery = metrics.battery_level;
}

void HorizonView::updateSignalStrength(uint32_t nodeNum, int32_t rssi, float snr)
{
    nodeStore.updateSignal(nodeNum, rssi, snr);
    nodesDirty = true;
}

void HorizonView::updateHopsAway(uint32_t nodeNum, uint8_t hopsAway)
{
    nodeStore.updateHops(nodeNum, (int8_t)hopsAway);
    nodesDirty = true;
}

void HorizonView::updateLastHeard(uint32_t nodeNum)
{
    nodeStore.updateLastHeard(nodeNum, (uint32_t)curtime);
    nodesDirty = true;
}

void HorizonView::packetReceived(const meshtastic_MeshPacket &p)
{
    MeshtasticView::packetReceived(p);
    // Синхронизация часов от сети: часы не шли вообще - принять любое валидное
    // время; восстановлены из файла - принять первое время, идущее вперёд.
    // Назад часы по сети не переводим никогда.
    if (p.rx_time > 1000000 && p.rx_time > getActTime() && (getActTime() < 1000000 || timeStale)) {
        setActTime(p.rx_time);
        timeStale = false;
        persistTime();
    }
    notePacketTools(p);
}

void HorizonView::removeNode(uint32_t nodeNum)
{
    nodeStore.remove(nodeNum);
    nodesDirty = true;
}

bool HorizonView::hasKnownNodeForPacket(uint32_t nodeNum) const
{
    return nodeStore.find(nodeNum) != nullptr;
}

void HorizonView::newMessage(uint32_t from, uint32_t to, uint8_t ch, const char *msg, uint32_t &msgtime, bool restore)
{
    if (!msg)
        return;
    if (to == 0xFFFFFFFFu && !strncmp(msg, kDossierBeaconPrefix, kDossierBeaconPrefixLen)) {
        // Маяк досье: обновить справочник, в историю/тосты/звук не идёт.
        // Восстановленные маяки из /messages тоже разбираем (перестроить
        // справочник после перезагрузки), но не отвечаем на них.
        handleDossierBeacon(from, msg, !restore);
        return;
    }
    if (msgtime < 1000000)
        msgtime = getActTime();
    store.addMessage(from, to, ch, msgtime, msg, !restore, kHzSendOk, 0, !restore);
    msgsDirty = true;
    if (!restore && ui)
        ui->onMessage(from, to, ch, msg, msgtime);
    // Короткий одиночный бип на входящее (свои эхо-пакеты не пищим).
    if (!restore && from != ownNode)
        HorizonSound::instance().play(to == 0xFFFFFFFFu ? HorizonSound::MESSAGE : HorizonSound::DIRECT);
}

void HorizonView::restoreMessage(const LogMessage &msg)
{
    char buf[messagePayloadSize + 1];
    size_t n = msg._size < messagePayloadSize ? msg._size : messagePayloadSize;
    memcpy(buf, msg.bytes, n);
    buf[n] = '\0';
    uint32_t t = (uint32_t)msg.time;
    // Восстановленные - без непрочитанных
    newMessage(msg.from, msg.to, msg.ch, buf, t, true);
}

void HorizonView::updateChannelConfig(const meshtastic_Channel &ch)
{
    if (ch.index < 0 || ch.index >= (int8_t)c_max_channels)
        return;
    radioCh[ch.index] = ch;
    radioChKnown[ch.index] = true;

    HorizonChannelInfo &ci = store.channels[ch.index];
    ci.known = true;
    ci.disabled = (ch.role == meshtastic_Channel_Role_DISABLED);
    ci.primary = (ch.role == meshtastic_Channel_Role_PRIMARY);
    if (ch.has_settings) {
        strncpy(ci.name, ch.settings.name, sizeof(ci.name) - 1);
        ci.name[sizeof(ci.name) - 1] = '\0';
        ci.pskLen = ch.settings.psk.size;
        ci.hasKey = ch.settings.psk.size > 0;
    }
    channelsDirty = true;
}

void HorizonView::updateDeviceConfig(const meshtastic_Config_DeviceConfig &cfg)
{
    device = cfg;
    gotDevice = true;
}

void HorizonView::updateLoRaConfig(const meshtastic_Config_LoRaConfig &cfg)
{
    lora = cfg;
    gotLoRa = true;
}

void HorizonView::persistTime(void)
{
    uint32_t now = getActTime();
    if (now > 1000000)
        HorizonConfig::instance().saveTime(now, millis());
}

void HorizonView::setManualTime(uint32_t epoch)
{
    if (epoch < 1000000)
        return;
    setActTime(epoch);
    timeStale = false;
    persistTime();
    ILOG_INFO("Horizon: manual time %u", (unsigned)epoch);
}

uint32_t HorizonView::getActTime(void) const
{
    if (actTime < 1000000)
        return actTime; // часы не идут - метки нет
    return actTime + (millis() - actTimeMs) / 1000;
}

void HorizonView::setActTime(uint32_t t)
{
    actTime = t;
    actTimeMs = millis();
}

void HorizonView::updateTime(uint32_t timeVal)
{
    time_t local;
    time(&local);
    if (local > 1000000) {
        setActTime((uint32_t)local);
        timeStale = false;
    } else if (timeVal > getActTime()) {
        setActTime(timeVal);
        if (timeStale) {
            timeStale = false;
            persistTime();
        }
    }
}

void HorizonView::notifyConnected(const char *info)
{
    (void)info;
    connected = true;
    ILOG_INFO("Horizon: radio connected");
    announceDossier(); // представиться сети (с rate-limit внутри)
}

void HorizonView::notifyDisconnected(const char *info)
{
    (void)info;
    connected = false;
    ILOG_INFO("Horizon: radio disconnected");
}

void HorizonView::notifyMessagesRestored(void)
{
    MeshtasticView::notifyMessagesRestored();
    msgsDirty = true;
}

void HorizonView::notifyResync(bool show)
{
    if (ui && show)
        ui->toast("Синхронизация с узлом...");
}

void HorizonView::notifyReboot(bool show)
{
    if (ui && show)
        ui->toast("Узел перезагружается...");
}

void HorizonView::notifyShutdown(void)
{
    if (ui)
        ui->toast("Узел выключается...");
}

// ---------------- запросы интерфейса ----------------

const char *HorizonView::nodeLongName(uint32_t nodeNum) const
{
    const NodeRecord *r = nodeStore.find(nodeNum);
    if (r && r->hasUser && r->user.long_name[0])
        return r->user.long_name;
    return "?";
}

const char *HorizonView::nodeShortName(uint32_t nodeNum) const
{
    const NodeRecord *r = nodeStore.find(nodeNum);
    if (r && r->hasUser && r->user.short_name[0])
        return r->user.short_name;
    return "?";
}

bool HorizonView::isNodeOnline(uint32_t nodeNum) const
{
    const NodeRecord *r = nodeStore.find(nodeNum);
    if (!r || r->lastHeard == 0)
        return false;
    time_t now;
    time(&now);
    uint32_t ref = now > 1000000 ? (uint32_t)now : getActTime();
    if (ref < 1000000)
        ref = r->lastHeard;
    return (ref - r->lastHeard) <= secs_until_offline;
}

uint32_t HorizonView::onlineCount(void) const
{
    uint32_t n = 0;
    for (auto &kv : nodeStore.records()) {
        if (kv.first != ownNode && isNodeOnline(kv.first))
            n++;
    }
    return n;
}

uint32_t HorizonView::nodeCount(void) const
{
    return (uint32_t)nodeStore.size();
}

const meshtastic_Channel *HorizonView::radioChannel(uint8_t idx) const
{
    if (idx >= c_max_channels || !radioChKnown[idx])
        return nullptr;
    return &radioCh[idx];
}

const char *HorizonView::channelLabel(uint8_t idx) const
{
    static char buf[24];
    const meshtastic_Channel *ch = radioChannel(idx);
    if (ch && ch->has_settings && ch->settings.name[0])
        return ch->settings.name;
    snprintf(buf, sizeof(buf), "Канал %u", idx);
    return buf;
}

bool HorizonView::channelIsLocked(uint8_t idx) const
{
    if (idx >= c_max_channels)
        return false;
    return store.channels[idx].known && store.channels[idx].hasKey;
}

bool HorizonView::takeNodesDirty(void)
{
    bool v = nodesDirty;
    nodesDirty = false;
    return v;
}

bool HorizonView::takeMsgsDirty(void)
{
    bool v = msgsDirty;
    msgsDirty = false;
    return v;
}

bool HorizonView::takeChannelsDirty(void)
{
    bool v = channelsDirty;
    channelsDirty = false;
    return v;
}

uint8_t HorizonView::nodeChannel(uint32_t nodeNum) const
{
    const NodeRecord *r = nodeStore.find(nodeNum);
    return r ? r->channel : 0;
}

bool HorizonView::nodeHasKey(uint32_t nodeNum) const
{
    const NodeRecord *r = nodeStore.find(nodeNum);
    return r && r->hasKey && !r->hasBadKey;
}

uint8_t HorizonView::nodeHopLimit(uint32_t nodeNum) const
{
    uint8_t hop = gotLoRa ? (uint8_t)lora.hop_limit : 3;
    if (hop == 0)
        hop = 3;
    const NodeRecord *r = nodeStore.find(nodeNum);
    if (r && r->hopsAway >= 0) {
        uint8_t need = (uint8_t)r->hopsAway + 1;
        if (need < hop)
            return need;
    }
    return hop;
}

// ---------------- действия ----------------

namespace {
// Обрезать до 230 байт по границе UTF-8 (лимит текстового пакета).
void truncate230(std::string &s)
{
    if (s.size() > 230) {
        size_t n = 230;
        while (n > 0 && (s[n] & 0xC0) == 0x80)
            n--;
        s.resize(n);
    }
}
} // namespace

void HorizonView::sendChannelRaw(uint8_t ch, const char *text)
{
    if (!text || !*text)
        return;
    std::string s = text;
    truncate230(s);
    uint8_t hop = gotLoRa ? (uint8_t)lora.hop_limit : 3;
    if (hop == 0)
        hop = 3;
    uint32_t reqId = requests.addRequest(ch, ResponseHandler::TextMessageRequest, nullptr, nullptr);
    controller->sendTextMessage(0xFFFFFFFFu, ch, hop, getActTime(), reqId, false, s.c_str());
}

void HorizonView::sendChannelMessage(uint8_t ch, const char *text)
{
    if (!text || !*text)
        return;
    sendChannelRaw(ch, text);
    std::string s = text;
    truncate230(s);
    // Широковещание не подтверждается доставкой по протоколу; единственный
    // твёрдый сигнал - пустая сеть: получить было некому.
    uint8_t st = onlineCount() == 0 ? kHzSendFailed : kHzSendOk;
    store.addMessage(ownNode, 0xFFFFFFFFu, ch, getActTime(), s.c_str(), false, st);
    msgsDirty = true;
}

void HorizonView::announceDossier(void)
{
    if (!connected)
        return;
    uint32_t now = millis();
    if (now - lastBeaconMs < kBeaconMinIntervalMs)
        return;
    lastBeaconMs = now;
    HorizonConfig &cfg = HorizonConfig::instance();
    char buf[96];
    snprintf(buf, sizeof(buf), "%s%u:%u:%s", kDossierBeaconPrefix, cfg.rank, cfg.group, cfg.callsign);
    ILOG_INFO("Horizon: announce dossier '%s'", buf);
    sendChannelRaw(cfg.generalCh, buf);
}

void HorizonView::handleDossierBeacon(uint32_t from, const char *msg, bool canReply)
{
    if (from == 0 || from == ownNode || !msg)
        return;
    const char *p = msg + kDossierBeaconPrefixLen;
    char *e1 = nullptr;
    char *e2 = nullptr;
    unsigned long rank = strtoul(p, &e1, 10);
    if (!e1 || *e1 != ':')
        return;
    unsigned long group = strtoul(e1 + 1, &e2, 10);
    if (!e2 || *e2 != ':' || !*(e2 + 1))
        return;
    if (rank > 3 || group > 9)
        return;
    bool known = store.findDossier(from) != nullptr;
    store.upsertDossier(from, e2 + 1, (uint8_t)rank, (uint8_t)group, getActTime());
    ILOG_INFO("Horizon: dossier %08x '%s' rank=%u group=%u", (unsigned)from, e2 + 1, (unsigned)rank,
              (unsigned)group);
    if (canReply && !known)
        announceDossier(); // представиться в ответ (с rate-limit внутри)
}

void HorizonView::sendDirectMessage(uint32_t peer, const char *text)
{
    if (!text || !*text || peer == 0 || peer == 0xFFFFFFFFu)
        return;
    std::string s = text;
    if (s.size() > 230) {
        size_t n = 230;
        while (n > 0 && (s[n] & 0xC0) == 0x80)
            n--;
        s.resize(n);
    }
    uint8_t ch = nodeChannel(peer);
    uint32_t reqId = requests.addRequest(peer, ResponseHandler::TextMessageRequest, nullptr, nullptr);
    controller->sendTextMessage(peer, ch, nodeHopLimit(peer), getActTime(), reqId, nodeHasKey(peer), s.c_str());
    uint8_t st;
    uint32_t track = 0;
    if (onlineCount() == 0) {
        st = kHzSendFailed; // в эфире никого - доставка невозможна
        requests.removeRequest(reqId);
    } else {
        st = kHzSendPending; // ждём ACK (резолв в handleResponse/pollTools)
        track = reqId;
        pendingTexts.push_back({reqId, millis()});
        while (pendingTexts.size() > 16) {
            store.markTextDelivered(pendingTexts.front().reqId, false);
            requests.removeRequest(pendingTexts.front().reqId);
            pendingTexts.erase(pendingTexts.begin());
        }
    }
    store.addMessage(ownNode, peer, ch, getActTime(), s.c_str(), false, st, track);
    msgsDirty = true;
}

void HorizonView::applyOwnerFromDossier(void)
{
    HorizonConfig &cfg = HorizonConfig::instance();
    meshtastic_User user{};
    std::string ln = cfg.meshLongName();
    std::string sn = cfg.meshShortName();
    strncpy(user.long_name, ln.c_str(), sizeof(user.long_name) - 1);
    strncpy(user.short_name, sn.c_str(), sizeof(user.short_name) - 1);
    user.hw_model = meshtastic_HardwareModel_UNSET;
    // Не затирать модель железа и флаг лицензии на узле (у штатного UI тут
    // TODO "don't overwrite is_licensed" - делаем сразу правильно).
    const NodeRecord *self = nodeStore.find(ownNode);
    if (self && self->hasUser) {
        user.hw_model = self->user.hw_model;
        user.is_licensed = self->user.is_licensed;
    }
    ILOG_INFO("Horizon: set owner '%s' / '%s'", user.long_name, user.short_name);
    controller->sendConfig(user, ownNode);
}

void HorizonView::applyChannelSettings(uint8_t idx, const char *name, uint8_t simpleKey)
{
    if (idx >= c_max_channels)
        return;
    meshtastic_Channel ch{};
    if (radioChKnown[idx])
        ch = radioCh[idx];
    ch.index = (int8_t)idx;
    if (idx == 0)
        ch.role = meshtastic_Channel_Role_PRIMARY;
    else if (ch.role == meshtastic_Channel_Role_DISABLED)
        ch.role = meshtastic_Channel_Role_SECONDARY;
    ch.has_settings = true;
    if (name) {
        strncpy(ch.settings.name, name, sizeof(ch.settings.name) - 1);
        ch.settings.name[sizeof(ch.settings.name) - 1] = '\0';
        // Не разрывать UTF-8 символ на границе буфера (11 кириллических
        // символов = 22 байта при name[12]): сначала отступаем через висячие
        // байты-продолжения, затем убираем осиротевший ведущий байт.
        size_t nl = strlen(ch.settings.name);
        while (nl > 0 && (((unsigned char)ch.settings.name[nl - 1] & 0xC0) == 0x80))
            nl--;
        if (nl > 0 && ((unsigned char)ch.settings.name[nl - 1] & 0x80))
            nl--;
        ch.settings.name[nl] = '\0';
    }
    if (simpleKey != 0xFF) {
        if (simpleKey == 0) {
            ch.settings.psk.size = 0;
        } else {
            ch.settings.psk.size = 1;
            ch.settings.psk.bytes[0] = simpleKey; // 1=default, 2..11=simple1..10
        }
    }
    // Игровые чаты не должны утекать в публичный интернет через MQTT-шлюзы.
    ch.settings.uplink_enabled = false;
    ch.settings.downlink_enabled = false;
    if (ch.settings.id == 0 && ch.settings.name[0]) {
        // Детерминированный id по имени, чтобы все ПДА игры сошлись на одном.
        uint32_t h = 5381;
        for (const char *s = ch.settings.name; *s; s++)
            h = ((h << 5) + h) ^ (uint8_t)*s;
        ch.settings.id = h ? h : 1;
    }
    ILOG_INFO("Horizon: set channel %u name='%s' key=%u", idx, ch.settings.name, simpleKey);
    controller->sendConfig(ch, ownNode);
}

void HorizonView::applyLoRaConfig(int region, int preset, uint32_t hopLimit, bool txEnabled)
{
    meshtastic_Config_LoRaConfig l = gotLoRa ? lora : meshtastic_Config_LoRaConfig{};
    l.use_preset = true;
    if (region >= 0)
        l.region = (meshtastic_Config_LoRaConfig_RegionCode)region;
    if (preset >= 0)
        l.modem_preset = (meshtastic_Config_LoRaConfig_ModemPreset)preset;
    l.hop_limit = hopLimit;
    l.tx_enabled = txEnabled;
    ILOG_INFO("Horizon: set lora region=%d preset=%d hops=%u tx=%d", region, preset, (unsigned)hopLimit, txEnabled);
    controller->sendConfig(std::move(l), ownNode);
}

void HorizonView::applyDeviceRole(int role)
{
    meshtastic_Config_DeviceConfig d = gotDevice ? device : meshtastic_Config_DeviceConfig{};
    if (role >= 0)
        d.role = (meshtastic_Config_DeviceConfig_Role)role;
    ILOG_INFO("Horizon: set device role=%d", role);
    controller->sendConfig(std::move(d), ownNode);
}

void HorizonView::rebootNode(void)
{
    controller->requestReboot(3, ownNode);
}

void HorizonView::resetNodeDb(void)
{
    controller->requestReset(false, ownNode);
}

void HorizonView::factoryResetNode(void)
{
    controller->requestReset(true, ownNode);
}

// ---------------- инструменты Mesh ----------------

namespace {
constexpr uint32_t kScanIntervalMs = 2500; // темп замеров сканера
constexpr uint32_t kTraceTimeoutMs = 20000; // ожидание ответа маршрута
constexpr uint8_t kScanCount = 8;           // замеров за один проход
constexpr size_t kStatsMaxNodes = 40;       // лимит таблицы статистики
} // namespace

void HorizonView::notePacketTools(const meshtastic_MeshPacket &p)
{
    if (p.from == 0)
        return;
    // Лог: свои админские пакеты не пишем (как в штатном виде).
    if (!(p.from == ownNode && p.decoded.portnum == meshtastic_PortNum_ADMIN_APP)) {
        HzPacketEntry e;
        e.time = getActTime();
        e.from = p.from;
        e.to = p.to;
        e.ch = (uint8_t)p.channel;
        e.port = (uint16_t)p.decoded.portnum;
        e.size = (uint16_t)p.decoded.payload.size;
        e.rssi = (int16_t)p.rx_rssi;
        e.snr = p.rx_snr;
        pktLog.push_back(e);
        while (pktLog.size() > kPacketLogMax)
            pktLog.erase(pktLog.begin());
    }
    // Статистика по узлу-отправителю.
    HzNodeStats *st = nullptr;
    for (auto &s : nodeStats_) {
        if (s.node == p.from) {
            st = &s;
            break;
        }
    }
    if (!st) {
        if (nodeStats_.size() >= kStatsMaxNodes)
            return;
        HzNodeStats ns;
        ns.node = p.from;
        nodeStats_.push_back(ns);
        st = &nodeStats_.back();
    }
    switch (p.decoded.portnum) {
    case meshtastic_PortNum_TEXT_MESSAGE_APP:
    case meshtastic_PortNum_RANGE_TEST_APP:
        st->txt++;
        break;
    case meshtastic_PortNum_POSITION_APP:
        st->pos++;
        break;
    case meshtastic_PortNum_NODEINFO_APP:
        st->inf++;
        break;
    case meshtastic_PortNum_ROUTING_APP:
    case meshtastic_PortNum_TRACEROUTE_APP:
        st->trc++;
        break;
    case meshtastic_PortNum_TELEMETRY_APP:
        if (p.from == ownNode)
            return; // свои батарейные пакеты не считаем (как в штатном виде)
        st->tel++;
        break;
    case meshtastic_PortNum_NEIGHBORINFO_APP:
        st->nbr++;
        break;
    default:
        break;
    }
    st->sum++;
    statsTotal_++;
    toolsDirty = true;
}

std::vector<HorizonView::HzNodeStats> HorizonView::nodeStatsSorted(void) const
{
    std::vector<HzNodeStats> v = nodeStats_;
    // Пузырьком: узлов мало, зато без <algorithm>.
    for (size_t i = 0; i < v.size(); i++) {
        for (size_t j = i + 1; j < v.size(); j++) {
            if (v[j].sum > v[i].sum) {
                HzNodeStats t = v[i];
                v[i] = v[j];
                v[j] = t;
            }
        }
    }
    return v;
}

void HorizonView::clearNodeStats(void)
{
    nodeStats_.clear();
    statsTotal_ = 0;
    toolsDirty = true;
}

bool HorizonView::takeToolsDirty(void)
{
    bool d = toolsDirty;
    toolsDirty = false;
    return d;
}

void HorizonView::startScan(uint32_t node)
{
    stopTrace();
    scanNode = node;
    scanCount = 0;
    scanWant = kScanCount;
    scanGot = false;
    scanRssi = 0;
    scanSnr = 0;
    scanDirect = false;
    scanReq = 0;
    scanNextMs = 0; // первый замер сразу
    scanRunning = true;
    toolsDirty = true;
    ILOG_INFO("Horizon: scan start %08x", (unsigned)node);
}

void HorizonView::stopScan(void)
{
    if (scanReq)
        requests.removeRequest(scanReq);
    scanReq = 0;
    scanRunning = false;
    toolsDirty = true;
}

void HorizonView::startTrace(uint32_t node)
{
    stopScan();
    traceNode = node;
    traceGot = false;
    traceFail = false;
    trace = HzTraceResult();
    if (!controller) {
        traceFail = true;
        return;
    }
    traceReq = requests.addRequest(node, ResponseHandler::TraceRouteRequest, nullptr, nullptr);
    controller->traceRoute(node, nodeChannel(node), nodeHopLimit(node), traceReq);
    traceStartMs = millis();
    traceRunning = true;
    toolsDirty = true;
    ILOG_INFO("Horizon: trace start %08x", (unsigned)node);
}

void HorizonView::stopTrace(void)
{
    if (traceReq)
        requests.removeRequest(traceReq);
    traceReq = 0;
    traceRunning = false;
    toolsDirty = true;
}

void HorizonView::pollTools(uint32_t nowMs)
{
    if (scanRunning) {
        if (scanCount >= scanWant) {
            scanRunning = false; // замеры кончились - экран покажет итог
            toolsDirty = true;
        } else if ((int32_t)(nowMs - scanNextMs) >= 0) {
            if (scanReq)
                requests.removeRequest(scanReq);
            scanReq = requests.addRequest(scanNode, ResponseHandler::PositionRequest, nullptr, nullptr);
            if (controller)
                controller->requestPosition(scanNode, nodeChannel(scanNode), scanReq);
            scanCount++;
            scanNextMs = nowMs + kScanIntervalMs;
            toolsDirty = true;
        }
    }
    // Таймаут доставки лички: 60 с без ACK - не доставлено (пузырь краснеет
    // сам: msgsDirty перерисует открытый чат).
    for (size_t i = 0; i < pendingTexts.size();) {
        uint32_t rid = pendingTexts[i].reqId;
        const HorizonMessage *m = store.findTextByReq(rid);
        if (!m || m->sendStatus != kHzSendPending) {
            pendingTexts.erase(pendingTexts.begin() + i); // резолвлён в handleResponse
            continue;
        }
        if (nowMs - pendingTexts[i].sentMs >= 60000) {
            store.markTextDelivered(rid, false);
            requests.removeRequest(rid);
            pendingTexts.erase(pendingTexts.begin() + i);
            msgsDirty = true;
            continue;
        }
        i++;
    }
    if (traceRunning && nowMs - traceStartMs >= kTraceTimeoutMs) {
        traceFail = true;
        traceRunning = false;
        if (traceReq)
            requests.removeRequest(traceReq);
        traceReq = 0;
        toolsDirty = true;
    }
}

void HorizonView::handleResponse(uint32_t from, uint32_t id, const meshtastic_Routing &routing,
                                 const meshtastic_MeshPacket &p)
{
    (void)p;
    if (traceRunning && id == traceReq) {
        // Ошибка доставки - маршрут не построить. Чистый ACK игнорируем:
        // следом придёт RouteDiscovery.
        if (routing.which_variant == meshtastic_Routing_error_reason_tag &&
            routing.error_reason != meshtastic_Routing_Error_NONE) {
            traceFail = true;
            traceRunning = false;
            requests.removeRequest(id);
            traceReq = 0;
            toolsDirty = true;
        }
        return;
    }
    handleTextDelivery(from, id, routing);
}

// Доставка текстов (вызывается из handleResponse выше, когда id - не маршрут).
// Как в штатном виде: локальное эхо (from==ownNode) - только факт передачи
// своей радиостанцией, ошибка в нём - провал сразу; ответ сети -
// настоящий ACK/ошибка доставки. Сообщения каналов reqId не хранят,
// поэтому их отклики здесь - тихий no-op.
void HorizonView::handleTextDelivery(uint32_t from, uint32_t id, const meshtastic_Routing &routing)
{
    if (routing.which_variant != meshtastic_Routing_error_reason_tag)
        return;
    ResponseHandler::Request req = requests.findRequest(id);
    if (req.type != ResponseHandler::TextMessageRequest)
        return;
    if (from == ownNode) {
        if (routing.error_reason != meshtastic_Routing_Error_NONE) {
            if (store.markTextDelivered(id, false))
                msgsDirty = true;
            requests.removeRequest(id);
        }
        // Чистый локальный ACK: доставка ещё впереди - ждём дальше.
        return;
    }
    bool ok = (routing.error_reason == meshtastic_Routing_Error_NONE);
    if (store.markTextDelivered(id, ok))
        msgsDirty = true;
    requests.removeRequest(id);
}

void HorizonView::handleResponse(uint32_t from, uint32_t id, const meshtastic_RouteDiscovery &route)
{
    (void)from;
    if (!traceRunning || id != traceReq)
        return;
    trace = HzTraceResult();
    trace.valid = true;
    trace.fwdN = route.route_count > 8 ? 8 : (uint8_t)route.route_count;
    for (uint8_t i = 0; i < trace.fwdN; i++)
        trace.fwd[i] = route.route[i];
    trace.backN = route.route_back_count > 8 ? 8 : (uint8_t)route.route_back_count;
    for (uint8_t i = 0; i < trace.backN; i++)
        trace.back[i] = route.route_back[i];
    traceGot = true;
    traceRunning = false;
    requests.removeRequest(id);
    traceReq = 0;
    toolsDirty = true;
    ILOG_INFO("Horizon: trace reply fwd=%u back=%u", trace.fwdN, trace.backN);
}

void HorizonView::handlePositionResponse(uint32_t from, uint32_t request_id, int32_t rx_rssi, float rx_snr,
                                         bool isNeighbor)
{
    if (!scanRunning || request_id != scanReq || from != scanNode)
        return;
    scanRssi = rx_rssi;
    scanSnr = rx_snr;
    scanDirect = isNeighbor;
    scanGot = true;
    requests.removeRequest(request_id);
    scanReq = 0;
    toolsDirty = true;
}

#endif // HORIZON_UI
