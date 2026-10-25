#pragma once
#include "horizon_target.h" // целевые флаги сборки (вкус: standalone/прошивка)
// ПДА «Горизонт» - хранилище сообщений и диалогов: оперативная память +
// append-лог /horizon.msg (LittleFS). Своего отправленного в штатном
// /messages нет, поэтому без своего лога история умирала при перезагрузке.

#ifdef HORIZON_UI

#include <stdint.h>
#include <map>
#include <string>
#include <vector>

// Статус исходящего (HorizonMessage::sendStatus).
constexpr uint8_t kHzSendOk = 0;      // отправлено / входящее
constexpr uint8_t kHzSendPending = 1; // личка ждёт ACK
constexpr uint8_t kHzSendFailed = 2;  // не доставлено (красный пузырь)

struct HorizonMessage {
    uint32_t from = 0;
    uint32_t to = 0;
    uint8_t ch = 0;
    uint32_t time = 0;
    std::string text;
    bool outgoing = false; // отправлено с этого ПДА
    uint8_t sendStatus = kHzSendOk;
    uint32_t reqId = 0; // id запроса для сопоставления ACK (только pending)
};

// Диалог: либо канал (isChannel), либо личка с узлом peer.
struct HorizonConvo {
    bool isChannel = false;
    uint8_t ch = 0;       // для канала
    uint32_t peer = 0;    // для лички
    std::vector<HorizonMessage> messages;
    uint32_t unread = 0;
    uint32_t lastActivity = 0;
    uint32_t rev = 0; // счётчик изменений (гвард перестроений UI)
};

// Досье чужого игрока, узнанное из маяка «HZ:DOSSIER:» (см. HorizonView).
struct HorizonDossierInfo {
    char callsign[40] = "";
    uint8_t rank = 0;
    uint8_t group = 0;
    uint32_t updated = 0; // actTime получения (0 - неизвестно)
};

// Информация о канале радиомодуля
struct HorizonChannelInfo {
    bool known = false;
    bool disabled = true;
    bool primary = false;
    char name[16] = "";
    bool hasKey = false;  // PSK отличается от пустого
    uint8_t pskLen = 0;
};

class HorizonStore
{
  public:
    static constexpr uint8_t kMaxChannels = 8;
    static constexpr size_t kMaxPerConvo = 60;
    static constexpr size_t kMaxDmConvos = 20;
    static constexpr size_t kMaxDossierDir = 64;

    HorizonStore() = default;

    void setMyNode(uint32_t n); // + переразметить outgoing у загруженной истории
    uint32_t getMyNode(void) const { return myNode; }

    // Добавить сообщение (входящее/исходящее/восстановленное).
    // sendStatus/reqId - только для своих отправленных; persist=false -
    // переигрывание (загрузка файла, восстановление /messages): в лог
    // не пишет и дубли последнего сообщения пропускает.
    // Возвращает диалог, в который попало сообщение.
    HorizonConvo &addMessage(uint32_t from, uint32_t to, uint8_t ch, uint32_t time, const char *text,
                             bool countUnread, uint8_t sendStatus = kHzSendOk, uint32_t reqId = 0,
                             bool persist = true);

    // Доставка лички: отметить pending-сообщение с reqId (false - не найдено).
    bool markTextDelivered(uint32_t reqId, bool ok);
    const HorizonMessage *findTextByReq(uint32_t reqId) const;

    // Персист истории.
    void loadMessages(void);

    // Диалог канала (создаёт при необходимости)
    HorizonConvo &channelConvo(uint8_t ch);
    const HorizonConvo *findChannelConvo(uint8_t ch) const;

    // Личка с узлом (создаёт при необходимости)
    HorizonConvo &dmConvo(uint32_t peer);
    const HorizonConvo *findDmConvo(uint32_t peer) const;

    // Список личек, отсортированный по активности (новые сверху)
    std::vector<const HorizonConvo *> dmList(void) const;

    void markRead(HorizonConvo &c);
    void clearConvo(HorizonConvo &c);
    void clearAllDms(void);
    uint32_t maxDmTime(void) const; // макс. время в личках (метка для корзины)
    void clearAll(void); // лички + каналы (память;persisted чистит контроллер)
    void clearDossiers(void); // забыть чужие досье (память + файл)

    // Справочник досье (маяки): запомнить/найти по номеру узла.
    void upsertDossier(uint32_t node, const char *callsign, uint8_t rank, uint8_t group, uint32_t now);
    const HorizonDossierInfo *findDossier(uint32_t node) const;
    void saveDossier(void); // справочник в /horizon.dos (зовётся из upsert)
    void loadDossier(void); // справочник из /horizon.dos (раз при старте)

    uint32_t unreadTotal(void) const;
    uint32_t unreadChannels(void) const; // сумма по каналам
    uint32_t unreadDms(void) const;

    HorizonChannelInfo channels[kMaxChannels];

    uint32_t dmRev = 0; // счётчик изменений личек (гвард экрана открытых чатов)
    void setRewritePending(void) { rewritePending = true; }
    bool takeRewritePending(void) { bool v = rewritePending; rewritePending = false; return v; }

    void saveMessage(const HorizonMessage &m); // дописать одно сообщение в лог
    void rewriteMessages(void); // перезаписать лог из памяти (чистка/компакт)

  private:
    uint32_t myNode = 0;
    bool rewritePending = false; // ACK просит перезапись лога (см. pollTools)
    HorizonConvo chanConvos[kMaxChannels];
    std::vector<HorizonConvo> dms;
    std::map<uint32_t, HorizonDossierInfo> dossierDir;

    HorizonStore(const HorizonStore &) = delete;
    HorizonStore &operator=(const HorizonStore &) = delete;
};

#endif // HORIZON_UI
