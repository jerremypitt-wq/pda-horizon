# ПДА «Горизонт»

Прошивка ПДА «Горизонт» — коммуникатор в стиле S.T.A.L.K.E.R. для
страйкбольных ролевых игр живого действия.

**Железо:** Elecrow CrowPanel Advance 3.5" (ESP32-S3) + LoRa-модуль SX1262
отдельной платой в слот J9+J11 (SPI + DIO1/BUSY/NRESET) + батарея 3.7 В
в штатный разъём J3.

## Сборка и прошивка

Основной путь — single-MCU: сборка в дереве `meshtastic/firmware`
стоковым env `elecrow-adv-35-tft` через `fw/overlay.py`
(процедура — в [fw/README.md](fw/README.md);
коротко: `build.bat`, затем `package.bat`).

Запасной путь — standalone (панель + внешнее радио по UART0/J10,
сейчас не используется — J10 свободен):

```bash
pio run -e elecrow-adv-35-horizon             # сборка
pio run -e elecrow-adv-35-horizon -t upload   # прошивка через J2 (OTG)
```

## Документация

- [docs/HORIZON.md](docs/HORIZON.md) — описание прошивки: экраны, настройки,
  мастер-пароль, интеграция с Meshtastic, хост-проверка синтаксиса
- [docs/FIRST_BOOT.md](docs/FIRST_BOOT.md) — инструкция по первому включению
- [docs/POWER_SUPPLY.md](docs/POWER_SUPPLY.md) — цепь питания: батарея,
  тумблер и USB-C; раздел делителя относится только к старой прошивке
- [docs/MAX17048.md](docs/MAX17048.md) — датчик заряда на J15 (с 1.10.0.ZI);
  до подключения необходимо проверить конкретный модуль мультиметром
- [docs/patches/](docs/patches/) — патчи к форку device-ui

## Основа

Репозиторий основан на [meshtastic/standalone-ui](https://github.com/meshtastic/standalone-ui)
(MUI) и использует форк [jerremypitt-wq/device-ui](https://github.com/jerremypitt-wq/device-ui).
Штатные окружения MUI под другие платы из `platformio.ini` удалены —
осталось только `elecrow-adv-35-horizon`.

**Отдельный редактор карт с вкладками** (HorizonMapStudio 1.12.2; прошивка от 1.12.0.ZI, текущая 1.12.1.ZI): сборка Windows `tools\flasher\build_map_studio.bat`, инструкция — [`tools/flasher/README-MAP-STUDIO.md`](tools/flasher/README-MAP-STUDIO.md). Существующий флешер и его редактор одной карты сохранены.
