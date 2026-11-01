#ifndef _DEVICE_PROJ_CLOCK_h
#define _DEVICE_PROJ_CLOCK_h

#include "main.h"

#include "mod_context.h"

#include <LittleFS.h>

#include "proj_clock_strip.h"
#include "proj_clock_types.h"

#ifdef DEBUG_PROJ_CLOCK
#define DEBUGPROJCLOCK(...) DBG_MOD("[D_PROJ_CLOCK] ", __VA_ARGS__)
#else
#define DEBUGPROJCLOCK(...)
#endif

class AsyncWebServerRequest;

// ============================================================
// CLASS_DEVICE_PROJ_CLOCK — проекционные часы ЧЧ:ММ:СС
// на 6 разрядах по 10 адресных RGB-светодиодов.
//
// Жёсткий инвариант: в каждом разряде одновременно горит не более
// одного светодиода. Всё отображение идёт через renderFrame(),
// который очищает ленту и зажигает максимум один LED на блок.
// ============================================================
class CLASS_DEVICE_PROJ_CLOCK {
public:
    CLASS_DEVICE_PROJ_CLOCK();
    void setFs(fs::LittleFSFS* fs);
    void begin();
    void begin(ModContext& ctx);
    void register_resources();
    void web_Init();

    // Публичное API для ресурсной шины: применяют в RAM/на экране,
    // но не сохраняют конфиг (сохранение — saveNow/web handleSave).
    bool setEffect(uint8_t e);
    bool setBrightness(uint8_t b);
    bool setColor(uint32_t c);
    bool setHourFormat(uint8_t f);
    bool setManualText(const String& t);
    bool setMinuteFx(uint8_t f);
    bool setHourFx(uint8_t f);
    bool setBusMode(uint8_t m);
    uint8_t getBusMode();

    // Явное сохранение (bus proj_clock.save / терминал).
    void saveNow();
    // Сохранение только режима шины (proj_clock.save_mode).
    bool saveModeConfig();

    // Отладочные действия (web /proj_clock/test).
    void startSweepAll();
    void startSweepDigit(uint8_t block);
    void stopSweep();
    void triggerFxMinute();
    void triggerFxHour();
    void triggerRain();

private:
    // Версионные методы
    String getVersionStr();
    String getGeneratedTime();
    String getCommitDateStr();
    void html_ver_get(AsyncWebServerRequest *request);

    // Веб-обработчики
    void handleInfo(AsyncWebServerRequest *request);
    void handleSave(AsyncWebServerRequest *request);
    void handleTest(AsyncWebServerRequest *request);

    // Конфиг
    void defaultConfig();
    bool loadConfig();
    bool saveConfig();

    // Драйвер ленты
    void initStrip();
    void destroyStrip();

    // Отображение
    void applyMode();
    void renderNow();
    void renderManual();
    void renderFrame(const uint8_t digits[PROJ_CLOCK_DIGITS],
                     const float levels[PROJ_CLOCK_DIGITS],
                     bool white);
    void renderDigit(uint8_t pos, uint8_t digit, float level, uint32_t color);
    void refreshTimeDigits(uint8_t out[PROJ_CLOCK_DIGITS]);
    void logFrame();

    // Переходы смены минуты/часа
    void startTransition(uint8_t type, uint8_t durSec, bool hourChange);
    void drawTransition();
    bool transitionActive() const;

    // Дождь и фиксация
    void startRain(bool forced);
    void drawRain();
    void startSettle();
    void drawSettle();
    bool rainActive() const;

    // Отладочный перебор LED
    void updateSweep();
    void stopSweepInternal();

    static void deferredApplyTask();

    friend void projClockSecondTask();
    friend void projClockAnimTask();

protected:
    fs::LittleFSFS* _fs;
    strProjClockConfig _config;
    ProjClockStrip _strip;

    // Отложенные операции
    bool    _pendingSave;
    bool    _pendingApply;
    bool    _pendingReinit;
    int16_t _pendingDataPin;
    uint8_t _pendingTest;

    // Текущее отображение
    uint8_t _view;
    uint8_t _digits[PROJ_CLOCK_DIGITS];
    uint8_t _fromDigits[PROJ_CLOCK_DIGITS];
    uint8_t _toDigits[PROJ_CLOCK_DIGITS];
    uint8_t _lastH, _lastM, _lastS;
    bool    _hadValidTime;
    bool    _forceRedraw;

    // Переход
    uint8_t  _transType;
    uint32_t _transStartMs;
    uint32_t _transDurMs;
    uint8_t  _transRand[PROJ_CLOCK_DIGITS];
    uint8_t  _spinFrom[PROJ_CLOCK_DIGITS];
    uint8_t  _spinDelta[PROJ_CLOCK_DIGITS];

    // Дождь/фиксация
    uint32_t _rainStartMs;
    uint32_t _rainNextMs[PROJ_CLOCK_DIGITS];
    uint8_t  _rainDigits[PROJ_CLOCK_DIGITS];
    bool     _rainForced;
    uint32_t _settleStartMs;
    uint8_t  _settleThreshold[PROJ_CLOCK_DIGITS];

    // Цветовая фаза
    float    _animPhase;
    uint32_t _fxAccumMs;
    uint32_t _fxLap;
    uint8_t  _fxOrder[PROJ_CLOCK_MAX_COLORS];

    // Отладочный перебор
    bool     _sweepActive;
    bool     _sweepAll;
    uint8_t  _sweepBlock;
    uint8_t  _sweepIdx;
    uint32_t _sweepLastMs;
};

extern CLASS_DEVICE_PROJ_CLOCK proj_clock;

#endif // _DEVICE_PROJ_CLOCK_h
