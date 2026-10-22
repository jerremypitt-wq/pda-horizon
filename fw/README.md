# «Горизонт» в дереве прошивки Meshtastic (single-MCU)

Комплектная плата Elecrow CrowPanel Advance 3.5 + SX1262 — это single-MCU:
радио висит на SPI того же ESP32-S3, отдельного радиомодуля с UART нет.
Поэтому «Горизонт» собирается прямо в дереве `meshtastic/firmware`
стоковым окружением `elecrow-adv-35-tft`, а данные берёт по внутреннему
PacketAPI (PacketServer + PacketClient) вместо UART-клиента.

`fw/overlay.py` — единственный инструмент переноса: **только копирование
файлов**, без форка прошивки и без правок её `platformio.ini`. Вкус сборки
переключается перезаписываемым `horizon_target.h` (флаг `HORIZON_PACKET_LINK`).

## Пины (проверено)

| Компонент | Пин |
|---|---|
| Прошивка | тег `v2.8.1` (`v2.8.1.8e6a88d`) |
| device-ui в ней | `f77bb70` (апстрим, авто-установка через lib_deps) |
| NodeStore (вендор) | из форка `jerremypitt-wq/device-ui@9ec70c8` (14.10.2026), файлы `fw/vendor/graphics/common/` |
| LVGL в device-ui | v9.3.0 |

Все `meshtastic_*`-символы, используемые Горизонтом, на месте в генерации
v2.8.1; дрейф API форк→f77 закрыт `#ifdef HORIZON_PACKET_LINK` (плоские
`setMyInfo`/`updateMetrics`, без `hasKnownNodeForPacket`, гард от затирания
имени заглушкой «Meshtastic XXXX»).

## Процедура (Windows, PowerShell)

```powershell
git clone --branch v2.8.1.8e6a88d --depth 1 https://github.com/meshtastic/firmware firmware-hz
cd firmware-hz
pio pkg install -e elecrow-adv-35-tft
python C:\path\to\pda-horizon\fw\overlay.py --fw .
pio run -e elecrow-adv-35-tft
```

`pio pkg install` нужен ДО оверлея, чтобы шаг 6 (boot-заливка) застал
установленный device-ui. Если libdeps не было — overlay предупредит, файлы
всё равно скопируются; тогда повторить `overlay.py` после первой сборки.

Проверка без сборки: `overlay.py --fw . --dry-run`. Откат оверлея:
`overlay.py --fw . --revert` (восстановит `tftSetup.cpp` из `.stock`,
удалит `src/horizon`; boot-патч в `.pio` не откатывается — он безвреден
и для стокового UI).

## Карта файлов

| Источник (этот репозиторий) | Назначение в дереве прошивки |
|---|---|
| `src/horizon/*.{h,cpp,c}` | `src/horizon/` (весь UI как есть) |
| `fw/vendor/graphics/common/NodeStore.*` | `src/horizon/graphics/common/` (нет в апстриме) |
| `variants/elecrow-adv-35/config.h` | `src/horizon/config.h` (класс панели, тач 0x14) |
| `fw/horizon_target_firmware.h` | `src/horizon/horizon_target.h` (флаг `HORIZON_PACKET_LINK`) |
| `fw/tftSetup_horizon.cpp` | `src/graphics/tftSetup.cpp` (клей; оригинал → `.stock`) |

Почему это работает без правок ini: `src/horizon/` компилируется общим
`src_filter +<*>`; quoted-инклуды (`"config.h"`, `"horizon_target.h"`,
`"graphics/common/NodeStore.h"`) резолвятся относительно каталога;
`"horizon/..."`, `"meshtastic/..."`, `"mesh-pb-constants.h"` покрыты
штатными `-Isrc -Isrc/mesh -Isrc/mesh/generated`; `"fonts.h"` подтягивает
extra-скрипт самого device-ui по `-D VIEW_320x240`; логи `ILOG_*` едут через
глобальные `USE_LOG_DEBUG` + `LOG_DEBUG_INC` варианта.

## Осознанные отличия от стока

- Экземпляр DeviceScreen не создаётся (экраном владеет HorizonView);
  `deviceScreen = nullptr` существует только как символ для WakeKey.cpp.
- Нет обсерверов light sleep (привязаны к методам DeviceScreen).
  Яркость/таймаут рулит сам DisplayDriver по настройкам Горизонта.
- Тач GT911: в конфиге 0x14, но драйвер LovyanGFX при отсутствии ответа сам
  перебирает 0x14/0x5D — на комплектном железе чип отвечает на **0x5D**
  (видно и в скане I2C самой прошивки), тач работает без правок.
- Перезагрузка из настроек / сбросы действуют на весь ПДА (строки в UI
  так и говорят), т.к. радио — часть той же прошивки.
- Шаг 6 красит boot-заливку из зелёной (0x3DDA83) в чёрную — тот же патч,
  что `bin/patch_deviceui_boot.py` делает для standalone.

## Диагностика

- Overlay ругается на пин device-ui → дерево не на v2.8.1 (либо `--force`
  на свой риск: API могло уплыть).
- `HORIZON_PACKET_LINK` не задан (собрался UART-вкус в прошивке: вечное
  «Нет связи») → шаг 4 не применился; проверить `src/horizon/horizon_target.h`.
- Пустой экран после прошивки → сначала проверить стоковый `.bin` с сайта
  Meshtastic (диагностика железа), затем `pio device monitor`.
