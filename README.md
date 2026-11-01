# device_projection_clock

Проекционные часы `ЧЧ:ММ:СС` на 6 разрядах × 10 адресных RGB-светодиодов (всего 60 LED) для ESP32.
/ 6-digit projection clock (HH:MM:SS) with 60 addressable RGB LEDs on ESP32.

Устройство для ядра [avr-fota](https://github.com/Arcanum753/avr-fota), env `esp32_projection_clock`.

## Аппаратная карта

| Параметр | Значение |
|----------|----------|
| Платформа | ESP32 classic |
| DATA | GPIO16 (`dataPin`) |
| Лента | 60 LED = 6 блоков × 10, `NeoGrbFeature` |
| Порядок блоков | слева направо: `ЧЧ ММ СС` |
| Порядок LED в блоке | `[4,3,2,1,5,6,0,9,8,7]` |

Глобальный индекс LED: `pos * 10 + digitToIndex[digit]`, где
`digitToIndex = {0:6, 1:3, 2:2, 3:1, 4:0, 5:4, 6:5, 7:9, 8:8, 9:7}`.

**Жёсткий инвариант:** в каждом разряде одновременно горит не более одного светодиода —
во всех режимах, включая переходы, затухания, `crossfade`, «дождь» и перебор LED.

## Возможности

- Показ времени 24/12 ч (в 12h — `01..12` с ведущим нулём).
- Цветовые эффекты: `mono`, `rainbow`, `grad_static`, `grad_dynamic`, `colorcycle`.
- Отдельные анимации смены минуты (`minuteFx`) и часа (`hourFx`):
  `off`, `fade_all`, `fade_seq`, `wipe_lr`, `wipe_rl`, `dissolve`, `flash`,
  `spin`, `slot`, `sweep`, `crossfade`.
- «Дождь» до синхронизации времени с ограничением по длительности и фиксацией.
- Веб-страница: настройки, ручной тест 6 цифр, перебор LED, предпросмотр анимаций, статус.
- Resource-bus `proj_clock.*` (`mode`, `effect`, `brightness`, `color`, `hour_format`,
  `text`, `minute_fx`, `hour_fx`, `save`, `save_mode`).
- Яркость по умолчанию — **255**, цвет — зелёный `#00FF00`.

## Сборка

```bash
# из корня avr-fota, после клонирования устройства в src/device_projection_clock
pio run -e esp32_projection_clock
```

Env: ядро + `module_udp` + `module_otaclient` + `device_projection_clock`. Без `module_macros`.

## Прошивка

На плате с CH340 авто-reset может не работать. Если `pio run -t upload` возвращает
`Wrong boot mode (0x13)`, переведите ESP32 в download mode вручную
(USB отключить → зажать BOOT → подключить USB → отпустить BOOT) и прошейте:

```bash
esptool.py --port COMx --baud 460800 --before no_reset --after hard_reset \
  write_flash -z --flash_mode dio --flash_freq 40m --flash_size 4MB \
  0x1000 .pio/build/esp32_projection_clock/bootloader.bin \
  0x8000 .pio/build/esp32_projection_clock/partitions.bin \
  0xe000 ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 .pio/build/esp32_projection_clock/firmware.bin \
  0x2F0000 .pio/build/esp32_projection_clock/littlefs.bin
```

## HTTP

Страница: `/proj_clock.html`. Эндпоинты (под HTTP-auth, если включена):

- `GET /proj_clock/save` — сохранить параметры;
- `GET /proj_clock/info` — CVT-статус;
- `GET /proj_clock/test?action=sweep_all|sweep_digit|stop|fx_minute|fx_hour|rain`;
- `GET /proj_clock/ver` — версия модуля.

Конфиг: `/config_proj_clock.json`.

## Документация

Подробности — в [`AGENTS.md`](AGENTS.md): архитектура, режимы, ресурсы шины, критерии приёмки.
