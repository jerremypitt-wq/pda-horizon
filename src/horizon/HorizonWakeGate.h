#pragma once
// Кандидат на пробуждение экрана: проверяется ДО передачи касания в LVGL.
// Не зависит от дисплея/радио; время в мс (разность выдерживает оборот millis()).
#include <stdint.h>

class HorizonWakeGate
{
  public:
    static constexpr uint32_t kMinTapMs = 35;
    static constexpr uint32_t kMaxTapMs = 250;
    static constexpr uint32_t kQuietMs = 300; // после хвата подождать, пока рука уйдёт
    static constexpr uint32_t kReleaseMs = 40; // два опроса без пальцев: отсечь краткий провал GT911
    static constexpr int32_t kMaxMovePx = 14;

    void reset(void) { state = State::Idle; }

    // true выдаётся ровно один раз, только на отпускании одиночного тапа.
    // 2+ пальца/долгое касание/движение блокируют ВСЮ попытку до отпускания.
    bool sample(uint32_t now, uint8_t fingers, int32_t x = 0, int32_t y = 0)
    {
        if (state == State::Waking)
            return false;
        if (state == State::Rejected) {
            if (fingers == 0) {
                state = State::Quiet;
                quietFrom = now;
            }
            return false;
        }
        if (state == State::Quiet) {
            if (fingers) {
                quietFrom = now; // нужна непрерывная пауза без пальцев
                return false;
            }
            if (now - quietFrom < kQuietMs)
                return false;
            state = State::Idle;
        }
        if (state == State::ReleasePending) {
            if (fingers) {
                state = State::Rejected; // не просыпаться между пальцами одной руки
                return false;
            }
            if (now - releaseFrom >= kReleaseMs) {
                state = State::Waking;
                return true;
            }
            return false;
        }
        if (fingers > 1) {
            state = State::Rejected;
            return false;
        }
        if (state == State::Idle) {
            if (fingers == 1) {
                state = State::Candidate;
                start = now;
                startX = x;
                startY = y;
            }
            return false;
        }
        if (fingers == 1) {
            if (now - start > kMaxTapMs || x - startX > kMaxMovePx || startX - x > kMaxMovePx ||
                y - startY > kMaxMovePx || startY - y > kMaxMovePx)
                state = State::Rejected;
            return false;
        }
        const uint32_t elapsed = now - start;
        if (elapsed >= kMinTapMs && elapsed <= kMaxTapMs) {
            state = State::ReleasePending;
            releaseFrom = now;
            return false;
        }
        state = State::Quiet;
        quietFrom = now;
        return false;
    }

  private:
    enum class State : uint8_t { Idle, Candidate, ReleasePending, Rejected, Quiet, Waking };
    State state = State::Idle;
    uint32_t start = 0;
    uint32_t quietFrom = 0;
    uint32_t releaseFrom = 0;
    int32_t startX = 0;
    int32_t startY = 0;
};
