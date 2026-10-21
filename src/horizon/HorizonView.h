#pragma once
// ПДА «Горизонт» — вид (View) в MVC device-ui: принимает данные от радиомодуля
// через ViewController и отдаёт их интерфейсу HorizonUI.

#ifdef HORIZON_UI

#if defined(ARCH_ESP32)
#include <Arduino.h> // раньше lvgl.h: IRAM_ATTR для LV_ATTRIBUTE_FAST_MEM
#endif

#include "graphics/common/MeshtasticView.h"
#include "graphics/common/NodeStore.h"
#include "horizon/HorizonConfig.h"
#include "horizon/HorizonStore.h"
#include <string>

class HorizonUI;

class HorizonView : public MeshtasticView
{
  public:
    static HorizonView &instance(void);

    void init(IClientBase *client) override;
    void task_handler(void) override;

    // --- MeshtasticView overrides: данные от радиомодуля ---
    void setMyInfo(uint32_t nodeNum, meshtastic_MyNodeInfo_device_id_t device_id) override;
    void addOrUpdateNode(uint32_t nodeNum, uint8_t channel, uint32_t lastHeard, eRole role, bool hasKey,
                         bool viaMqtt) override;
    void addOrUpdateNode(uint32_t nodeNum, uint8_t channel, uint32_t lastHeard, const meshtastic_User &cfg) override;
    void updateNode(uint32_t nodeNum, uint8_t channel, const meshtastic_User &cfg) override;
    void updatePosition(uint32_t nodeNum, int32_t lat, int32_t lon, int32_t alt, uint32_t sats,
                        uint32_t precision) override;
    void updateMetrics(uint32_t nodeNum, const meshtastic_DeviceMetrics &metrics) override;
    void updateSignalStrength(uint32_t nodeNum, int32_t rssi, float snr) override;
    void updateHopsAway(uint32_t nodeNum, uint8_t hopsAway) override;
    void updateLastHeard(uint32_t nodeNum) override;
    void packetReceived(const meshtastic_MeshPacket &p) override;

    void newMessage(uint32_t from, uint32_t to, uint8_t ch, const char *msg, uint32_t &msgtime,
                    bool restore = false) override;
    void restoreMessage(const LogMessage &msg) override;

    void removeNode(uint32_t nodeNum) override;
    bool hasKnownNodeForPacket(uint32_t nodeNum) const override;

    void updateChannelConfig(const meshtastic_Channel &ch) override;
    void updateDeviceConfig(const meshtastic_Config_DeviceConfig &cfg) override;
    void updateLoRaConfig(const meshtastic_Config_LoRaConfig &cfg) override;

    void updateTime(uint32_t timeVal) override;
    void notifyConnected(const char *info) override;
    void notifyDisconnected(const char *info) override;
    void notifyMessagesRestored(void) override;
    void notifyResync(bool show) override;
    void notifyReboot(bool show) override;
    void notifyShutdown(void) override;

    // --- Доступ для интерфейса ---
    HorizonStore &getStore(void) { return store; }
    NodeStore &getNodes(void) { return nodeStore; }
    ViewController *getController(void) { return controller; }
    HorizonUI *getUI(void) { return ui; }

    uint32_t getActTime(void) const { return actTime; }
    bool radioConnected(void) const { return connected; }
    int myBattery(void) const { return battery; } // -1 — неизвестно

    bool bootReady(void) const; // можно уходить с заставки

    // Имена узлов
    const char *nodeLongName(uint32_t nodeNum) const;
    const char *nodeShortName(uint32_t nodeNum) const;
    bool isNodeOnline(uint32_t nodeNum) const;
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

    uint32_t actTime = 0;
    bool connected = false;
    int battery = -1;
    time_t bootTime = 0;

    bool nodesDirty = false;
    bool msgsDirty = false;
    bool channelsDirty = false;

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
