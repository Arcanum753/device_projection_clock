# device_projection_clock — проекционные часы ЧЧ:ММ:СС

> **Опциональное устройство.** Подключается через `src_filter` + `[registry]`.
> **Работает только в составе сборки с ядром** (см. `../../TRS.md` §2.1).
> Для сборки нужны само устройство и модули, указанные в его `.ini`.

- **Репозиторий:** https://github.com/Arcanum753/device_projection_clock
- **Папка:** `src/device_projection_clock/`
- **Env:** `esp32_projection_clock`
- **Registry:** `object=proj_clock`, `define=DEVICE_PROJ_CLOCK`, `namespace=proj_clock`,
  `web=1`, `loop=0`, `res=1`, `prio=70`
- **Зависит от модулей:** `module_udp`, `module_otaclient`
- **Зависит от ядра:** `core_web`, `core_sys`, `core_json`, `core_state`, `core_led`

## Назначение

Часы на 6 проекционных разрядах `ЧЧ:ММ:СС`. Каждый разряд — 10 адресных RGB-светодиодов
(всего 60). В разряде одновременно горит **не более одного** светодиода. Поддерживаются
цветовые эффекты, отдельные анимации смены минуты и часа, «дождь» до синхронизации времени и
отладочные режимы (ручные цифры, перебор LED, предпросмотр анимаций).

## Аппаратная карта

| Параметр | Значение |
|----------|----------|
| Платформа | ESP32 |
| DATA | GPIO16 (`dataPin`), NeoPixelBus `NeoGrbFeature` |
| Лента | 60 LED = 6 блоков × 10 |
| Порядок блоков | слева направо: `ЧЧ ММ СС` |
| Порядок LED в блоке | `[4,3,2,1,5,6,0,9,8,7]` |

Обратная карта `digit -> index`: `0→6, 1→3, 2→2, 3→1, 4→0, 5→4, 6→5, 7→9, 8→8, 9→7`.
Глобальный индекс: `pos * 10 + digitToIndex[digit]`.

Вывод ленты: I2S+DMA по умолчанию; `-D PROJ_CLOCK_USE_RMT=1` — RMT. `dataPin=-1` — выключено.

## Функциональные требования

- FR-PCLK-1: Показывать текущее время 6 разрядами; источник — `core_sys.timeNow()`,
  показ при `timeValid() || timeFallback()`. Смена секунд — без анимации.
- FR-PCLK-2: 24/12-часовой формат; в 12h — `01..12` с ведущим нулём, без AM/PM.
- FR-PCLK-3: Смена минуты — `minuteFx` (если `minute % minuteFxFreq == 0`), смена часа —
  `hourFx` (приоритет). Анимации затрагивают все 6 разрядов; во время анимации время
  замораживается, после — синхронизируется с текущим.
- FR-PCLK-4: Жёсткий инвариант — в каждом разряде 0 или 1 LED на каждом кадре, включая
  переходы, дождь, затухания, `crossfade`, перебор LED и ручной тест.
- FR-PCLK-5: Цветовые эффекты `mono/rainbow/grad_static/grad_dynamic/colorcycle`; цвет
  вычисляется по позиции разряда (0..5), `effectDir` — слева→вправо, справа→влево,
  от центра, к центру. По умолчанию зелёный `#00FF00`.
- FR-PCLK-6: «Дождь» до валидного времени: случайные цифры в разрядах, не дольше
  `rainDurMin`, затем внутренние часы. При синхронизации — фиксация (`rainSettleDur`) или
  переход к реальному времени.
- FR-PCLK-7: Публичные сеттеры применяют изменения в RAM/на экране, но не сохраняют конфиг;
  сохранение — web `handleSave`, bus `proj_clock.save` (полный), `proj_clock.save_mode`
  (только `busMode`).
- FR-PCLK-8: Режимы `busMode` off/auto/macro; bus-запись (кроме mode/save_mode) разрешена
  только в `macro`.
- FR-PCLK-9: Отладка: serial-лог, веб-статус, ручные 6 цифр, перебор всей ленты/разряда,
  предпросмотр минуты/часа/дождя.

## Переходные эффекты

`off, fade_all, fade_seq, wipe_lr, wipe_rl, dissolve, flash, spin, slot, sweep, crossfade`.

- `spin` — полный оборот LED внутри разряда по 10 позициям.
- `crossfade` — старый LED гаснет до 0, затем новый плавно разгорается; совмещения нет.
- `fade_*`/`flash`/`wipe`/`sweep`/`dissolve`/`slot` также дают не более одного LED на разряд.

## Конфигурация

`/config_proj_clock.json`: `busMode`, `mode`, `dataPin`, `brightness`, `hourFormat`,
`effect`, `effectDir`, `digitsColor`, `digitsColor2`, `animSpeed`, `colorsCount`, `cycleMode`,
`palette[8]`, `minuteFx`, `minuteFxDur`, `minuteFxFreq`, `hourFx`, `hourFxDur`, `rainEnabled`,
`rainDurMin`, `rainIntensity`, `rainSettleDur`, `manualText`.

Дефолты: GPIO16, яркость 255, 24h, `mono`, зелёный, `minuteFx=spin/1с` (каждую минуту),
`hourFx=sweep/3с`, дождь включён (макс. 2 с, фиксация 1 с), `manualText="123456"`.

## Веб-интерфейс

Маршруты:
- `GET /proj_clock/save` — сохранить параметры (auth)
- `GET /proj_clock/info` — CVT-статус (auth)
- `GET /proj_clock/test` — `action=sweep_all|sweep_digit|stop|fx_minute|fx_hour|rain` (auth)
- `GET /proj_clock/ver` — версия модуля

Страница `proj_clock.html`, пункт меню — `web/_menu.html`.

## Ресурсы шины (`proj_clock`)

Состояния: `mode` (off/auto/macro), `effect` (ENUM), `brightness` (I32), `color` (STR),
`hour_format` (ENUM), `text` (STR), `minute_fx` (I32 0..10), `hour_fx` (I32 0..10).
`minute_fx`/`hour_fx` — I32, а не ENUM, из-за лимита ядра `CORE_STATE_MAX_ENUM=8`.
Функции: те же + `save`, `save_mode`.

`proj_clock.mode` — это `busMode`, а не рабочий/ручной режим. `proj_clock.text` переключает
`mode` в ручной.

## Критерии приёмки

- AC-PCLK-1: В рабочем режиме видны `ЧЧ:ММ:СС`; секунды меняются без анимации; 12/24
  переключается и сохраняется.
- AC-PCLK-2: Ручной тест и перебор LED подтверждают карту `digit -> LED`; в каждом разряде
  никогда не горит два LED.
- AC-PCLK-3: Смена минуты запускает `minuteFx` с учётом `minuteFxFreq`, смена часа — `hourFx`;
  после анимации время синхронизируется.
- AC-PCLK-4: Все 11 переходных эффектов не нарушают инвариант «один LED на разряд».
- AC-PCLK-5: Цветовые эффекты и `effectDir` работают; по умолчанию зелёный.
- AC-PCLK-6: «Дождь» ограничен `rainDurMin`; при синхронизации — фиксация/переход.
- AC-PCLK-7: `proj_clock.save`/веб-Save персистят полный конфиг, `save_mode` — только
  `busMode`, обычные сеттеры — не персистят.
- AC-PCLK-8: Отладочные действия работают и не вызывают двух LED в разряде.

## Файлы

`proj_clock_types.h`, `common_module.*`, `proj_clock_strip.*`, `proj_clock.*`,
`device_projection_clock.h` (тонкая обёртка для registry), `proj_clock_engine.cpp`, `web/*`,
`device_projection_clock.ini`, `device_projection_clock_version.h` (генерируется сборкой).

> Если устройство читается вне дерева ядра (standalone), корневые документы доступны в
> репозитории ядра avr-fota.
