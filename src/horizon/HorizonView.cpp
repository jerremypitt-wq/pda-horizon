// ПДА «Горизонт» — реализация вида.
#ifdef HORIZON_UI

#include "horizon/HorizonView.h"
#include "graphics/common/ViewController.h"
#include "graphics/driver/DisplayDriver.h"
#include "graphics/driver/DisplayDriverFactory.h"
#include "horizon/HorizonSound.h"
#include "horizon/HorizonUI.h"
#include "util/ILog.h"
#include <string.h>
#include <time.h>
#include <utility>

#if defined(ELECROW_ADV_35)
// Свой LGFX-драйвер CrowPanel Advance 3.5" (вариант платы, см. variants/elecrow-adv-35).
// Создаём LGFXDriver напрямую в нашей единице трансляции, чтобы не требовать
// глобального -include config.h для всей библиотеки device-ui.
#include "config.h"
#include "graphics/driver/LGFXDriver.h"
#endif

HorizonView *HorizonView::gui = nullptr;

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
#if defined(ELECROW_ADV_35)
        gui = new HorizonView(nullptr, new LGFXDriver<LGFX_ELECROW_ADV_35>(w, h));
#else
        gui = new HorizonView(nullptr, DisplayDriverFactory::create(w, h));
#endif
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
    // выставляет это состояние из таймера заставки — делаем то же самое здесь,
    // иначе конфиг/ноды/сообщения никогда не придут.
    state = MeshtasticView::eBootScreenDone;
    // Яркость/таймаут из конфига
    if (displaydriver) {
        displaydriver->setBrightness(HorizonConfig::instance().brightness);
        displaydriver->setScreenTimeout(HorizonConfig::instance().timeoutSec);
    }
    HorizonSound::instance().init();
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
    if (actTime < 1000000 && p.rx_time > 1000000)
        actTime = p.rx_time;
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
    if (msgtime < 1000000)
        msgtime = actTime;
    store.addMessage(from, to, ch, msgtime, msg, !restore);
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
    // Восстановленные — без непрочитанных
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

void HorizonView::updateTime(uint32_t timeVal)
{
    time_t local;
    time(&local);
    if (local > 1000000)
        actTime = (uint32_t)local;
    else if (timeVal > actTime)
        actTime = timeVal;
}

void HorizonView::notifyConnected(const char *info)
{
    (void)info;
    connected = true;
    ILOG_INFO("Horizon: radio connected");
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
    uint32_t ref = now > 1000000 ? (uint32_t)now : (actTime > 1000000 ? actTime : r->lastHeard);
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

void HorizonView::sendChannelMessage(uint8_t ch, const char *text)
{
    if (!text || !*text)
        return;
    // Обрезать до 230 байт по границе UTF-8
    std::string s = text;
    if (s.size() > 230) {
        size_t n = 230;
        while (n > 0 && (s[n] & 0xC0) == 0x80)
            n--;
        s.resize(n);
    }
    uint8_t hop = gotLoRa ? (uint8_t)lora.hop_limit : 3;
    if (hop == 0)
        hop = 3;
    uint32_t reqId = requests.addRequest(ch, ResponseHandler::TextMessageRequest, nullptr, nullptr);
    controller->sendTextMessage(0xFFFFFFFFu, ch, hop, actTime, reqId, false, s.c_str());
    store.addMessage(ownNode, 0xFFFFFFFFu, ch, actTime, s.c_str(), false);
    msgsDirty = true;
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
    controller->sendTextMessage(peer, ch, nodeHopLimit(peer), actTime, reqId, nodeHasKey(peer), s.c_str());
    store.addMessage(ownNode, peer, ch, actTime, s.c_str(), false);
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
    // TODO "don't overwrite is_licensed" — делаем сразу правильно).
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

#endif // HORIZON_UI
