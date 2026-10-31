#include "horizon_target.h" // целевые флаги сборки (вкус: standalone/прошивка)
// ПДА «Горизонт» - реализация вида.
#ifdef HORIZON_UI

#include "horizon/HorizonView.h"
#include "graphics/common/ViewController.h"
#include "graphics/driver/DisplayDriver.h"
#include "horizon/HorizonDisplay.h"
#include "horizon/HorizonSound.h"
#include "horizon/HorizonStrings.h" // kFirmwareVersion в стартовый лог
#include "horizon/HorizonUI.h"
#include "util/ILog.h"
#include <stdlib.h> // strtoul для разбора маяков
#include <string.h>
#include <time.h>
#include <utility>

// Тост при закрытом гейте radioConnected: в прошивке радиомодуль - это сам ПДА.
#ifdef HORIZON_PACKET_LINK
#define HZ_NO_LINK "Нет связи"
#else
#define HZ_NO_LINK "Нет связи с радиомодулем"
#endif

HorizonView *HorizonView::gui = nullptr;

volatile uint32_t hzUartRxBytes = 0;
volatile uint32_t hzUartRxFrames = 0;

namespace {
// Маяк досье: широковещательный текст в общем канале, который Горизонты
// перехватывают и не показывают. Формат: HZ:DOSSIER:<rank>:<group>:<epoch>:<callsign>
// rank 0..3, group 0..9, epoch - время отправителя либо 0 (часы не выставлены),
// позывной - остаток строки (может содержать ':').
constexpr const char *kDossierBeaconPrefix = "HZ:DOSSIER:";
constexpr size_t kDossierBeaconPrefixLen = sizeof("HZ:DOSSIER:") - 1;
constexpr uint32_t kBeaconMinIntervalMs = 5000; // rate-limit маяков
constexpr uint32_t kBeaconPeriodicMs = 15 * 60 * 1000; // переозвучка досье
// Синхронизация времени от мастера: PRIVATE_APP-пакет, который Горизонты
// перехватывают в packetReceived и применяют. Формат: "HZT1" + uint32 LE epoch.
// Штатные клиенты PRIVATE_APP молча игнорируют - эфир не засоряем.
constexpr const char *kTimeSyncMagic = "HZT1";
constexpr size_t kTimeSyncMagicLen = 4;
constexpr size_t kTimeSyncPayloadLen = 8;
constexpr uint32_t kTimeSyncMinEpoch = 1704067200; // 2024-01-01 UTC
constexpr uint32_t kTimeSyncMaxEpoch = 2082758400; // 2036-01-01 UTC
// Точки карты (см. docs/MAP.md раздел 2): широковещательный текст в канале,
// который Горизонты перехватывают и не показывают.
// HZ:POI:<id>|<total>|<idx>|<type>|<name>|<x>|<y>|<sender>|<chunk>
// HZ:POID:<id>|<node>|<rank>
constexpr const char *kPoiPrefix = "HZ:POI:";
constexpr size_t kPoiPrefixLen = sizeof("HZ:POI:") - 1;
constexpr const char *kPoiDelPrefix = "HZ:POID:";
constexpr size_t kPoiDelPrefixLen = sizeof("HZ:POID:") - 1;
constexpr size_t kPoiChunkMax = 96; // байт комментария в части (бюджет части - 220)
constexpr uint8_t kPoiTotalMax = 8; // частей максимум
constexpr size_t kPoiRxMax = 4;     // параллельных сборок
constexpr uint32_t kPoiRxTimeoutMs = 120000; // несобранное выкинуть
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

// Подкласс контроллера: открывает protected generic send() для сырых пакетов
// (синхронизация времени через PRIVATE_APP). Форк не трогаем.
class HzController : public ViewController {
  public:
    bool sendRaw(uint32_t to, uint8_t ch, uint8_t hopLimit, meshtastic_PortNum portnum,
                 const unsigned char *bytes, size_t len)
    {
        // NOTE: send() читает все 233 байта буфера независимо от len (см. форк),
        // поэтому передаём только полный занулённый буфер - перечтения нет.
        unsigned char buf[meshtastic_Constants_DATA_PAYLOAD_LEN] = {};
        if (len > sizeof(buf))
            return false;
        memcpy(buf, bytes, len);
        return send(to, ch, hopLimit, 0, portnum, false, false, buf, len);
    }
};

HzController *HorizonView::hzController(void)
{
    return static_cast<HzController *>(controller);
}

HorizonView::HorizonView(const DisplayDriverConfig *cfg, DisplayDriver *driver)
    : MeshtasticView(cfg, driver, new HzController)
{
    memset(radioCh, 0, sizeof(radioCh));
    time(&bootTime);
}

void HorizonView::init(IClientBase *client)
{
    ILOG_INFO("Horizon: v%s init...", HorizonStrings::kFirmwareVersion);
    MeshtasticView::init(client);
#ifdef HORIZON_PACKET_LINK
    // В дереве прошивки нет ARCH_ESP32, поэтому TFTDriver не ставит esp_timer
    // для LVGLтика. Без тика lv_tick_get() стоит - а на нём у нас nowMs():
    // tick() вечно возвращается досрочно и UI висит на заставке.
    // Отдаём LVGL часы напрямую через millis() (единственный источник тика
    // в этом вкусе: esp_timer не создан, lv_tick_inc никто не зовёт).
    lv_tick_set_cb([]() -> uint32_t { return millis(); });
#endif
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
    // Свой номер - ещё до реставра истории: свои исходящие сразу лягут
    // по собеседникам, а не в «чат с собой» (0 на первом запуске - не беда,
    // лог тогда пуст, а setMyNode позже всё равно переразложит).
    store.setMyNode(HorizonConfig::instance().meshNode);
    store.loadMessages(); // история с прошлого запуска: свой лог + штатный /messages
    // (в журнале есть и свои сообщения; кратные копии давит дедуп в addMessage)
    store.loadDossier();  // справочник досье (имена свои сразу, не «Meshtastic ****»)
    mapStore.loadAll();     // точки карты /hzmap
    ui = new HorizonUI(this);
    ui->init();
#ifdef HORIZON_PACKET_LINK
    // PacketClient колбэков состояния не шлёт (setNotifyCallback - no-op):
    // внутренний линк жив уже по факту запуска.
    notifyConnected("packet");
#endif
    ILOG_INFO("Horizon: init done");
}

void HorizonView::task_handler(void)
{
    MeshtasticView::task_handler();
    HorizonSound::instance().tick();
    if (ui)
        ui->tick();
    // Переозвучка досье раз в 15 минут: переименование расходится одним
    // broadcast-маяком без повторов (при сохранении), а ответные маяки шлются
    // только новым узлам - потерянный маяк = рассинхрон навсегда. Периодика
    // чинит несогласованность позывных в пределах 15 минут.
    uint32_t nowMs = millis();
    if (nowMs - lastPeriodicBeaconMs >= kBeaconPeriodicMs) {
        lastPeriodicBeaconMs = nowMs;
        announceDossier();
    }
    pollBattery(nowMs);
}

void HorizonView::pollBattery(uint32_t nowMs)
{
    if (batteryProbed && nowMs - lastBatteryMs < 30000)
        return; // чаще раза в 30 с батарею не дёргаем
    batteryProbed = true;
    lastBatteryMs = nowMs;
    hzBatteryPoll(vbat, HorizonConfig::instance().vbatScale);
    if (vbat.percent >= 0)
        battery = vbat.percent; // делитель главнее телеметрии радио
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

#ifdef HORIZON_PACKET_LINK
void HorizonView::setMyInfo(uint32_t nodeNum)
{
#else
void HorizonView::setMyInfo(uint32_t nodeNum, meshtastic_MyNodeInfo_device_id_t device_id)
{
    (void)device_id;
#endif
    ownNode = nodeNum;
    store.setMyNode(nodeNum);
    HorizonConfig &cfg = HorizonConfig::instance();
    if (cfg.meshNode != nodeNum) {
        cfg.meshNode = nodeNum;
        cfg.save(); // персист: следующая загрузка разложит историю до линка
    }
    ILOG_INFO("Horizon: my node 0x%08x", nodeNum);
    announceDossier(); // радио реально живо - представиться сети
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
    if (nodeNum == ownNode)
        pushOwnerFromDossier(); // своя nodeinfo: дефолтное имя? - усыновить
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
    if (nodeStore.find(nodeNum) && cfg.hw_model == meshtastic_HardwareModel_UNSET &&
        strncmp(cfg.long_name, "Meshtastic ", 11) == 0) {
        // Синтетическая заглушка из базового packetReceived («Meshtastic XXXX»):
        // настоящее имя уже есть - не затирать, только освежить время.
        // В форке сюда не попасть (там гейт hasKnownNodeForPacket), в апстриме
        // базовый `nodes` пуст всегда - без гарда имя затиралось бы каждый пакет.
        updateLastHeard(nodeNum);
        return;
    }
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

#ifdef HORIZON_PACKET_LINK
void HorizonView::updateMetrics(uint32_t nodeNum, uint32_t bat_level, float voltage, float chUtil, float airUtil)
{
    meshtastic_DeviceMetrics metrics{};
    metrics.has_battery_level = (bat_level > 0 && bat_level <= 100);
    metrics.battery_level = bat_level;
    metrics.has_voltage = true;
    metrics.voltage = voltage;
    metrics.has_channel_utilization = true;
    metrics.channel_utilization = chUtil;
    metrics.has_air_util_tx = true;
    metrics.air_util_tx = airUtil;
    updateMetrics(nodeNum, metrics);
}
#endif

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
    // Живой пакет: штамповать в домене часов панели, иначе при выставленных
    // часах все узлы вечно «офлайн» (смешались бы секунды с эпохи и с загрузки).
    uint32_t now = getActTime();
    if (now < 1000000)
        now = (uint32_t)curtime;
    nodeStore.updateLastHeard(nodeNum, now);
    nodesDirty = true;
}

void HorizonView::packetReceived(const meshtastic_MeshPacket &p)
{
    if (p.decoded.portnum == meshtastic_PortNum_PRIVATE_APP && handleTimeSync(p)) {
        notePacketTools(p); // в статистику пакет идёт, в чат/лог/звук - нет
        return;
    }
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

#ifndef HORIZON_PACKET_LINK
bool HorizonView::hasKnownNodeForPacket(uint32_t nodeNum) const
{
    return nodeStore.find(nodeNum) != nullptr;
}
#endif

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
    // ВАЖНО: POID проверять раньше POI («HZ:POI:» - префикс «HZ:POID:»).
    if (!strncmp(msg, kPoiDelPrefix, kPoiDelPrefixLen)) {
        // Удаление точки: в историю/тосты не идёт (тост - только по факту).
        handlePoiDelete(from, msg, !restore);
        return;
    }
    if (!strncmp(msg, kPoiPrefix, kPoiPrefixLen)) {
        // Точка карты: собрать, в историю/тосты не идёт (тост - только по факту).
        handlePoiBeacon(from, ch, msg, !restore);
        return;
    }
    if (msgtime < 1000000)
        msgtime = getActTime();
    store.addMessage(from, to, ch, msgtime, msg, !restore, kHzSendOk, 0, !restore);
    msgsDirty = true;
    if (!restore && ui)
        ui->onMessage(from, to, ch, msg, msgtime);
    // Бип на входящее (свои эхо-пакеты не пищим): личка - DIRECT-паттерном,
    // общий эфир - MESSAGE. Звук - выбранный в настройках.
    if (!restore && from != ownNode)
        HorizonSound::instance().play(to == ownNode ? HorizonSound::DIRECT : HorizonSound::MESSAGE);
}

void HorizonView::restoreMessage(const LogMessage &msg)
{
    char buf[messagePayloadSize + 1];
    size_t n = msg._size < messagePayloadSize ? msg._size : messagePayloadSize;
    memcpy(buf, msg.bytes, n);
    buf[n] = '\0';
    uint32_t t = (uint32_t)msg.time;
    // Корзина личек: выборочного удаления в журнале контроллера нет, старые
    // лички лежат там бессрочно - отсекаем по метке очистки. Каналы льются всегда.
    if (msg.to != 0xFFFFFFFFu && t <= HorizonConfig::instance().dmsClearedAt)
        return;
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
    silenceFirmwareBuzzer(); // сам решит, надо ли (вкус прошивки, раз в загрузку)
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

const char *HorizonView::displayName(uint32_t nodeNum) const
{
    const HorizonDossierInfo *d = store.findDossier(nodeNum);
    if (d && d->callsign[0])
        return d->callsign;
    const NodeRecord *r = nodeStore.find(nodeNum);
    if (r && r->hasUser) {
        if (r->user.long_name[0])
            return r->user.long_name;
        if (r->user.short_name[0])
            return r->user.short_name;
    }
    // Узла ещё нет в справочниках: стабильный короткий фолбэк вместо «?».
    static char fb[4][8];
    static uint8_t fi = 0;
    fi = (uint8_t)((fi + 1) & 3);
    snprintf(fb[fi], sizeof(fb[fi]), "!%04X", (unsigned)(nodeNum & 0xFFFF));
    return fb[fi];
}

bool HorizonView::isNodeHeardWithin(uint32_t nodeNum, uint32_t windowSec) const
{
    const NodeRecord *r = nodeStore.find(nodeNum);
    if (!r || r->lastHeard == 0)
        return false;
    time_t now;
    time(&now);
    uint32_t ref = now > 1000000 ? (uint32_t)now : getActTime();
    if (ref < 1000000)
        ref = r->lastHeard;
    return (ref - r->lastHeard) <= windowSec;
}

bool HorizonView::isNodeOnline(uint32_t nodeNum) const
{
    return isNodeHeardWithin(nodeNum, secs_until_offline);
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
    if (!radioConnected() && ui)
        ui->toast(HZ_NO_LINK);
    sendChannelRaw(ch, text);
    std::string s = text;
    truncate230(s);
    // Широковещание не подтверждается доставкой по протоколу - пометку
    // «не доставлено» для общих каналов не ставим никогда.
    uint8_t st = kHzSendOk;
    store.addMessage(ownNode, 0xFFFFFFFFu, ch, getActTime(), s.c_str(), false, st);
    msgsDirty = true;
}

void HorizonView::announceDossier(void)
{
    if (ownNode == 0)
        return; // my_info ещё не приходил - радио молчит, слать некуда

    uint32_t now = millis();
    if (now - lastBeaconMs < kBeaconMinIntervalMs)
        return;
    lastBeaconMs = now;
    HorizonConfig &cfg = HorizonConfig::instance();
    // Время в маяке - только доверенное (свои часы выставлены и не протухли),
    // иначе 0: чужое «примерно» хуже отсутствия времени.
    uint32_t ep = (!timeStale && getActTime() > 1000000) ? getActTime() : 0;
    char buf[96];
    snprintf(buf, sizeof(buf), "%s%u:%u:%u:%s", kDossierBeaconPrefix, cfg.rank, cfg.group, ep, cfg.callsign);
    ILOG_INFO("Horizon: announce dossier '%s'", buf);
    sendChannelRaw(cfg.generalCh, buf);
}

void HorizonView::broadcastTimeSync(void)
{
    if (!radioConnected()) {
        if (ui)
            ui->toast(HZ_NO_LINK);
        return;
    }
    uint32_t ep = getActTime();
    if (ep < 1000000 || timeStale) {
        if (ui)
            ui->toast("Сначала установите время");
        return;
    }
    uint8_t hop = gotLoRa ? (uint8_t)lora.hop_limit : 3;
    if (hop == 0)
        hop = 3;
    HorizonConfig &cfg = HorizonConfig::instance();
    unsigned char payload[kTimeSyncPayloadLen];
    memcpy(payload, kTimeSyncMagic, kTimeSyncMagicLen);
    payload[4] = (unsigned char)(ep & 0xFF);
    payload[5] = (unsigned char)((ep >> 8) & 0xFF);
    payload[6] = (unsigned char)((ep >> 16) & 0xFF);
    payload[7] = (unsigned char)((ep >> 24) & 0xFF);
    if (hzController()->sendRaw(0xFFFFFFFFu, cfg.generalCh, hop, meshtastic_PortNum_PRIVATE_APP, payload,
                                kTimeSyncPayloadLen)) {
        if (ui)
            ui->toast("Время разослано");
    } else if (ui) {
        ui->toast(HZ_NO_LINK);
    }
    ILOG_INFO("Horizon: time sync broadcast %u", (unsigned)ep);
}

bool HorizonView::handleTimeSync(const meshtastic_MeshPacket &p)
{
    if (p.which_payload_variant != meshtastic_MeshPacket_decoded_tag)
        return false;
    const meshtastic_Data &d = p.decoded;
    if (d.payload.size < kTimeSyncPayloadLen || memcmp(d.payload.bytes, kTimeSyncMagic, kTimeSyncMagicLen) != 0)
        return false;
    if (p.from == ownNode)
        return true; // своё эхо (релей вернул) - часы уже такие
    uint32_t ep = (uint32_t)d.payload.bytes[4] | ((uint32_t)d.payload.bytes[5] << 8) |
                  ((uint32_t)d.payload.bytes[6] << 16) | ((uint32_t)d.payload.bytes[7] << 24);
    if (ep < kTimeSyncMinEpoch || ep >= kTimeSyncMaxEpoch) {
        ILOG_INFO("Horizon: time sync out of range %u", (unsigned)ep);
        return true;
    }
    // Явная команда мастера - применяем всегда, в т.ч. назад (отмотка игры).
    setManualTime(ep);
    if (ui) {
        char db[16], tb[16];
        HorizonUI::fmtDate(ep, db, sizeof(db));
        HorizonUI::fmtTime(ep, tb, sizeof(tb));
        char msg[96];
        snprintf(msg, sizeof(msg), "Время: %s %s (%.32s)", db, tb, displayName(p.from));
        ui->toast(msg);
    }
    ILOG_INFO("Horizon: time sync applied %u from 0x%08x", (unsigned)ep, (unsigned)p.from);
    return true;
}

void HorizonView::handleDossierBeacon(uint32_t from, const char *msg, bool canReply)
{
    if (from == 0 || from == ownNode || !msg)
        return;
    // Формат: HZ:DOSSIER:<rank>:<group>[:<epoch>]:<callsign>. Третье числовое
    // поле - всегда epoch нового формата: валидное время либо 0 (часы гостя
    // не выставлены). Ноль тоже пропускаем, иначе позывной получит префикс
    // «0:». Без третьего числового поля остаток - позывной целиком (старье).
    const char *p = msg + kDossierBeaconPrefixLen;
    char *e1 = nullptr;
    char *e2 = nullptr;
    char *e3 = nullptr;
    unsigned long rank = strtoul(p, &e1, 10);
    if (!e1 || *e1 != ':')
        return;
    unsigned long group = strtoul(e1 + 1, &e2, 10);
    if (!e2 || *e2 != ':' || !*(e2 + 1))
        return;
    if (rank > 3 || group > 9)
        return;
    const char *rest = e2 + 1;
    const char *callsign = rest;
    uint32_t epoch = 0;
    unsigned long maybeEpoch = strtoul(rest, &e3, 10);
    if (e3 && *e3 == ':' && *(e3 + 1) && (maybeEpoch > 1000000 || maybeEpoch == 0)) {
        epoch = (uint32_t)maybeEpoch;
        callsign = e3 + 1;
    }
    bool known = store.findDossier(from) != nullptr;
    store.upsertDossier(from, callsign, (uint8_t)rank, (uint8_t)group, getActTime());
    // Время от маяка: свои часы не выставлены или протухли - принять время
    // отправителя, если оно валидно и идёт вперёд. Назад - никогда.
    if (epoch > 1000000 && epoch > getActTime() && (getActTime() < 1000000 || timeStale)) {
        setActTime(epoch);
        timeStale = false;
        persistTime();
        ILOG_INFO("Horizon: time adopted from beacon %08x: %u", (unsigned)from, (unsigned)epoch);
    }
    ILOG_INFO("Horizon: dossier %08x '%s' rank=%u group=%u", (unsigned)from, callsign, (unsigned)rank,
              (unsigned)group);
    if (canReply) {
        // Ответить: новому - представиться; тому, у кого нет времени, -
        // поделиться своим (только доверенным). Петли нет: получивший время
        // перестаёт быть «нуждающимся», дальше отвечаем только новым.
        uint32_t mine = getActTime();
        bool iHaveTime = mine > 1000000 && !timeStale;
        bool peerNeedsTime = (epoch <= 1000000);
        if (!peerNeedsTime && iHaveTime && epoch < mine && (mine - epoch) > 300)
            peerNeedsTime = true; // часы гостя отстали больше чем на 5 минут
        if (!known || (iHaveTime && peerNeedsTime))
            announceDossier(); // представиться в ответ (с rate-limit внутри)
    }
}

// ---------------- карта: точки ----------------

bool HorizonView::mapAllocId(char *out, size_t cap)
{
    // Свой номер - из радио, запасной - персист из конфига (до линка).
    uint32_t node = ownNode ? ownNode : HorizonConfig::instance().meshNode;
    if (!node || !out || cap < 16)
        return false;
    HorizonConfig &cfg = HorizonConfig::instance();
    for (int i = 0; i < 100; i++) {
        cfg.poiSeq++;
        if (cfg.poiSeq == 0)
            cfg.poiSeq = 1; // 0 не выдаём (оборот u16)
        snprintf(out, cap, "%u-%u", (unsigned)node, (unsigned)cfg.poiSeq);
        // Счётчик мог сброситься (вайп конфига) - занятые ID пропускаем.
        if (!mapStore.find(out)) {
            cfg.save();
            return true;
        }
    }
    return false; // всё занято (не должно случиться: пулы маленькие)
}

bool HorizonView::mapSendPoint(const HzPoi &p, uint32_t to)
{
    if (!radioConnected())
        return false;
    // Бюджет части - 220 байт: чанк подгоняем под шапку (имена - до 80 байт
    // каждое при эмодзи, поэтому фиксированные 96 годятся не всегда).
    // total/idx всегда одна цифра (≤8) - длина шапки точная заранее.
    char head[256];
    int headLen = snprintf(head, sizeof(head), "%s%s|1|1|%u|%s|%u|%u|%s|", kPoiPrefix, p.id, p.type,
                           p.name.c_str(), p.x, p.y, p.sender.c_str());
    if (headLen < 0)
        headLen = 0;
    size_t chunkMax = headLen < 204 ? (size_t)(220 - headLen) : 16;
    if (chunkMax > kPoiChunkMax)
        chunkMax = kPoiChunkMax;
    // Нарезать комментарий (хвост за 8 частями - отбросить, залогировать).
    const char *cm = p.comment.c_str();
    size_t off[8] = {0};
    uint8_t total = 0;
    size_t pos = 0, len = strlen(cm);
    while (pos < len && total < kPoiTotalMax) {
        size_t l = hzChunkLen(cm + pos, chunkMax);
        if (l == 0)
            l = 1;
        off[total++] = pos;
        pos += l;
    }
    if (total == 0)
        total = 1; // пустой комментарий - одна часть без чанка
    if (pos < len)
        ILOG_INFO("Horizon: POI '%s' comment tail dropped (%u parts cap)", p.id, (unsigned)total);
    uint8_t ch = p.ch > 7 ? HorizonConfig::instance().generalCh : p.ch;
    for (uint8_t i = 0; i < total; i++) {
        size_t start = off[i];
        size_t end = (i + 1 < total) ? off[i + 1] : pos;
        char chunk[kPoiChunkMax + 1];
        size_t cl = end - start;
        if (cl > kPoiChunkMax)
            cl = kPoiChunkMax;
        memcpy(chunk, cm + start, cl);
        chunk[cl] = '\0';
        char buf[256];
        snprintf(buf, sizeof(buf), "%s%s|%u|%u|%u|%s|%u|%u|%s|%s", kPoiPrefix, p.id, total, i + 1,
                 p.type, p.name.c_str(), p.x, p.y, p.sender.c_str(), chunk);
        if (to == 0xFFFFFFFFu)
            sendChannelRaw(ch, buf);
        else
            mapSendDmRaw(to, ch, buf);
    }
    ILOG_INFO("Horizon: POI '%s' sent (%u part(s))", p.id, (unsigned)total);
    return true;
}

bool HorizonView::mapSendDelete(const char *id, uint8_t ch)
{
    if (!radioConnected() || !id || !*id)
        return false;
    HorizonConfig &cfg = HorizonConfig::instance();
    char buf[64];
    snprintf(buf, sizeof(buf), "%s%s|%u|%u", kPoiDelPrefix, id, (unsigned)ownNode, (unsigned)cfg.rank);
    sendChannelRaw(ch > 7 ? cfg.generalCh : ch, buf);
    ILOG_INFO("Horizon: POID '%s' sent", id);
    return true;
}

bool HorizonView::mapSendPointDm(const HzPoi &p, uint32_t peer)
{
    if (!peer || peer == 0xFFFFFFFFu)
        return false;
    HzPoi out = p;
    out.ch = nodeChannel(peer); // как у чата с узлом; оригинал не изменяем
    return mapSendPoint(out, peer);
}

bool HorizonView::mapSendDmRaw(uint32_t peer, uint8_t ch, const char *text)
{
    if (!text || !*text || !radioConnected())
        return false;
    std::string s = text;
    truncate230(s);
    uint32_t reqId = requests.addRequest(peer, ResponseHandler::TextMessageRequest, nullptr, nullptr);
    controller->sendTextMessage(peer, ch, nodeHopLimit(peer), getActTime(), reqId, nodeHasKey(peer),
                                s.c_str());
    // ACK от пира сам снимет заявку (handleTextDelivery терпит чужие reqId).
    return true;
}

bool HorizonView::mapSendDeleteDm(const char *id, uint32_t peer)
{
    if (!radioConnected() || !id || !*id || !peer || peer == 0xFFFFFFFFu)
        return false;
    HorizonConfig &cfg = HorizonConfig::instance();
    char buf[64];
    snprintf(buf, sizeof(buf), "%s%s|%u|%u", kPoiDelPrefix, id, (unsigned)ownNode, (unsigned)cfg.rank);
    return mapSendDmRaw(peer, nodeChannel(peer), buf);
}

void HorizonView::mapDeletePoint(const char *id)
{
    if (!id || !*id)
        return;
    const HzPoi *p = mapStore.find(id);
    if (!p)
        return;
    // Запечённые канала не имеют (0xFF) - удаление в общий.
    uint8_t ch = p->ch > 7 ? HorizonConfig::instance().generalCh : p->ch;
    bool shared = p->shared;
    uint32_t peer = p->dmPeer;
    mapStore.removeEverywhere(id);
    if (!shared)
        return;
    if (peer)
        mapSendDeleteDm(id, peer); // best-effort: офлайн - только локально
    else
        mapSendDelete(id, ch); // best-effort: офлайн - только локально
}

void HorizonView::handlePoiBeacon(uint32_t from, uint8_t ch, const char *msg, bool canNotify)
{
    if (from == 0 || from == ownNode || !msg)
        return;
    // HZ:POI:<id>|<total>|<idx>|<type>|<name>|<x>|<y>|<sender>|<chunk>
    const char *p = msg + kPoiPrefixLen;
    const char *sep = strchr(p, '|');
    if (!sep || sep == p || (size_t)(sep - p) > 27)
        return;
    char id[28];
    memcpy(id, p, (size_t)(sep - p));
    id[sep - p] = '\0';
    if (!hzPoiIdOk(id))
        return;
    // Своё эхо через релей - мимо. ownNode==0 до my_info, а 0 - «автор»
    // запечённых: без гейта ownNode выкинем вообще всё.
    if (ownNode != 0 && hzPoiAuthorNode(id) == ownNode)
        return;
    // Обновлять точку может только её автор (запечённые Bxxx по эфиру
    // не обновляются вообще: их автор 0, никогда != from).
    if (hzPoiAuthorNode(id) != from)
        return;
    char *e1 = nullptr, *e2 = nullptr, *e3 = nullptr;
    unsigned long total = strtoul(sep + 1, &e1, 10);
    if (!e1 || *e1 != '|')
        return;
    unsigned long idx = strtoul(e1 + 1, &e2, 10);
    if (!e2 || *e2 != '|')
        return;
    unsigned long type = strtoul(e2 + 1, &e3, 10);
    if (!e3 || *e3 != '|')
        return;
    const char *name = e3 + 1;
    const char *s4 = strchr(name, '|');
    if (!s4 || s4 == name || (size_t)(s4 - name) > 128)
        return;
    char *e4 = nullptr, *e5 = nullptr;
    unsigned long x = strtoul(s4 + 1, &e4, 10);
    if (!e4 || *e4 != '|')
        return;
    unsigned long y = strtoul(e4 + 1, &e5, 10);
    if (!e5 || *e5 != '|')
        return;
    const char *sender = e5 + 1;
    const char *s6 = strchr(sender, '|');
    if (!s6 || s6 == sender || (size_t)(s6 - sender) > 128)
        return;
    const char *chunk = s6 + 1;
    if (total < 1 || total > kPoiTotalMax || idx < 1 || idx > total || type > 4 || x > 1000 || y > 1000)
        return;
    if (strchr(chunk, '\n') || strchr(chunk, '\r'))
        return; // многострочный мусор - мимо
    purgePoiRx(millis());
    HzPoiRx *rx = nullptr;
    for (HzPoiRx &r : poiRx_) {
        if (!strcmp(r.id, id)) {
            rx = &r;
            break;
        }
    }
    if (!rx) {
        // Сборки лежат по порядку добавления - первая самая старая.
        if (poiRx_.size() >= kPoiRxMax)
            poiRx_.erase(poiRx_.begin());
        HzPoiRx nr;
        strcpy(nr.id, id);
        strcpy(nr.head.id, id);
        nr.total = (uint8_t)total;
        nr.t0 = millis();
        nr.ch = ch > 7 ? 0 : ch;
        nr.head.type = (uint8_t)type;
        nr.head.name.assign(name, (size_t)(s4 - name));
        nr.head.x = (uint16_t)x;
        nr.head.y = (uint16_t)y;
        nr.head.sender.assign(sender, (size_t)(s6 - sender));
        nr.head.shared = true;
        nr.head.ch = nr.ch;
        poiRx_.push_back(nr);
        rx = &poiRx_.back();
    } else if (rx->total != (uint8_t)total) {
        return; // часть от другой версии точки - мимо
    }
    size_t ci = (size_t)(idx - 1);
    if (!rx->have[ci]) {
        rx->have[ci] = true;
        rx->got++;
    }
    rx->chunks[ci] = chunk;
    if (rx->got < rx->total)
        return; // ждём остальные части
    // Собралось: склеить, проверить длины в символах, положить.
    HzPoi poi = rx->head;
    for (uint8_t i = 0; i < rx->total; i++)
        poi.comment += rx->chunks[i];
    hzSanitizeField(&poi.name[0]);
    hzSanitizeField(&poi.sender[0]);
    hzSanitizeField(&poi.comment[0]);
    bool ok = !poi.name.empty() && hzUtf8Len(poi.name.c_str()) <= HzMapStore::kMaxNameChars &&
              hzUtf8Len(poi.sender.c_str()) <= 20 &&
              hzUtf8Len(poi.comment.c_str()) <= HzMapStore::kMaxCommentChars &&
              poi.comment.size() <= 768;
    // Сборщик - долой что на успехе, что на провале (битую версию не копим).
    for (auto it = poiRx_.begin(); it != poiRx_.end(); ++it) {
        if (!strcmp(it->id, id)) {
            poiRx_.erase(it);
            break;
        }
    }
    if (!ok) {
        ILOG_INFO("Horizon: POI '%s' dropped (bad lengths)", id);
        return;
    }
    if (!mapStore.find(id) && mapStore.sharedCount() >= HzMapStore::kMaxShared) {
        ILOG_INFO("Horizon: POI '%s' dropped (pool full)", id);
        return; // молча: тост на каждый чужой маяк - спам
    }
    mapStore.upsertNet(poi);
    mapStore.saveNet();
    ILOG_INFO("Horizon: POI '%s' stored (%u part(s))", id, (unsigned)total);
    if (canNotify && ui) {
        ui->toast("Точка: %s", poi.name.c_str());
        ui->mapOnNetUpdate();
    }
}

void HorizonView::handlePoiDelete(uint32_t from, const char *msg, bool canNotify)
{
    if (from == 0 || from == ownNode || !msg)
        return;
    // HZ:POID:<id>|<node>|<rank>
    const char *p = msg + kPoiDelPrefixLen;
    const char *s1 = strchr(p, '|');
    if (!s1 || s1 == p || (size_t)(s1 - p) > 27)
        return;
    char id[28];
    memcpy(id, p, (size_t)(s1 - p));
    id[s1 - p] = '\0';
    if (!hzPoiIdOk(id))
        return;
    char *e1 = nullptr, *e2 = nullptr;
    unsigned long node = strtoul(s1 + 1, &e1, 10);
    if (!e1 || *e1 != '|')
        return;
    unsigned long rank = strtoul(e1 + 1, &e2, 10);
    if (!e2 || (*e2 != '\0' && *e2 != '\r' && *e2 != '\n') || rank > 3)
        return;
    if ((uint32_t)node != from)
        return; // заявленный удаляющий - не отправитель: мимо
    // Права: автор точки или мастер (ранг 3). Нет в кэше досье - верим
    // заявленному рангу 3 (игра, задокументировано в docs/MAP.md §2.2).
    bool author = hzPoiAuthorNode(id) == from;
    bool master = false;
    if (rank == 3) {
        const HorizonDossierInfo *d = store.findDossier(from);
        master = !d || d->rank == 3;
    }
    if (!author && !master)
        return;
    if (!mapStore.find(id))
        return; // уже нет (или могила)
    mapStore.removeEverywhere(id);
    ILOG_INFO("Horizon: POID '%s' applied from %08x", id, (unsigned)from);
    if (canNotify && ui) {
        ui->toast("Точка удалена");
        ui->mapOnNetUpdate();
    }
}

void HorizonView::purgePoiRx(uint32_t now)
{
    for (auto it = poiRx_.begin(); it != poiRx_.end();) {
        if (now - it->t0 > kPoiRxTimeoutMs)
            it = poiRx_.erase(it);
        else
            ++it;
    }
}

void HorizonView::sendDirectMessage(uint32_t peer, const char *text)
{
    if (!text || !*text || peer == 0 || peer == 0xFFFFFFFFu)
        return;
    if (!radioConnected() && ui)
        ui->toast(HZ_NO_LINK);
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
    if (!radioConnected()) {
        if (ui)
            ui->toast(HZ_NO_LINK);
        return;
    }
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
    if (!radioConnected()) {
        if (ui)
            ui->toast(HZ_NO_LINK);
        return;
    }
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
    if (!radioConnected()) {
        if (ui)
            ui->toast(HZ_NO_LINK);
        return;
    }
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
    if (!radioConnected()) {
        if (ui)
            ui->toast(HZ_NO_LINK);
        return;
    }
    meshtastic_Config_DeviceConfig d = gotDevice ? device : meshtastic_Config_DeviceConfig{};
    if (role >= 0)
        d.role = (meshtastic_Config_DeviceConfig_Role)role;
    ILOG_INFO("Horizon: set device role=%d", role);
    controller->sendConfig(std::move(d), ownNode);
}

void HorizonView::silenceFirmwareBuzzer(void)
{
#ifdef HORIZON_PACKET_LINK
    // Зуммер GPIO8 принадлежит панели: прошивочный ExternalNotificationModule
    // иначе пищит RTTTL поверх наших мелодий и в обход бесшумного режима.
    // canBuzz() в прошивке смотрит только на buzzer_mode - его и гасим.
    if (buzzerSilenced || !radioConnected() || !gotDevice)
        return;
    if (device.buzzer_mode == meshtastic_Config_DeviceConfig_BuzzerMode_DISABLED) {
        buzzerSilenced = true;
        return; // уже тихо
    }
    meshtastic_Config_DeviceConfig d = device;
    d.buzzer_mode = meshtastic_Config_DeviceConfig_BuzzerMode_DISABLED;
    ILOG_INFO("Horizon: firmware buzzer off (panel owns GPIO8)");
    controller->sendConfig(std::move(d), ownNode);
    buzzerSilenced = true; // одна попытка за загрузку
#else
    (void)0;
#endif
}

void HorizonView::pushOwnerFromDossier(void)
{
    // Вызывается ТОЛЬКО когда пришла своя nodeinfo (см. upsertNodeUser).
    // set_owner в прошивке 2.8 планирует ребут при changed != 0, а вслепую
    // (без своей записи) changed почти наверняка != 0 (hw_model/is_licensed/
    // is_unmessagable нечем сравнить) - так и получался boot-loop.
    // Поэтому: толкаем лишь дефолтное заводское имя («Meshtastic ****»)
    // и лишь когда запись НАСТОЯЩАЯ (hw_model != UNSET, т.е. пришла по сети
    // ретранслированная своя nodeinfo, а не синтетическая заглушка базового
    // packetReceived). Всё остальное - только кнопкой «Применить к радио»
    // из досье (осознанный один ребут).
    if (ownerPushed || ownNode == 0)
        return;
    HorizonConfig &cfg = HorizonConfig::instance();
    if (!cfg.callsign[0])
        return; // досье не заполнено - радио не трогаем
    const NodeRecord *self = nodeStore.find(ownNode);
    if (!self || !self->hasUser || !self->user.long_name[0])
        return; // своей nodeinfo нет - НЕ ТРОГАТЬ радио вслепую
    if (self->user.hw_model == meshtastic_HardwareModel_UNSET)
        return; // синтетическая заглушка, настоящее имя неизвестно - ждать
    if (strncmp(self->user.long_name, "Meshtastic ", 11) != 0) {
        ownerPushed = true;
        return; // имя уже осмысленное - не дёргать радио
    }
    applyOwnerFromDossier(); // дефолт -> имя из досье, один раз
    ownerPushed = true;
}

void HorizonView::rebootNode(void)
{
    if (!radioConnected()) {
        if (ui)
            ui->toast(HZ_NO_LINK);
        return;
    }
    controller->requestReboot(3, ownNode);
}

void HorizonView::resetNodeDb(void)
{
    if (!radioConnected()) {
        if (ui)
            ui->toast(HZ_NO_LINK);
        return;
    }
    // Панель тоже забывает чужие узлы (живых радио перевыучит из эфира).
    std::vector<uint32_t> drop;
    for (auto &kv : nodeStore.records()) {
        if (kv.first != ownNode)
            drop.push_back(kv.first);
    }
    for (uint32_t n : drop)
        nodeStore.remove(n);
    nodesDirty = true;
    controller->requestReset(false, ownNode);
}

void HorizonView::factoryResetNode(void)
{
    if (!radioConnected()) {
        if (ui)
            ui->toast(HZ_NO_LINK);
        return;
    }
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
    // Отложенная перезапись лога после ACK: тяжёлая операция (весь файл),
    // не чаще раза в 10 с, чтобы не вешать интерфейс на каждой доставке.
    if (store.takeRewritePending()) {
        if (nowMs - lastMsgRewriteMs >= 10000) {
            store.rewriteMessages();
            lastMsgRewriteMs = nowMs;
        } else {
            store.setRewritePending(); // попробуем на следующем тике
        }
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
