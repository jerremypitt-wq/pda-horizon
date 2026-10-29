# «Горизонт» в дереве прошивки Meshtastic (single-MCU)

Комплектная плата Elecrow CrowPanel Advance 3.5 + SX1262 — это single-MCU:
радио висит на SPI того же ESP32-S3, отдельного радиомодуля с UART нет.
Поэтому «Горизонт» собирается прямо в дереве `meshtastic/firmware`
стоковым окружением `elecrow-adv-35-tft`, а данные берёт по внутреннему
PacketAPI (PacketServer + PacketClient) вместо UART-клиента.

`fw/overlay.py` — единственный инструмент переноса: **только копирование
файлов**, без форка прошивки и без правок её `platformio.ini`. Вкус сборки
переключается перезаписываемым `horizon_target.h` (флаг `HORIZON_PACKET_LINK`).
Слот радиоплагина на плате — J9+J11 (распиновка и нюансы: ключ U8/IO45,
общий CS/BOOT, общий I2C с тачем — в `docs/HORIZON.md`, раздел single-MCU).

## Пины (проверено)

| Компонент | Пин |
|---|---|
| Прошивка | тег `v2.8.1` (`v2.8.1.8e6a88d`) |
| device-ui в ней | `f77bb70` (апстрим, авто-установка через lib_deps) |
| NodeStore (вендор) | из форка `jerremypitt-wq/device-ui@9ec70c8` (14.10.2026), файлы `fw/vendor/graphics/common/` |
| LVGL в device-ui | v9.3.0 |
| PlatformIO | 6.1.19+ (проверено на 6.2.0; ставится сам через build.bat) |

Все `meshtastic_*`-символы, используемые Горизонтом, на месте в генерации
v2.8.1; дрейф API форк→f77 закрыт `#ifdef HORIZON_PACKET_LINK` (плоские
`setMyInfo`/`updateMetrics`, без `hasKnownNodeForPacket`, гард от затирания
имени заглушкой «Meshtastic XXXX»).

## Процедура (Windows, PowerShell)

Короткий путь (2 команды из корня репозитория; нужен Python 3.10-3.14,
лучше 3.13, остальное скрипты сделают сами):

```powershell
.\build.bat    # клон дерева, PlatformIO, оверлей, сборка, buildfs, контроль выхлопа
.\package.bat  # ZIP-пакет horizon-<версия>.zip для флэшера
```

Вручную (то же самое по шагам):

```powershell
git clone --branch v2.8.1.8e6a88d --depth 1 https://github.com/meshtastic/firmware firmware-hz
cd firmware-hz
pio pkg install -e elecrow-adv-35-tft
python C:\path\to\pda-horizon\fw\overlay.py --fw .
pio run -e elecrow-adv-35-tft
# проверка, что собрался именно Горизонт, а не стоковый MUI:
Select-String "HORIZON_PACKET_LINK" .\src\horizon\horizon_target.h
# заливка в ПДА, подключённый по USB (J1), и просмотр логов загрузки:
pio run -e elecrow-adv-35-tft -t upload
pio device monitor -b 115200
```

`pio pkg install` нужен ДО оверлея, чтобы шаг 6 (boot-заливка) застал
установленный device-ui. Если libdeps не было — overlay предупредит, файлы
всё равно скопируются; тогда повторить `overlay.py` после первой сборки.

Полевая прошивка готовых бинарников без VSCode/интернета — отдельный
инструмент `tools/flasher/` (один `.exe` + папка пакета): см. его README.

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

- `TypeError: ... not NoneType` в `arduino.py`, строка `Path(FRAMEWORK_DIR)` →
  не установлен пакет платформы `framework-arduinoespressif32` (в списке
  PACKAGES его нет, есть только `...-libs`). Сам оверлей тут ни при чём:
  он только копирует файлы и пакеты PlatformIO не трогает. Лечение —
  переустановить платформу начисто (закрыть VSCode, PowerShell):
  ```powershell
  pio platform uninstall espressif32
  Remove-Item -Recurse -Force ~\.platformio\packages\framework-arduinoespressif32*
  pio run -e elecrow-adv-35-tft
  ```
  Платформа и фреймворк скачаются заново (~1 ГБ, несколько минут).
  Если раньше правили `platform.json` под SCons (обход конфликта версий
  tool-scons) — правку внести заново после переустановки. Не помогло —
  также удалить `~\.platformio\packages\framework-espidf` и повторить.
- `esptool: error: unrecognized arguments: --flash-mode ...` на шаге
  `elf2image` → в `~\.platformio\packages` залежался `tool-esptoolpy` v4
  (понимает только `--flash_mode` с подчёркиванием), а платформа 55.03.311
  шлёт ему синтаксис v5 (`--flash-mode` с дефисом) и требует свой
  `esptoolpy-v5.3.0`. Сам оверлей ни при чём. Объекты в `.pio/build` целы —
  после лечения добьётся быстро, 7 минут компиляции не потеряны:
  ```powershell
  Remove-Item -Recurse -Force ~\.platformio\packages\tool-esptoolpy*
  pio run -e elecrow-adv-35-tft
  ```
  Если после этого снова встал v4 — ищите ручную правку `tool-esptoolpy`
  в `~\.platformio\platforms\espressif32*\platform.json` (следы старых
  обходов конфликтов пакетов), откатите её либо переустановите платформу.
- `InvalidSchema: Missing dependencies for SOCKS support` при скачивании
  пакетов → в системе/переменных окружения задан SOCKS-прокси, а в Python
  PlatformIO нет `PySocks`. Лечение:
  ```powershell
  ~\.platformio\penv\Scripts\python -m pip install "requests[socks]"
  pio run -e elecrow-adv-35-tft
  ```
  Если прокси мёртв или не нужен — вместо этого выключить его (переменные
  `*_PROXY` и «Параметры → Сеть → Прокси» Windows) и повторить сборку.
- Overlay ругается на пин device-ui → дерево не на v2.8.1 (либо `--force`
  на свой риск: API могло уплыть).
- `HORIZON_PACKET_LINK` не задан (собрался UART-вкус в прошивке: вечное
  «Нет связи») → шаг 4 не применился; проверить `src/horizon/horizon_target.h`.
- Пустой экран после прошивки → сначала проверить стоковый `.bin` с сайта
  Meshtastic (диагностика железа), затем `pio device monitor`.
