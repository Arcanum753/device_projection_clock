#ifndef _DEVICE_PROJECTION_CLOCK_TYPES_h
#define _DEVICE_PROJECTION_CLOCK_TYPES_h

// ============================================================
// proj_clock_types.h — типы, структуры и define'ы устройства
// «Проекционные часы» (6 разрядов ЧЧММСС, 60 адресных LED).
// Реализация: proj_clock.cpp (шаблон) и proj_clock_engine.cpp.
// ============================================================

#include <Arduino.h>
#include <stdint.h>

#define CONFIG_FILE_PROJ_CLOCK "/config_proj_clock.json"

#define PROJ_CLOCK_DIGITS          6
#define PROJ_CLOCK_LEDS_PER_DIGIT  10
#define PROJ_CLOCK_LEDS            (PROJ_CLOCK_DIGITS * PROJ_CLOCK_LEDS_PER_DIGIT)

// Рабочий режим устройства
#define PROJ_CLOCK_MODE_WORK     0   // часы ЧЧ:ММ:СС
#define PROJ_CLOCK_MODE_MANUAL   1   // ручной тест: 6 заданных цифр

// Режим управления шиной (off / auto / macro)
#define PROJ_CLOCK_BUS_OFF       0   // выключено: лента погашена
#define PROJ_CLOCK_BUS_AUTO      1   // автономная работа
#define PROJ_CLOCK_BUS_MACRO     2   // управление сценариями (пока не подключено)

// Формат часов
#define PROJ_CLOCK_HOUR_24       0   // 00..23
#define PROJ_CLOCK_HOUR_12       1   // 01..12 (ведущий ноль, без AM/PM)

// Цветовые эффекты
#define PROJ_CLOCK_EFFECT_MONO          0
#define PROJ_CLOCK_EFFECT_RAINBOW       1
#define PROJ_CLOCK_EFFECT_GRAD_STATIC   2
#define PROJ_CLOCK_EFFECT_GRAD_DYNAMIC  3
#define PROJ_CLOCK_EFFECT_COLORCYCLE    4
#define PROJ_CLOCK_EFFECT_COUNT         5

// Направление цветового перелива
#define PROJ_CLOCK_FXDIR_LR         0   // слева направо
#define PROJ_CLOCK_FXDIR_RL         1   // справа налево
#define PROJ_CLOCK_FXDIR_CENTER     2   // от центра к краям
#define PROJ_CLOCK_FXDIR_EDGES      3   // от краёв к центру
#define PROJ_CLOCK_FXDIR_COUNT      4

// Режимы обхода палитры
#define PROJ_CLOCK_CYCLE_SEQUENTIAL 0
#define PROJ_CLOCK_CYCLE_RANDOM     1
#define PROJ_CLOCK_CYCLE_RAINBOW    2

#define PROJ_CLOCK_MAX_COLORS       8

// Переходные эффекты смены минуты/часа
#define PROJ_CLOCK_TFX_OFF          0
#define PROJ_CLOCK_TFX_FADE_ALL     1
#define PROJ_CLOCK_TFX_FADE_SEQ     2
#define PROJ_CLOCK_TFX_WIPE_LR      3
#define PROJ_CLOCK_TFX_WIPE_RL      4
#define PROJ_CLOCK_TFX_DISSOLVE     5
#define PROJ_CLOCK_TFX_FLASH        6
#define PROJ_CLOCK_TFX_SPIN         7
#define PROJ_CLOCK_TFX_SLOT         8
#define PROJ_CLOCK_TFX_SWEEP        9
#define PROJ_CLOCK_TFX_CROSSFADE    10
#define PROJ_CLOCK_TFX_COUNT        11

// Внутренние состояния отображения
#define PROJ_CLOCK_VIEW_NORMAL      0
#define PROJ_CLOCK_VIEW_RAIN        1
#define PROJ_CLOCK_VIEW_SETTLE      2
#define PROJ_CLOCK_VIEW_TRANS       3

// Отложенные отладочные действия (выполняются в главном цикле)
#define PROJ_CLOCK_TEST_NONE        0
#define PROJ_CLOCK_TEST_FX_MINUTE   1
#define PROJ_CLOCK_TEST_FX_HOUR     2
#define PROJ_CLOCK_TEST_RAIN        3

// Периоды задач
#define PROJ_CLOCK_ANIM_MS          100   // шаг фазы цветовых эффектов
#define PROJ_CLOCK_RENDER_MS        50    // период рендера анимаций
#define PROJ_CLOCK_SWEEP_STEP_MS    200   // шаг отладочного перебора LED
#define PROJ_CLOCK_RAIN_RAMP_MS     1000  // разгон «дождя»

#define PROJ_CLOCK_TFX_FREQ_DEFAULT 1
#define PROJ_CLOCK_DUR_MAX          10
#define PROJ_CLOCK_RAIN_DUR_MAX     10

typedef struct {
    uint8_t  busMode;       // PROJ_CLOCK_BUS_*
    uint8_t  mode;          // PROJ_CLOCK_MODE_WORK / MANUAL
    int16_t  dataPin;       // пин данных (по умолчанию 16, -1 = выкл.)
    uint8_t  brightness;    // 0..255
    uint8_t  hourFormat;    // PROJ_CLOCK_HOUR_24 / 12
    uint8_t  effect;        // PROJ_CLOCK_EFFECT_*
    uint8_t  effectDir;     // PROJ_CLOCK_FXDIR_*
    uint32_t digitsColor;   // 0xRRGGBB
    uint32_t digitsColor2;  // 0xRRGGBB
    uint8_t  animSpeed;     // 1..50 градусов за 100 мс
    uint8_t  colorsCount;   // 1..8
    uint8_t  cycleMode;     // PROJ_CLOCK_CYCLE_*
    uint32_t palette[PROJ_CLOCK_MAX_COLORS];

    uint8_t  minuteFx;      // PROJ_CLOCK_TFX_*
    uint8_t  minuteFxDur;   // 1..10 с
    uint8_t  minuteFxFreq;  // 1,3,5,10,15,20,40
    uint8_t  hourFx;        // PROJ_CLOCK_TFX_*
    uint8_t  hourFxDur;     // 1..10 с

    uint8_t  rainEnabled;   // 0/1
    uint8_t  rainDurMin;    // 1..10 с — максимум дождя без NTP
    uint8_t  rainIntensity; // 1..10
    uint8_t  rainSettleDur; // 0..10 с

    String   manualText;    // 6 цифр для ручного теста
} strProjClockConfig;

#endif // _DEVICE_PROJECTION_CLOCK_TYPES_h
