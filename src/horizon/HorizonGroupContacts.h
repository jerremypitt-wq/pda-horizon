#pragma once
// Контакты, от которых видели декодированный пакет на текущем канале
// «Чат группировки». Чужую конфигурацию радио протокол не раскрывает:
// это подтверждение совместимого канала, не чтение удалённых настроек.
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

class HzGroupContacts
{
  public:
    // Смена локального канала, его имени/ID/ключа сбрасывает доказательства:
    // старые пакеты не должны делать контакт участником новой группировки.
    void configure(bool ready, uint8_t channel, const char *name, uint32_t id,
                   const uint8_t *key, size_t keyLen)
    {
        if (!ready) {
            clear();
            return;
        }
        std::string next;
        next.push_back((char)channel);
        for (size_t i = 0; name && i < 12 && name[i]; ++i)
            next.push_back(name[i]);
        next.push_back('\0');
        for (unsigned i = 0; i < 4; ++i)
            next.push_back((char)(id >> (i * 8)));
        next.push_back((char)keyLen);
        if (key && keyLen)
            next.append((const char *)key, keyLen);
        if (!active_ || next != signature_) {
            peers_.clear();
            signature_ = next;
        }
        active_ = true;
        channel_ = channel;
    }

    bool observe(uint32_t from, uint8_t channel, bool decoded)
    {
        if (!active_ || !decoded || from == 0 || channel != channel_ ||
            (peers_.size() >= 250 && !contains(from)))
            return false;
        return peers_.emplace(from, true).second;
    }

    bool contains(uint32_t node) const { return active_ && peers_.find(node) != peers_.end(); }
    void remove(uint32_t node) { peers_.erase(node); }
    void clear()
    {
        peers_.clear();
        signature_.clear();
        active_ = false;
    }

  private:
    std::string signature_; // только в RAM, никогда не сохраняется/логируется
    std::map<uint32_t, bool> peers_;
    uint8_t channel_ = 0;
    bool active_ = false;
};
