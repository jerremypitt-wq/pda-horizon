#pragma once
#include "horizon_target.h" // целевые флаги сборки (вкус: standalone/прошивка)
// ПДА «Горизонт» - точки карты: данные, файлы, лимиты (см. docs/MAP.md).
#ifdef HORIZON_UI

#include <stdint.h>
#include <string>
#include <vector>

// Тип точки (индекс едет в эфир и в CSV). Порядок = docs/MAP.md §1.3.
enum HzPoiType : uint8_t {
    HZ_POI_SIGHT = 0, // точка интереса
    HZ_POI_BASE = 1,  // база
    HZ_POI_DANGER = 2, // опасность
    HZ_POI_ANOMALY = 3, // аномалия
    HZ_POI_CACHE = 4, // схрон
    HZ_POI_COUNT = 5,
};

// Одна точка. x/y - тысячные доли (0..1000), см. docs/MAP.md §1.1.
struct HzPoi {
    char id[28] = "";
    uint8_t type = 0;
    std::string name;
    uint16_t x = 0;
    uint16_t y = 0;
    std::string sender;
    std::string comment;
    bool shared = true;
    uint8_t ch = 0;       // канал, куда слалась точка (для удаления)
    uint32_t dmPeer = 0;  // получатель-узел (0 = всем в канал)
};

// Три пула точек: запечённые (RO из прошивки), принятые из эфира, свои.
// Свои с shared=1 входят в общий лимит вместе с запечёнными и принятыми.
class HzMapStore
{
  public:
    static constexpr size_t kMaxShared = 50;
    static constexpr size_t kMaxPriv = 10;
    static constexpr size_t kMaxNameChars = 20;
    static constexpr size_t kMaxCommentChars = 150;

    HzMapStore() = default;

    bool loaded = false;
    void loadAll(void); // всё из /hzmap (раз при старте)
    void saveMy(void);
    void saveNet(void);
    void saveDel(void);

    const std::vector<HzPoi> &baked(void) const { return baked_; }
    const std::vector<HzPoi> &net(void) const { return net_; }
    const std::vector<HzPoi> &my(void) const { return my_; }

    size_t sharedCount(void) const; // запечённые (минус могилы) + принятые + свои общие
    size_t privCount(void) const;   // свои личные
    // Указатели живы до следующей правки пулов - использовать сразу, не хранить.
    const HzPoi *find(const char *id) const; // везде (запечённые - кроме могил)
    HzPoi *findMut(const char *id);          // только свои (правка)
    bool isMine(const char *id) const;
    bool isTombstoned(const char *id) const;
    bool upsertNet(const HzPoi &p); // принять из эфира (true = новая, false = обновили)
    bool addMy(const HzPoi &p);     // false = пул полон (не добавлено)
    bool removeEverywhere(const char *id); // стереть отовсюду (+могила для запечённой)

  private:
    std::vector<HzPoi> baked_;
    std::vector<HzPoi> net_;
    std::vector<HzPoi> my_;
    std::vector<std::string> del_;

    HzMapStore(const HzMapStore &) = delete;
    HzMapStore &operator=(const HzMapStore &) = delete;
};

// --- Протокол и формат (docs/MAP.md раздел 2) ---
size_t hzUtf8Len(const char *s); // длина в символах (не в байтах)
void hzUtf8Trunc(char *dst, size_t dstBytes, const char *src, size_t maxChars); // урезать до N символов
size_t hzChunkLen(const char *s, size_t maxBytes); // байт чанка ≤ maxBytes по границе UTF-8
void hzSanitizeField(char *s); // '|' и переводы строк -> пробел (целы CSV и эфир)
uint32_t hzPoiAuthorNode(const char *id); // число до '-', у запечённых 0
bool hzPoiIdOk(const char *id); // 1..27 символов из [0-9A-Za-z-]

#endif // HORIZON_UI
