#pragma once
// ПДА «Горизонт» - хранилище сообщений и диалогов (оперативная память).
// Долговременное хранение делает ViewController (LittleFS /messages).

#ifdef HORIZON_UI

#include <stdint.h>
#include <map>
#include <string>
#include <vector>

struct HorizonMessage {
    uint32_t from = 0;
    uint32_t to = 0;
    uint8_t ch = 0;
    uint32_t time = 0;
    std::string text;
    bool outgoing = false; // отправлено с этого ПДА
};

// Диалог: либо канал (isChannel), либо личка с узлом peer.
struct HorizonConvo {
    bool isChannel = false;
    uint8_t ch = 0;       // для канала
    uint32_t peer = 0;    // для лички
    std::vector<HorizonMessage> messages;
    uint32_t unread = 0;
    uint32_t lastActivity = 0;
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

    void setMyNode(uint32_t n) { myNode = n; }
    uint32_t getMyNode(void) const { return myNode; }

    // Добавить сообщение (входящее/исходящее/восстановленное).
    // Возвращает диалог, в который попало сообщение.
    HorizonConvo &addMessage(uint32_t from, uint32_t to, uint8_t ch, uint32_t time, const char *text,
                             bool countUnread);

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
    void clearAll(void); // лички + каналы (память;persisted чистит контроллер)

    // Справочник досье (маяки): запомнить/найти по номеру узла.
    void upsertDossier(uint32_t node, const char *callsign, uint8_t rank, uint8_t group, uint32_t now);
    const HorizonDossierInfo *findDossier(uint32_t node) const;

    uint32_t unreadTotal(void) const;
    uint32_t unreadChannels(void) const; // сумма по каналам
    uint32_t unreadDms(void) const;

    HorizonChannelInfo channels[kMaxChannels];

  private:
    uint32_t myNode = 0;
    HorizonConvo chanConvos[kMaxChannels];
    std::vector<HorizonConvo> dms;
    std::map<uint32_t, HorizonDossierInfo> dossierDir;

    HorizonStore(const HorizonStore &) = delete;
    HorizonStore &operator=(const HorizonStore &) = delete;
};

#endif // HORIZON_UI
