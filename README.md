# ПДА «Горизонт»

Прошивка ПДА «Горизонт» — коммуникатор в стиле S.T.A.L.K.E.R. для
страйкбольных ролевых игр живого действия.

**Железо:** Elecrow CrowPanel Advance 3.5" (ESP32-S3) + LoRa-модуль SX1262
на разъёме UART0 (J10, пины 44/43) + батарея 3.7 В в штатный разъём J3.

## Сборка и прошивка

```bash
pio run -e elecrow-adv-35-horizon             # сборка
pio run -e elecrow-adv-35-horizon -t upload   # прошивка через USB-UART (J1)
```

## Документация

- [docs/HORIZON.md](docs/HORIZON.md) — описание прошивки: экраны, настройки,
  мастер-пароль, интеграция с Meshtastic, хост-проверка синтаксиса
- [docs/FIRST_BOOT.md](docs/FIRST_BOOT.md) — инструкция по первому включению
- [docs/POWER_SUPPLY.md](docs/POWER_SUPPLY.md) — цепь питания: батарея,
  тумблер, делитель для индикатора заряда, какой USB-C выводить в корпусе
- [docs/patches/](docs/patches/) — патчи к форку device-ui

## Основа

Репозиторий основан на [meshtastic/standalone-ui](https://github.com/meshtastic/standalone-ui)
(MUI) и использует форк [jerremypitt-wq/device-ui](https://github.com/jerremypitt-wq/device-ui).
Штатные окружения MUI под другие платы из `platformio.ini` удалены —
осталось только `elecrow-adv-35-horizon`.
