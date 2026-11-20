#pragma once
#include "horizon_target.h" // целевые флаги сборки (вкус: standalone/прошивка)
// ПДА «Горизонт» - интерфейс на LVGL: меню-плитки, чаты, контакты, досье, настройки.

#ifdef HORIZON_UI

#if defined(ARCH_ESP32) || defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h> // раньше lvgl.h: IRAM_ATTR для LV_ATTRIBUTE_FAST_MEM
#endif

#include "lvgl.h"
#include <functional>
#include <stdint.h>
#include <string>

class HorizonView;

struct HzMapUi; // состояние экрана карты (определено в HorizonMapUI.cpp)

class HorizonUI
{
  public:
    explicit HorizonUI(HorizonView *view);

    void init(void); // построить заставку
    void tick(void); // периодика (вызывать из task_handler)
    void onMessage(uint32_t from, uint32_t to, uint8_t ch, const char *text, uint32_t msgtime);

    void toast(const char *fmt, ...);
    static void fmtTime(uint32_t t, char *out, size_t cap); // HH:MM (публично: тост синхронизации)
    static void fmtDate(uint32_t t, char *out, size_t cap); // DD.MM.YYYY (публично: тост синхронизации)

    // Навигация
    void showMenu(void);
    void showChatChannel(uint8_t ch);
    void showChatDm(uint32_t peer);
    void showContacts(void);
    void showOpenChats(void);
    void showDossier(void);
    void showSettings(void);
    void showMaster(void); // раздел мастера (вход по PIN из настроек)
    void showMap(void);
    void mapOnNetUpdate(void); // точка прилетела/удалилась: обновить маркеры
    void onMapPoint(const char *id, const char *name); // кликабельное уведомление о точке
    void mapOpenPoint(const char *id); // переход из чата/уведомления, центр + импульсы

  private:
    enum class Screen : uint8_t {
        BOOT,
        LOCK,
        MENU,
        CHAT,
        CONTACTS,
        CONTACT_CARD,
        OPENCHATS,
        DOSSIER,
        DOSSIER_EDIT,
        SETTINGS,
        SET_CHANNELS,
        SET_CHANNEL_DETAIL,
        SET_DEVICE,
        SET_DISPLAY,
        SET_PIN,
        SET_RESET,
        SET_ABOUT,
        SET_TIME,
        SET_SOUND,
        SET_SECURITY,
        SET_BATTERY,
        SET_TOOLS,
        SET_MASTER,
        TOOL_DETECTOR,
        TOOL_PICK,
        TOOL_SCAN,
        TOOL_ROUTE,
        TOOL_STATS,
        TOOL_LOG,
        MAP,
        MAP_FORM,
    };

    HorizonView *view = nullptr;
    Screen current = Screen::BOOT;
    lv_obj_t *scr = nullptr;

    // Геометрия
    int16_t W(void);
    int16_t H(void);
    bool isPortrait(void);

    // --- Каркас экрана ---
    void clearScreen(Screen s);
    lv_obj_t *addStatusBar(lv_obj_t *parent); // тонкая верхняя строка
    lv_obj_t *addNavHeader(lv_obj_t *parent, const char *title, const char *sub);
    lv_obj_t *addFooterHint(lv_obj_t *parent, const char *text);
    void refreshStatus(void);

    lv_obj_t *stTime = nullptr;
    lv_obj_t *stOnline = nullptr;
    lv_obj_t *stUnread = nullptr;
    lv_obj_t *stBatt = nullptr;
    lv_obj_t *stLink = nullptr;

    // --- Заставка ---
    void showBoot(void);
    void showLock(void); // полный экран без статуса/уведомлений
    lv_obj_t *bootBar = nullptr;
    lv_obj_t *bootStatus = nullptr;
    uint32_t bootMs = 0;

    // --- Меню ---
    void buildMenu(void);
    lv_obj_t *menuBadges[7] = {nullptr};
    void refreshMenuBadges(void);

    // --- Чат ---
    bool chatIsChannel = true;
    uint8_t chatCh = 0;
    uint32_t chatPeer = 0;
    void buildChat(void);
    void rebuildChatList(void);
    void appendChatBubble(uint32_t from, const char *text, uint32_t time, bool outgoing, bool failed = false,
                          bool deferLayout = false);
    void sendChatInput(void);
    lv_obj_t *chatList = nullptr;
    lv_obj_t *chatInput = nullptr;
    lv_obj_t *chatSub = nullptr;
    lv_obj_t *chatSendBtn = nullptr;

    // --- Контакты ---
    // Фильтр списка: 0 - все, 1 - в сети, 2 - группировка, 3 - избранное.
    uint8_t contactsFilter = 0;
    uint32_t cardNode = 0; // узел, открытый в карточке контакта
    bool mapPickMode = false;  // контакты открыты выбором получателя точки
    uint32_t mapPickPeer = 0;  // выбранный получатель (форма точки)
    void buildContacts(void);
    void rebuildContactsList(void);
    void showContactCard(uint32_t node);
    void buildContactCard(void);
    lv_obj_t *contactsList = nullptr;
    lv_obj_t *contactsFavLabel = nullptr; // подпись кнопки избранного со счётчиком

    // --- Открытые чаты ---
    void buildOpenChats(void);

    // --- Досье ---
    void buildDossier(void);
    void buildDossierEdit(void);
    lv_obj_t *editName = nullptr;
    lv_obj_t *editCall = nullptr;
    lv_obj_t *editRank = nullptr;
    lv_obj_t *editGroup = nullptr;
    void saveDossier(void);

    // --- Настройки ---
    void buildSettings(void);
    void buildSetChannels(void);
    void buildSetChannelDetail(void);
    void buildSetDevice(void);
    void buildSetDisplay(void);
    void buildSetPin(void);
    void buildSetSecurity(void);
    void requestUserPin(void);
    void buildSetReset(void);
    void wipeLocalData(bool full); // сброс локального: конфиг+чаты (+досье при full)
    void buildSetAbout(void);
    void buildSetTime(void);
    void buildSetSound(void);
    void buildSetBattery(void);
    void refreshBatteryInfo(void);
    void buildSetTools(void);
    void buildSetMaster(void);

    // --- Инструменты Mesh ---
    void buildToolDetector(void);
    void buildToolPick(void); // выбор узла для сканера/маршрута (toolPickMode)
    void buildToolScan(void);
    void buildToolRoute(void);
    void buildToolStats(void);
    void buildToolLog(void);
    void refreshTools(void); // тик-обновление открытого инструмента
    static bool fingerDown(void); // палец сейчас на экране (опрос indev напрямую, не get_act)
    static uint32_t nowMs(void); // lv_tick_get (единые часы UI)
    lv_obj_t *toolBody(void); // тело экрана инструмента (под шапкой)
    void refreshDetector(void);
    void refreshScan(void);
    void refreshRoute(void);
    void refreshStats(void);
    void refreshLog(void);

    lv_obj_t *rowButton(lv_obj_t *parent, const char *title, const char *sub, int id);
    lv_obj_t *sectionLabel(lv_obj_t *parent, const char *text);
    lv_obj_t *channelDropdown(lv_obj_t *parent, uint8_t selected);
    lv_obj_t *simpleKeyDropdown(lv_obj_t *parent, uint8_t selected);

    // состояние экранов настроек
    lv_obj_t *setRegion = nullptr;
    lv_obj_t *setPreset = nullptr;
    lv_obj_t *setHops = nullptr;
    lv_obj_t *setRole = nullptr;
    lv_obj_t *setTx = nullptr;
    lv_obj_t *setBattInfo = nullptr;
    lv_obj_t *setBrightness = nullptr;
    lv_obj_t *setBrightnessVal = nullptr;
    lv_obj_t *setTimeout = nullptr;
    lv_obj_t *setOrientation = nullptr;
    lv_obj_t *setTheme = nullptr;
    // Детали канала: 0=общий, 1=групповой, 2=объявления
    uint8_t setDetailKind = 0;
    lv_obj_t *setDetailCh = nullptr;
    lv_obj_t *setDetailName = nullptr;
    lv_obj_t *setDetailKey = nullptr;
    bool displayDirty = false;
    // Каналы: были ли реальные изменения имени/ключа на радио с момента
    // последнего вопроса про перезагрузку (спрашиваем только на выходе).
    bool channelsDirty = false;
    // Время: черновик часов/минут + указатели экрана
    int timeH = 12;
    int timeM = 0;
    int timeD = 1;
    int timeMo = 1;
    int timeY = 2026;
    lv_obj_t *timeHVal = nullptr;
    lv_obj_t *timeMVal = nullptr;
    lv_obj_t *timeDVal = nullptr;
    lv_obj_t *timeMoVal = nullptr;
    lv_obj_t *timeYVal = nullptr;
    lv_obj_t *timeCur = nullptr;
    uint32_t lastTimeSaveMs = 0;
    // Инструменты Mesh: состояние экранов
    uint8_t toolPickMode = 0; // 0 - сканер, 1 - маршрут
    lv_obj_t *detList = nullptr;
    lv_obj_t *detSub = nullptr;
    lv_obj_t *scanBig = nullptr;
    lv_obj_t *scanPct = nullptr;
    lv_obj_t *scanProg = nullptr;
    lv_obj_t *routeStatus = nullptr;
    lv_obj_t *routeBox = nullptr;
    lv_obj_t *statsList = nullptr;
    lv_obj_t *statsSub = nullptr;
    lv_obj_t *logList = nullptr;
    // --- Карта ---
    HzMapUi *mapUi = nullptr; // состояние (буферы живут между MAP и MAP_FORM)
    void buildMap(void);
    void buildMapForm(void);
    void mapDropWidgets(void); // занулить указатели виджетов (экрана больше нет)
    void mapCleanup(void);     // освободить буферы и состояние
    void mapRefreshMarkers(void);
    void mapTap(int lx, int ly); // тап по виду (координаты вида, px)
    void mapOpenPopup(const char *id);
    void mapClosePopup(void);
    void mapSaveForm(void);
    void mapSyncFormRows(void); // строки формы следуют за списками (из tick)
    void mapPlaceTag(void); // бирка с именем выбранной точки
    void mapPulseTick(uint32_t now); // пять колец с шагом 0.5 с
    void mapToggleFilter(void);
    void mapBuildFilterPanel(void);

    // --- Тост ---
    lv_obj_t *toastBtn = nullptr;
    lv_obj_t *toastLabel = nullptr;
    uint32_t toastUntil = 0;
    bool toastIsChannel = true;
    uint8_t toastCh = 0;
    uint32_t toastPeer = 0;
    bool toastHasTarget = false;
    char toastMapId[28] = ""; // если задан, тап по уведомлению открывает карту
    char pendingMapId[28] = ""; // ссылка могла приехать раньше частей точки

    // --- Модальные окна ---
    lv_obj_t *modal = nullptr;
    void closeModal(void);
    // PIN-диалог
    std::string pinEntry;
    lv_obj_t *pinDots = nullptr;
    std::function<void(void)> pinOk;
    std::function<void(std::string)> pinSubmit; // установка нового PIN, без проверки старого
    bool pinAllowUser = false;
    uint8_t pinMaxLength = 4;
    uint8_t pinFailures = 0;
    uint32_t pinBlockedUntil = 0; // локальная задержка после пяти ошибочных попыток
    void askPin(const char *prompt, std::function<void(void)> onOk, bool allowUser = false);
    void askPinEntry(const char *prompt, std::function<void(std::string)> onSubmit);
    void pinPress(const char *key);
    // Подтверждение
    std::function<void(void)> confirmOk;
    std::function<void(void)> confirmCancel;
    void askConfirm(const char *text, const char *okLabel, std::function<void(void)> onOk,
                    std::function<void(void)> onCancel = nullptr);

    // --- Экранная клавиатура (своя, RU/EN/123) ---
    lv_obj_t *kbd = nullptr;       // панель с матрицей
    lv_obj_t *kbdMatrix = nullptr; // buttonmatrix
    lv_obj_t *kbdTarget = nullptr; // textarea
    uint8_t kbdLayout = 1;         // 0=EN 1=RU 2=123 (по умолчанию RU)
    uint8_t kbdPrevAlpha = 1;      // куда возвращает АБВ (0=EN 1=RU)
    bool kbdShift = false;         // Shift разового действия (только EN/RU)
    bool kbdCaps = false;          // CapsLock: двойной тап по Аа (сброс - тап по Аа)
    uint32_t kbdAaLastMs = 0;      // время последнего тапа по Аа (детект двойного)
    static constexpr uint32_t kKbdDoubleTapMs = 400; // окно двойного тапа, мс
    bool kbdSuppressClick = false; // долгое нажатие уже вставило символ
    void kbdShow(lv_obj_t *ta);
    void kbdHide(void);
    bool kbdVisible(void) const;
    void kbdSetLayout(uint8_t layout);
    void kbdPress(const char *txt);

    // --- Охранник сна: съедает первое нажатие после засыпания ---
    lv_obj_t *sleepGuard = nullptr;
    void showSleepGuard(void);
    void hideSleepGuard(void);

    // --- Утилиты ---
    void formatSender(uint32_t node, char *out, size_t cap); // «позывной, ранг» или имя узла
    static void fmtLastHeard(HorizonView *v, uint32_t lh, char *out, size_t cap);
    uint32_t visibleUnread(void);

    // tick-состояние
    uint32_t lastTickMs = 0;
    uint32_t lastTouchMs = 0; // также обновляется при удержании; после отпускания таймер начинается заново
    uint32_t lastSlowMs = 0;
    uint32_t lastContactsMs = 0;
    uint32_t lastDetectorMs = 0;
    uint32_t chatRevShown = 0; // ревизия открытого диалога, отрисованная в списке
    uint32_t openRevShown = 0; // ревизия личек, отрисованная в открытых чатах

    // статические колбэки LVGL
    static void cbTile(lv_event_t *e);
    static void cbBack(lv_event_t *e);
    static void cbMasterRow(lv_event_t *e);
    static void cbRow(lv_event_t *e);
    static void cbSend(lv_event_t *e);
    static void cbInputFocus(lv_event_t *e);
    static void cbKbd(lv_event_t *e);
    static void cbKbdLong(lv_event_t *e);
    static void cbSleepWake(lv_event_t *e);
    static void cbToast(lv_event_t *e);
    static void cbPinBtn(lv_event_t *e);
    static void cbConfirmOk(lv_event_t *e);
    static void cbConfirmCancel(lv_event_t *e);
    static void cbModalBlock(lv_event_t *e);
    static void cbContact(lv_event_t *e);
    static void cbContactGo(lv_event_t *e); // кнопка ">" в строке контакта
    static void cbCardWrite(lv_event_t *e);
    static void cbCardFav(lv_event_t *e);
    static void cbOpenChat(lv_event_t *e);
    static void cbDossierEdit(lv_event_t *e);
    static void cbDossierSave(lv_event_t *e);
    static void cbSetChannelPick(lv_event_t *e);
    static void cbSetApplyChannelDetail(lv_event_t *e);
    static void cbSetApplyDevice(lv_event_t *e);
    static void cbSetApplyDisplay(lv_event_t *e);
    static void cbSetApplyPin(lv_event_t *e);
    static void cbSecurityRow(lv_event_t *e);
    static void cbSetReset(lv_event_t *e);
    static void cbBrightness(lv_event_t *e);
    static void cbThemeChanged(lv_event_t *e);
    static void cbFilter(lv_event_t *e);
    static void cbClearChats(lv_event_t *e);
    static void cbRebootNow(lv_event_t *e);
    static void cbTimeStep(lv_event_t *e);
    static void cbTimeApply(lv_event_t *e);
    static void cbSoundToggle(lv_event_t *e);
    static void cbSoundTest(lv_event_t *e);
    static void cbSoundSet(lv_event_t *e);
    static void cbSoundVolume(lv_event_t *e);
    static void cbToolRow(lv_event_t *e);
    static void cbToolPick(lv_event_t *e);
    static void cbToolPickGo(lv_event_t *e); // кнопка ">" в строке выбора узла
    static void cbScanStop(lv_event_t *e);
    static void cbTraceStop(lv_event_t *e);
    static void cbStatsClear(lv_event_t *e);
    static void cbLogClear(lv_event_t *e);
    static void cbMapTouch(lv_event_t *e);
    static void cbMapTabs(lv_event_t *e);
    static void cbMapTabSelect(lv_event_t *e);
    static void cbMapZoom(lv_event_t *e);
    static void cbMapPopup(lv_event_t *e);
    static void cbMapFormSave(lv_event_t *e);
    static void cbMapChatLink(lv_event_t *e);
    static void cbMapPeerPick(lv_event_t *e); // строка контакта: выбор получателя
    static void cbMapFilter(lv_event_t *e);
    static void cbMapFilterRow(lv_event_t *e);
    static void cbMapFilterReset(lv_event_t *e);

    HorizonUI(const HorizonUI &) = delete;
    HorizonUI &operator=(const HorizonUI &) = delete;
};

#endif // HORIZON_UI
