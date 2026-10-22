#pragma once
#include "horizon_target.h" // целевые флаги сборки (вкус: standalone/прошивка)
// ПДА «Горизонт» - вид (View) в MVC device-ui: принимает данные от радиомодуля
// через ViewController и отдаёт их интерфейсу HorizonUI.

#ifdef HORIZON_UI

#if defined(ARCH_ESP32) || defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h> // раньше lvgl.h: IRAM_ATTR для LV_ATTRIBUTE_FAST_MEM
#endif

#include "graphics/common/MeshtasticView.h"
#include "graphics/common/NodeStore.h"
#include "horizon/HorizonConfig.h"
#include "horizon/HorizonStore.h"
#include <string>
#include <vector>

class HorizonUI;

// Счётчики UART-трафика панель<-радио (инкрементятся патчем libdeps
// bin/patch_deviceui_uartmon.py; без патча остаются нулями). Монотонные
// 32-бит: чтение из UI при записи из serial-таска безопасно.
extern volatile uint32_t hzUartRxBytes;  // сырых байт с провода
extern volatile uint32_t hzUartRxFrames; // валидных кадров FromRadio

class HorizonView : public MeshtasticView
{
  public:
    static HorizonView &instance(void);

    void init(IClientBase *client) override;
    void task_handler(void) override;

    // --- MeshtasticView overrides: данные от радиомодуля ---
#ifdef HORIZON_PACKET_LINK
    // Апстримный MeshtasticView (прошивка): плоская сигнатура, device_id у нас
    // всё равно не использовался.
    void setMyInfo(uint32_t nodeNum) override;
#else
    void setMyInfo(uint32_t nodeNum, meshtastic_MyNodeInfo_device_id_t device_id) override;
#endif
    void addOrUpdateNode(uint32_t nodeNum, uint8_t channel, uint32_t lastHeard, eRole role, bool hasKey,
                         bool viaMqtt) override;
    void addOrUpdateNode(uint32_t nodeNum, uint8_t channel, uint32_t lastHeard, const meshtastic_User &cfg) override;
    void updateNode(uint32_t nodeNum, uint8_t channel, const meshtastic_User &cfg) override;
    void updatePosition(uint32_t nodeNum, int32_t lat, int32_t lon, int32_t alt, uint32_t sats,
                        uint32_t precision) override;
#ifdef HORIZON_PACKET_LINK
    // Апстрим шлёт метрики плоским набором (ViewController: node_info и телеметрия):
    // собираем структуру и отдаём рабочему варианту ниже.
    void updateMetrics(uint32_t nodeNum, uint32_t bat_level, float voltage, float chUtil, float airUtil) override;
    void updateMetrics(uint32_t nodeNum, const meshtastic_DeviceMetrics &metrics);
#else
    void updateMetrics(uint32_t nodeNum, const meshtastic_DeviceMetrics &metrics) override;
#endif
    void updateSignalStrength(uint32_t nodeNum, int32_t rssi, float snr) override;
    void updateHopsAway(uint32_t nodeNum, uint8_t hopsAway) override;
    void updateLastHeard(uint32_t nodeNum) override;
    void packetReceived(const meshtastic_MeshPacket &p) override;

    void newMessage(uint32_t from, uint32_t to, uint8_t ch, const char *msg, uint32_t &msgtime,
                    bool restore = false) override;
    void restoreMessage(const LogMessage &msg) override;

    void removeNode(uint32_t nodeNum) override;
#ifndef HORIZON_PACKET_LINK
    // Только форк: апстримный базовый класс этого метода не объявляет.
    bool hasKnownNodeForPacket(uint32_t nodeNum) const override;
#endif

    void updateChannelConfig(const meshtastic_Channel &ch) override;
    void updateDeviceConfig(const meshtastic_Config_DeviceConfig &cfg) override;
    void updateLoRaConfig(const meshtastic_Config_LoRaConfig &cfg) override;

    void handleResponse(uint32_t from, uint32_t id, const meshtastic_Routing &routing,
                        const meshtastic_MeshPacket &p) override;
    void handleResponse(uint32_t from, uint32_t id, const meshtastic_RouteDiscovery &route) override;
    void handlePositionResponse(uint32_t from, uint32_t request_id, int32_t rx_rssi, float rx_snr,
                                bool isNeighbor) override;

    void updateTime(uint32_t timeVal) override;
    void notifyConnected(const char *info) override;
    void notifyDisconnected(const char *info) override;
    void notifyMessagesRestored(void) override;
    void notifyResync(bool show) override;
    void notifyReboot(bool show) override;
    void notifyShutdown(void) override;

    // --- Инструменты Mesh: лог пакетов ---
    struct HzPacketEntry {
        uint32_t time = 0;
        uint32_t from = 0;
        uint32_t to = 0;
        uint8_t ch = 0;
        uint16_t port = 0;
        uint16_t size = 0;
        int16_t rssi = 0;
        float snr = 0;
    };
    static constexpr size_t kPacketLogMax = 30;
    const std::vector<HzPacketEntry> &packetLog(void) const { return pktLog; }
    void clearPacketLog(void)
    {
        pktLog.clear();
        toolsDirty = true;
    }
    // --- Инструменты Mesh: статистика по узлам ---
    struct HzNodeStats {
        uint32_t node = 0;
        uint16_t txt = 0;
        uint16_t pos = 0;
        uint16_t inf = 0;
        uint16_t trc = 0;
        uint16_t tel = 0;
        uint16_t nbr = 0;
        uint32_t sum = 0;
    };
    std::vector<HzNodeStats> nodeStatsSorted(void) const;
    void clearNodeStats(void);
    uint32_t statsTotal(void) const { return statsTotal_; }
    // --- Инструменты Mesh: сканер сигналов (состояние читает интерфейс) ---
    bool scanRunning = false;
    uint32_t scanNode = 0;
    uint8_t scanCount = 0; // отправлено замеров
    uint8_t scanWant = 8;  // всего замеров
    bool scanGot = false;  // был хоть один ответ
    int32_t scanRssi = 0;
    float scanSnr = 0;
    bool scanDirect = false; // ответ пришёл напрямую (сосед)
    void startScan(uint32_t node);
    void stopScan(void);
    // --- Инструменты Mesh: трассировка маршрута ---
    struct HzTraceResult {
        bool valid = false;
        uint32_t fwd[8] = {0};
        uint8_t fwdN = 0;
        uint32_t back[8] = {0};
        uint8_t backN = 0;
    };
    bool traceRunning = false;
    uint32_t traceNode = 0;
    uint32_t traceStartMs = 0;
    bool traceGot = false;
    bool traceFail = false;
    HzTraceResult trace;
    void startTrace(uint32_t node);
    void stopTrace(void);
    void pollTools(uint32_t nowMs); // темп сканера + таймаут маршрута (из UI tick)
    bool takeToolsDirty(void);

    // --- Доступ для интерфейса ---
    HorizonStore &getStore(void) { return store; }
    NodeStore &getNodes(void) { return nodeStore; }
    ViewController *getController(void) { return controller; }
    HorizonUI *getUI(void) { return ui; }

    // Текущее время: между синхронизациями идёт по millis() от последней
    // известной метки (RTC на плате нет - это стандартный подход для ESP32).
    uint32_t getActTime(void) const;
    bool timeUncertain(void) const { return timeStale; } // время стоит со выключения, сеть ещё не уточнила
    void persistTime(void);             // сохранить actTime в /horizon.time (если валидно)
    void setManualTime(uint32_t epoch); // ручная установка времени из настроек
    // Жив ли радиомодуль: открытого UART мало (Serial-клиент считает себя
    // подключённым сразу после init), доказательство - пришедший my_info.
    // Без этого индикатор MESH врёт при мёртвой линии панель-радио.
    bool radioConnected(void) const { return connected && ownNode != 0; }
    int myBattery(void) const { return battery; } // -1 - неизвестно

    bool bootReady(void) const; // можно уходить с заставки

    // Имена узлов
    const char *nodeLongName(uint32_t nodeNum) const;
    const char *nodeShortName(uint32_t nodeNum) const;
    // Единое отображаемое имя узла везде в интерфейсе: позывной из досье,
    // иначе длинное имя, иначе короткое, иначе «!abcd».
    const char *displayName(uint32_t nodeNum) const;
    bool isNodeOnline(uint32_t nodeNum) const;
    bool isNodeHeardWithin(uint32_t nodeNum, uint32_t windowSec) const; // слышали не раньше windowSec назад
    uint32_t onlineCount(void) const;
    uint32_t nodeCount(void) const;

    // Каналы радиомодуля
    const meshtastic_Channel *radioChannel(uint8_t idx) const;
    const char *channelLabel(uint8_t idx) const; // имя или "Канал N"
    bool channelIsLocked(uint8_t idx) const;     // есть PSK

    bool hasLoRaConfig(void) const { return gotLoRa; }
    bool hasDeviceConfig(void) const { return gotDevice; }
    const meshtastic_Config_LoRaConfig &loraConfig(void) const { return lora; }
    const meshtastic_Config_DeviceConfig &deviceConfig(void) const { return device; }

    // Флаги изменений для перерисовки
    bool takeNodesDirty(void);
    bool takeMsgsDirty(void);
    bool takeChannelsDirty(void);
    void markMsgsDirty(void) { msgsDirty = true; }

    // --- Действия (интерфейс -> радиомодуль) ---
    void sendChannelMessage(uint8_t ch, const char *text);
    void sendChannelRaw(uint8_t ch, const char *text); // без записи в локальную историю (маяки)
    void announceDossier(void);                        // маяк «HZ:DOSSIER:» в общий канал
    void sendDirectMessage(uint32_t peer, const char *text);
    void applyOwnerFromDossier(void);
    // simpleKey: 0=без ключа, 1=default, 2..11=simple1..10, 0xFF=не менять
    void applyChannelSettings(uint8_t idx, const char *name, uint8_t simpleKey);
    void applyLoRaConfig(int region, int preset, uint32_t hopLimit, bool txEnabled);
    void applyDeviceRole(int role);
    // Прошивочный зуммер выкл: GPIO8 принадлежит панели (тон - наш).
    // Раз в загрузку, только вкус прошивки (HORIZON_PACKET_LINK).
    void silenceFirmwareBuzzer(void);
    // Владелец радио из досье без лишних вопросов (раз в загрузку).
    void pushOwnerFromDossier(void);
    void rebootNode(void);
    void resetNodeDb(void);
    void factoryResetNode(void);

  protected:
    HorizonView(const DisplayDriverConfig *cfg, DisplayDriver *driver);

  private:
    static HorizonView *gui;

    HorizonUI *ui = nullptr;
    NodeStore nodeStore;
    HorizonStore store;

    meshtastic_Channel radioCh[c_max_channels];
    bool radioChKnown[c_max_channels] = {false};
    meshtastic_Config_LoRaConfig lora{};
    meshtastic_Config_DeviceConfig device{};
    bool gotLoRa = false;
    bool gotDevice = false;

    uint32_t actTime = 0;   // последняя известная метка (epoch, UTC)
    uint32_t actTimeMs = 0; // millis() в момент установки метки
    bool timeStale = false; // восстановлено из файла: простой при выключении неизвестен
    void setActTime(uint32_t t); // обновить метку (штампует и millis)
    bool connected = false;
    bool buzzerSilenced = false; // прошивочный зуммер уже гасили в этой загрузке
    bool ownerPushed = false;    // владельца уже толкали в радио в этой загрузке
    int battery = -1;
    time_t bootTime = 0;

    bool nodesDirty = false;
    bool msgsDirty = false;
    bool channelsDirty = false;
    bool toolsDirty = false;

    std::vector<HzPacketEntry> pktLog;
    std::vector<HzNodeStats> nodeStats_;
    uint32_t statsTotal_ = 0;
    uint32_t scanReq = 0;
    uint32_t scanNextMs = 0;
    uint32_t traceReq = 0;
    // Личка, ждущая ACK (резолв в handleResponse или по таймауту в pollTools).
    struct HzPendingText {
        uint32_t reqId = 0;
        uint32_t sentMs = 0;
    };
    std::vector<HzPendingText> pendingTexts;

    void notePacketTools(const meshtastic_MeshPacket &p); // лог+статистика из packetReceived
    void handleTextDelivery(uint32_t from, uint32_t id, const meshtastic_Routing &routing);

    void upsertNodeUser(uint32_t nodeNum, uint8_t channel, uint32_t lastHeard, const meshtastic_User &cfg,
                        bool viaMqtt);
    // Разобрать маяк досье (canReply=false для восстановленных из истории)
    void handleDossierBeacon(uint32_t from, const char *msg, bool canReply);
    uint32_t lastBeaconMs = 0; // rate-limit маяков
    uint8_t nodeChannel(uint32_t nodeNum) const;
    bool nodeHasKey(uint32_t nodeNum) const;
    uint8_t nodeHopLimit(uint32_t nodeNum) const;

    HorizonView(const HorizonView &) = delete;
    HorizonView &operator=(const HorizonView &) = delete;
};

#endif // HORIZON_UI
