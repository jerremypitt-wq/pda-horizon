#include "horizon_target.h" // целевые флаги сборки (вкус: standalone/прошивка)
// ПДА «Горизонт» - замер батареи через делитель на J15.
#ifdef HORIZON_UI

#include "horizon/HorizonBattery.h"
#include "Arduino.h"

namespace {
constexpr int kSamples = 7;
constexpr int kMinValidMv = 1000;  // чтение младше - обрыв/не тот пин
constexpr int kMaxValidMv = 2500;  // чтение старше - коротыш/не тот пин
constexpr int kDetectMinMv = 1400; // окно автопоиска уже: висящий пин
constexpr int kDetectMaxMv = 2250; // в него случайно не попадает
constexpr uint8_t kPinPrimary = 17;
constexpr uint8_t kPinAlt = 18;
} // namespace

int hzBatteryReadPinMv(int pin)
{
#if defined(ARCH_PORTDUINO)
    (void)pin;
    return -1; // на Linux замера нет
#else
    int v[kSamples];
    int n = 0;
    for (int i = 0; i < kSamples; i++) {
        int r = (int)analogReadMilliVolts((uint8_t)pin);
        if (r > 0)
            v[n++] = r;
    }
    if (n < 3)
        return -1;
    for (int i = 1; i < n; i++) { // сортировка вставками, n <= 7
        int x = v[i], j = i - 1;
        while (j >= 0 && v[j] > x) {
            v[j + 1] = v[j];
            j--;
        }
        v[j + 1] = x;
    }
    int m = v[n / 2];
    return (m >= kMinValidMv && m <= kMaxValidMv) ? m : -1;
#endif
}

int hzBatteryDetectPin(void)
{
    int p = hzBatteryReadPinMv(kPinPrimary);
    if (p >= kDetectMinMv && p <= kDetectMaxMv)
        return kPinPrimary;
    int a = hzBatteryReadPinMv(kPinAlt);
    if (a >= kDetectMinMv && a <= kDetectMaxMv)
        return kPinAlt;
    return -1;
}

int hzBatteryPercent(int battMv)
{
    static const int V[] = {3300, 3550, 3700, 3800, 3900, 4050, 4200};
    static const int P[] = {0, 12, 28, 45, 65, 85, 100};
    if (battMv <= V[0])
        return 0;
    for (size_t i = 1; i < sizeof(V) / sizeof(V[0]); i++) {
        if (battMv <= V[i]) {
            int dv = V[i] - V[i - 1];
            return P[i - 1] + (P[i] - P[i - 1]) * (battMv - V[i - 1]) / dv;
        }
    }
    return 100;
}

void hzBatteryPoll(HzBatteryState &st, uint16_t scale)
{
    if (st.pin < 0) { // -2 и -1: (пере)поиск каждый опрос, пока не найден
        st.pin = hzBatteryDetectPin();
        st.badReads = 0;
        if (st.pin < 0) {
            st.rawMv = st.battMv = st.percent = -1;
            return;
        }
    }
    int mv = hzBatteryReadPinMv(st.pin);
    if (mv < 0) {
        if (++st.badReads >= 3) { // делитель отвалился - искать заново
            st.pin = -2;
            st.badReads = 0;
            st.rawMv = st.battMv = st.percent = -1;
        }
        return;
    }
    st.badReads = 0;
    st.rawMv = mv;
    if (scale < 800)
        scale = 800;
    if (scale > 1200)
        scale = 1200;
    st.battMv = (int)((int32_t)mv * 2 * scale / 1000);
    st.percent = hzBatteryPercent(st.battMv);
}

#endif // HORIZON_UI
