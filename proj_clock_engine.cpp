#include "core_web/FSWebServerLib.h"

#include <math.h>

#include "proj_clock.h"
#include "common_module.h"
#include "common/common.h"
#include "common/TimeLib.h"
#include "core_sys/core_sys.h"
#include "core_sys/eertos.h"

// ============================================================
// Исполнительная логика устройства «Проекционные часы»
// ============================================================

static float projClamp01(float v) {
    if (v < 0.0f) { return 0.0f; }
    if (v > 1.0f) { return 1.0f; }
    return v;
}

static uint32_t projRainIntervalMs(uint8_t intensity) {
    int base = 140 - 10 * (int)intensity;
    if (base < 40) { base = 40; }
    return (uint32_t)(base + (int)(ns_device_projection_clock::rand32() % 30));
}

// ============================================================
// Драйвер ленты
// ============================================================
void CLASS_DEVICE_PROJ_CLOCK::initStrip() {
    _strip.init(_config.dataPin);
    if (_strip.ready()) {
        _strip.clear();
        _strip.show();
    }
}

void CLASS_DEVICE_PROJ_CLOCK::destroyStrip() {
    _strip.destroy();
}

// ============================================================
// Отображение: один LED на разряд
// ============================================================
void CLASS_DEVICE_PROJ_CLOCK::refreshTimeDigits(uint8_t out[PROJ_CLOCK_DIGITS]) {
    ns_device_projection_clock::timeDigits(core_sys.timeNow(), _config.hourFormat, out);
}

void CLASS_DEVICE_PROJ_CLOCK::renderDigit(uint8_t pos, uint8_t digit, float level, uint32_t color) {
    level = projClamp01(level);
    if (level <= 0.003f) { return; }
    uint8_t b = (uint8_t)((float)_config.brightness * level + 0.5f);
    if (b == 0) { return; }
    uint16_t idx = ns_device_projection_clock::ledIndex(pos, digit % 10);
    _strip.setLed(idx, color, b);
}

void CLASS_DEVICE_PROJ_CLOCK::renderFrame(const uint8_t digits[PROJ_CLOCK_DIGITS],
                                          const float levels[PROJ_CLOCK_DIGITS],
                                          bool white) {
    memcpy(_digits, digits, sizeof(_digits));
    if (_strip.ready() == false) { return; }

    _strip.clear();
    for (uint8_t p = 0; p < PROJ_CLOCK_DIGITS; p++) {
        if (levels[p] <= 0.003f) { continue; }
        uint32_t color = white ? 0xFFFFFF
                               : ns_device_projection_clock::fxColor(_config, p, _animPhase, _fxOrder);
        renderDigit(p, digits[p] % 10, levels[p], color);
    }
    _strip.show();
}

void CLASS_DEVICE_PROJ_CLOCK::renderNow() {
    refreshTimeDigits(_digits);
    float levels[PROJ_CLOCK_DIGITS];
    for (uint8_t i = 0; i < PROJ_CLOCK_DIGITS; i++) { levels[i] = 1.0f; }
    renderFrame(_digits, levels, false);
    _forceRedraw = false;
}

void CLASS_DEVICE_PROJ_CLOCK::renderManual() {
    uint8_t digits[PROJ_CLOCK_DIGITS];
    for (uint8_t i = 0; i < PROJ_CLOCK_DIGITS; i++) {
        char c = (i < _config.manualText.length()) ? _config.manualText.charAt(i) : '0';
        digits[i] = (c >= '0' && c <= '9') ? (uint8_t)(c - '0') : 0;
    }
    float levels[PROJ_CLOCK_DIGITS];
    for (uint8_t i = 0; i < PROJ_CLOCK_DIGITS; i++) { levels[i] = 1.0f; }
    renderFrame(digits, levels, false);
    _forceRedraw = false;
}

void CLASS_DEVICE_PROJ_CLOCK::applyMode() {
    if (_sweepActive) {
        updateSweep();
        return;
    }
    if (_config.busMode == PROJ_CLOCK_BUS_OFF) {
        _strip.clear();
        _strip.show();
        return;
    }
    if (_config.mode == PROJ_CLOCK_MODE_MANUAL) {
        renderManual();
        return;
    }
    if (_view == PROJ_CLOCK_VIEW_RAIN) {
        drawRain();
    } else if (_view == PROJ_CLOCK_VIEW_SETTLE) {
        drawSettle();
    } else if (_view == PROJ_CLOCK_VIEW_TRANS) {
        drawTransition();
    } else {
        renderNow();
    }
}

void CLASS_DEVICE_PROJ_CLOCK::logFrame() {
    DEBUGPROJCLOCK("time=%s digits=%d%d%d%d%d%d leds=%u,%u,%u,%u,%u,%u valid=%d view=%u\r\n",
                   ns_device_projection_clock::timeHhMmSs(core_sys.timeNow(), _config.hourFormat).c_str(),
                   _digits[0], _digits[1], _digits[2], _digits[3], _digits[4], _digits[5],
                   ns_device_projection_clock::ledIndex(0, _digits[0]),
                   ns_device_projection_clock::ledIndex(1, _digits[1]),
                   ns_device_projection_clock::ledIndex(2, _digits[2]),
                   ns_device_projection_clock::ledIndex(3, _digits[3]),
                   ns_device_projection_clock::ledIndex(4, _digits[4]),
                   ns_device_projection_clock::ledIndex(5, _digits[5]),
                   core_sys.timeValid() ? 1 : 0, _view);
}

// ============================================================
// Переходы смены минуты/часа
// ============================================================
bool CLASS_DEVICE_PROJ_CLOCK::transitionActive() const {
    return (_view == PROJ_CLOCK_VIEW_TRANS);
}

void CLASS_DEVICE_PROJ_CLOCK::startTransition(uint8_t type, uint8_t durSec, bool hourChange) {
    if (_config.busMode == PROJ_CLOCK_BUS_OFF) { return; }
    if (durSec < 1) { durSec = 1; }
    if (durSec > PROJ_CLOCK_DUR_MAX) { durSec = PROJ_CLOCK_DUR_MAX; }

    refreshTimeDigits(_toDigits);

    if (type == PROJ_CLOCK_TFX_OFF) {
        _view = PROJ_CLOCK_VIEW_NORMAL;
        _transType = PROJ_CLOCK_TFX_OFF;
        memcpy(_digits, _toDigits, sizeof(_digits));
        float levels[PROJ_CLOCK_DIGITS];
        for (uint8_t i = 0; i < PROJ_CLOCK_DIGITS; i++) { levels[i] = 1.0f; }
        renderFrame(_digits, levels, false);
        time_t t = core_sys.timeNow();
        _lastH = (uint8_t)hour(t);
        _lastM = (uint8_t)minute(t);
        _lastS = (uint8_t)second(t);
        _forceRedraw = false;
        return;
    }

    memcpy(_fromDigits, _digits, sizeof(_fromDigits));
    _transType = type;
    _transStartMs = millis();
    _transDurMs = (uint32_t)durSec * 1000u;
    _view = PROJ_CLOCK_VIEW_TRANS;
    _sweepActive = false;
    _rainForced = false;

    for (uint8_t p = 0; p < PROJ_CLOCK_DIGITS; p++) {
        _transRand[p] = (uint8_t)(ns_device_projection_clock::rand32() % 255);
        _spinFrom[p] = ns_device_projection_clock::digitToLed(_fromDigits[p]);
        uint8_t toIdx = ns_device_projection_clock::digitToLed(_toDigits[p]);
        _spinDelta[p] = (uint8_t)((toIdx + PROJ_CLOCK_LEDS_PER_DIGIT - _spinFrom[p]) %
                                  PROJ_CLOCK_LEDS_PER_DIGIT);
    }

    time_t t = core_sys.timeNow();
    _lastH = (uint8_t)hour(t);
    _lastM = (uint8_t)minute(t);
    _lastS = (uint8_t)second(t);

    DEBUGPROJCLOCK("transition %s for %us (%s) from=%d%d%d%d%d%d to=%d%d%d%d%d%d\r\n",
                   ns_device_projection_clock::tfxName(type), durSec,
                   hourChange ? "hour" : "minute",
                   _fromDigits[0], _fromDigits[1], _fromDigits[2],
                   _fromDigits[3], _fromDigits[4], _fromDigits[5],
                   _toDigits[0], _toDigits[1], _toDigits[2],
                   _toDigits[3], _toDigits[4], _toDigits[5]);

    drawTransition();
}

void CLASS_DEVICE_PROJ_CLOCK::drawTransition() {
    if (transitionActive() == false) { return; }

    uint32_t elapsed = millis() - _transStartMs;
    float t = (float)elapsed / (float)_transDurMs;
    t = projClamp01(t);

    uint8_t digits[PROJ_CLOCK_DIGITS];
    float levels[PROJ_CLOCK_DIGITS];
    bool white = false;

    for (uint8_t p = 0; p < PROJ_CLOCK_DIGITS; p++) {
        uint8_t from = _fromDigits[p];
        uint8_t to = _toDigits[p];
        digits[p] = to;
        levels[p] = 1.0f;

        switch (_transType) {
            case PROJ_CLOCK_TFX_FADE_ALL:
                if (t < 0.5f) {
                    digits[p] = from;
                    levels[p] = 1.0f - 2.0f * t;
                } else {
                    digits[p] = to;
                    levels[p] = 2.0f * t - 1.0f;
                }
                break;

            case PROJ_CLOCK_TFX_FADE_SEQ: {
                float seg = 1.0f / (float)PROJ_CLOCK_DIGITS;
                float local = (t - (float)p * seg) / seg;
                local = projClamp01(local);
                if (local <= 0.0f) {
                    digits[p] = from;
                    levels[p] = 1.0f;
                } else if (local >= 1.0f) {
                    digits[p] = to;
                    levels[p] = 1.0f;
                } else if (local < 0.5f) {
                    digits[p] = from;
                    levels[p] = 1.0f - 2.0f * local;
                } else {
                    digits[p] = to;
                    levels[p] = 2.0f * local - 1.0f;
                }
            } break;

            case PROJ_CLOCK_TFX_WIPE_LR:
                digits[p] = ((float)p < t * (float)PROJ_CLOCK_DIGITS) ? to : from;
                break;

            case PROJ_CLOCK_TFX_WIPE_RL:
                digits[p] = ((float)p >= (float)PROJ_CLOCK_DIGITS -
                                         t * (float)PROJ_CLOCK_DIGITS) ? to : from;
                break;

            case PROJ_CLOCK_TFX_DISSOLVE:
                digits[p] = ((float)_transRand[p] < t * 255.0f) ? to : from;
                break;

            case PROJ_CLOCK_TFX_FLASH:
                if (t < 0.45f) {
                    digits[p] = from;
                } else if (t < 0.65f) {
                    digits[p] = to;
                    levels[p] = 1.0f;
                    white = true;
                } else {
                    digits[p] = to;
                }
                break;

            case PROJ_CLOCK_TFX_SPIN: {
                if (from == to) {
                    digits[p] = to;
                } else {
                    uint8_t steps = (uint8_t)(PROJ_CLOCK_LEDS_PER_DIGIT + _spinDelta[p]);
                    uint8_t idx = (uint8_t)((_spinFrom[p] + (uint8_t)(t * (float)steps)) %
                                            PROJ_CLOCK_LEDS_PER_DIGIT);
                    digits[p] = ns_device_projection_clock::ledToDigit(idx);
                }
            } break;

            case PROJ_CLOCK_TFX_SLOT:
                if (t < 0.8f) {
                    digits[p] = (((int)(t * 10.0f)) & 1) ? to : from;
                } else {
                    digits[p] = to;
                }
                break;

            case PROJ_CLOCK_TFX_SWEEP: {
                float wave = t * (float)(PROJ_CLOCK_DIGITS + 1) - 1.0f;
                if ((float)p <= wave) {
                    digits[p] = to;
                    levels[p] = 1.0f;
                } else {
                    digits[p] = from;
                    levels[p] = 0.65f;
                }
            } break;

            case PROJ_CLOCK_TFX_CROSSFADE: {
                if (from == to) {
                    digits[p] = to;
                    levels[p] = 1.0f;
                } else {
                    float stagger = 0.06f;
                    float local = (t - (float)p * stagger) / (1.0f - stagger * 5.0f);
                    local = projClamp01(local);
                    if (local < 0.5f) {
                        digits[p] = from;
                        levels[p] = 1.0f - 2.0f * local;
                    } else {
                        digits[p] = to;
                        levels[p] = 2.0f * local - 1.0f;
                    }
                }
            } break;

            case PROJ_CLOCK_TFX_OFF:
            default:
                digits[p] = to;
                levels[p] = 1.0f;
                break;
        }

        levels[p] = projClamp01(levels[p]);
    }

    renderFrame(digits, levels, white);

    if (t >= 1.0f) {
        _view = PROJ_CLOCK_VIEW_NORMAL;
        _transType = PROJ_CLOCK_TFX_OFF;
        time_t nt = core_sys.timeNow();
        _lastH = (uint8_t)hour(nt);
        _lastM = (uint8_t)minute(nt);
        _lastS = (uint8_t)second(nt);
        refreshTimeDigits(_digits);
        float lv[PROJ_CLOCK_DIGITS];
        for (uint8_t i = 0; i < PROJ_CLOCK_DIGITS; i++) { lv[i] = 1.0f; }
        renderFrame(_digits, lv, false);
        _forceRedraw = false;
        DEBUGPROJCLOCK("transition done\r\n");
    }
}

// ============================================================
// «Дождь» и фиксация
// ============================================================
bool CLASS_DEVICE_PROJ_CLOCK::rainActive() const {
    return (_view == PROJ_CLOCK_VIEW_RAIN || _view == PROJ_CLOCK_VIEW_SETTLE);
}

void CLASS_DEVICE_PROJ_CLOCK::startRain(bool forced) {
    if (_config.busMode == PROJ_CLOCK_BUS_OFF) { return; }
    _view = PROJ_CLOCK_VIEW_RAIN;
    _rainForced = forced;
    _rainStartMs = millis();
    _sweepActive = false;
    for (uint8_t p = 0; p < PROJ_CLOCK_DIGITS; p++) {
        _rainDigits[p] = (uint8_t)(ns_device_projection_clock::rand32() % 10);
        _rainNextMs[p] = _rainStartMs + (ns_device_projection_clock::rand32() % 200);
    }
    DEBUGPROJCLOCK("rain start (forced=%d, max=%us)\r\n", forced ? 1 : 0, _config.rainDurMin);
    drawRain();
}

void CLASS_DEVICE_PROJ_CLOCK::drawRain() {
    if (_view != PROJ_CLOCK_VIEW_RAIN) { return; }
    uint32_t now = millis();
    for (uint8_t p = 0; p < PROJ_CLOCK_DIGITS; p++) {
        if ((int32_t)(now - _rainNextMs[p]) < 0) { continue; }
        uint8_t nd = (uint8_t)(ns_device_projection_clock::rand32() % 10);
        if (nd == _rainDigits[p]) { nd = (uint8_t)((nd + 1) % 10); }
        _rainDigits[p] = nd;
        _rainNextMs[p] = now + projRainIntervalMs(_config.rainIntensity);
    }
    float levels[PROJ_CLOCK_DIGITS];
    for (uint8_t i = 0; i < PROJ_CLOCK_DIGITS; i++) { levels[i] = 1.0f; }
    renderFrame(_rainDigits, levels, false);
}

void CLASS_DEVICE_PROJ_CLOCK::startSettle() {
    if (_config.busMode == PROJ_CLOCK_BUS_OFF) { return; }
    _view = PROJ_CLOCK_VIEW_SETTLE;
    _settleStartMs = millis();
    _rainForced = false;
    refreshTimeDigits(_toDigits);

    uint8_t order[PROJ_CLOCK_DIGITS];
    for (uint8_t i = 0; i < PROJ_CLOCK_DIGITS; i++) { order[i] = i; }
    ns_device_projection_clock::shuffleOrder(order, PROJ_CLOCK_DIGITS);
    for (uint8_t i = 0; i < PROJ_CLOCK_DIGITS; i++) {
        _settleThreshold[order[i]] = (uint8_t)(i + 1);
    }
    for (uint8_t p = 0; p < PROJ_CLOCK_DIGITS; p++) {
        _rainNextMs[p] = _settleStartMs;
    }
    DEBUGPROJCLOCK("settle start (%us) target=%d%d:%d%d:%d%d\r\n",
                   _config.rainSettleDur,
                   _toDigits[0], _toDigits[1], _toDigits[2],
                   _toDigits[3], _toDigits[4], _toDigits[5]);
    drawSettle();
}

void CLASS_DEVICE_PROJ_CLOCK::drawSettle() {
    if (_view != PROJ_CLOCK_VIEW_SETTLE) { return; }
    uint32_t now = millis();
    uint32_t dur = (uint32_t)_config.rainSettleDur * 1000u;
    float t = (dur == 0) ? 1.0f : (float)(now - _settleStartMs) / (float)dur;
    t = projClamp01(t);

    uint8_t digits[PROJ_CLOCK_DIGITS];
    float levels[PROJ_CLOCK_DIGITS];
    for (uint8_t p = 0; p < PROJ_CLOCK_DIGITS; p++) {
        levels[p] = 1.0f;
        if (!_rainForced && t * (float)PROJ_CLOCK_DIGITS >= (float)_settleThreshold[p]) {
            digits[p] = _toDigits[p];
        } else {
            if ((int32_t)(now - _rainNextMs[p]) >= 0) {
                uint8_t nd = (uint8_t)(ns_device_projection_clock::rand32() % 10);
                if (nd == _rainDigits[p]) { nd = (uint8_t)((nd + 1) % 10); }
                _rainDigits[p] = nd;
                _rainNextMs[p] = now + projRainIntervalMs(_config.rainIntensity);
            }
            digits[p] = _rainDigits[p];
        }
    }
    renderFrame(digits, levels, false);
}

// ============================================================
// Отладочный перебор LED
// ============================================================
void CLASS_DEVICE_PROJ_CLOCK::updateSweep() {
    if (_sweepActive == false) { return; }
    uint32_t now = millis();
    if ((uint32_t)(now - _sweepLastMs) < PROJ_CLOCK_SWEEP_STEP_MS) { return; }
    _sweepLastMs = now;

    uint8_t limit = _sweepAll ? PROJ_CLOCK_LEDS : PROJ_CLOCK_LEDS_PER_DIGIT;
    if (_sweepIdx >= limit) {
        _sweepActive = false;
        _sweepIdx = 0;
        _forceRedraw = true;
        DEBUGPROJCLOCK("sweep done\r\n");
        return;
    }

    uint8_t local = _sweepIdx;
    uint8_t block = _sweepAll ? (uint8_t)(local / PROJ_CLOCK_LEDS_PER_DIGIT) : _sweepBlock;
    uint8_t inBlock = _sweepAll ? (uint8_t)(local % PROJ_CLOCK_LEDS_PER_DIGIT) : local;
    uint16_t global = (uint16_t)block * PROJ_CLOCK_LEDS_PER_DIGIT + inBlock;
    uint8_t digit = ns_device_projection_clock::ledToDigit(inBlock);
    uint32_t color = ns_device_projection_clock::fxColor(_config, block, _animPhase, _fxOrder);

    _strip.clear();
    _strip.setLed(global, color, _config.brightness);
    _strip.show();
    DEBUGPROJCLOCK("sweep led=%u block=%u digit=%u\r\n", global, block + 1, digit);
    _sweepIdx++;
}

// ============================================================
// Отложенное применение изменений (main-loop)
// ============================================================
void CLASS_DEVICE_PROJ_CLOCK::deferredApplyTask() {
    CLASS_DEVICE_PROJ_CLOCK& d = proj_clock;

    if (d._pendingReinit) {
        d._config.dataPin = d._pendingDataPin;
        d.destroyStrip();
        d.initStrip();
        d.saveConfig();
        d._pendingReinit = false;
        d._pendingSave = false;
        d._pendingApply = true;
        DEBUGPROJCLOCK("strip reinit dataPin=%d\r\n", d._config.dataPin);
    }

    if (d._pendingSave) {
        d.saveConfig();
        d._pendingSave = false;
    }

    if (d._pendingApply) {
        d._sweepActive = false;
        d._pendingTest = PROJ_CLOCK_TEST_NONE;
        d._rainForced = false;
        d._view = PROJ_CLOCK_VIEW_NORMAL;
        d._transType = PROJ_CLOCK_TFX_OFF;

        if (d._config.busMode == PROJ_CLOCK_BUS_OFF) {
            d._strip.clear();
            d._strip.show();
        } else if (d._config.mode == PROJ_CLOCK_MODE_MANUAL) {
            d.renderManual();
        } else {
            time_t t = core_sys.timeNow();
            d._lastH = (uint8_t)hour(t);
            d._lastM = (uint8_t)minute(t);
            d._lastS = (uint8_t)second(t);
            d.refreshTimeDigits(d._digits);
            d.renderNow();
            d.logFrame();
        }
        d._pendingApply = false;
        d._forceRedraw = false;
    }
}

// ============================================================
// Задача 1 с: время, смена минуты/часа, дождь/фиксация
// ============================================================
void projClockSecondTask() {
    CLASS_DEVICE_PROJ_CLOCK& d = proj_clock;

    if (d._config.busMode == PROJ_CLOCK_BUS_OFF) {
        SetTimerTask(projClockSecondTask, 1000);
        return;
    }
    if (d._sweepActive) {
        SetTimerTask(projClockSecondTask, 1000);
        return;
    }
    if (d._config.mode == PROJ_CLOCK_MODE_MANUAL) {
        SetTimerTask(projClockSecondTask, 1000);
        return;
    }

    uint32_t now = millis();
    bool valid = core_sys.timeValid();
    time_t t = core_sys.timeNow();
    uint8_t h = (uint8_t)hour(t);
    uint8_t m = (uint8_t)minute(t);
    uint8_t s = (uint8_t)second(t);

    if (valid && d._hadValidTime == false) {
        d._hadValidTime = true;
        if (d._view == PROJ_CLOCK_VIEW_RAIN && d._rainForced == false) {
            d.startSettle();
        } else if (d._view == PROJ_CLOCK_VIEW_NORMAL) {
            uint8_t fx = (h != d._lastH) ? d._config.hourFx : d._config.minuteFx;
            uint8_t dur = (h != d._lastH) ? d._config.hourFxDur : d._config.minuteFxDur;
            if (fx != PROJ_CLOCK_TFX_OFF && dur > 0) {
                d.startTransition(fx, dur, h != d._lastH);
            } else {
                d.refreshTimeDigits(d._digits);
                d.renderNow();
                d.logFrame();
            }
            d._lastH = h;
            d._lastM = m;
            d._lastS = s;
        }
    }

    if (d._view == PROJ_CLOCK_VIEW_RAIN) {
        if (d._rainForced == false && valid) {
            d.startSettle();
        } else if ((uint32_t)(now - d._rainStartMs) >=
                   (uint32_t)d._config.rainDurMin * 1000u) {
            d._view = PROJ_CLOCK_VIEW_NORMAL;
            d._rainForced = false;
            d._lastH = h;
            d._lastM = m;
            d._lastS = s;
            d.refreshTimeDigits(d._digits);
            d.renderNow();
            d.logFrame();
            DEBUGPROJCLOCK("rain timeout -> fallback clock\r\n");
        }
    } else if (d._view == PROJ_CLOCK_VIEW_SETTLE) {
        uint32_t dur = (uint32_t)d._config.rainSettleDur * 1000u;
        if (dur == 0 || (uint32_t)(now - d._settleStartMs) >= dur) {
            d._view = PROJ_CLOCK_VIEW_NORMAL;
            d._lastH = h;
            d._lastM = m;
            d._lastS = s;
            d.refreshTimeDigits(d._digits);
            d.renderNow();
            d.logFrame();
            DEBUGPROJCLOCK("settle done\r\n");
        }
    } else if (d._view == PROJ_CLOCK_VIEW_TRANS) {
        // Завершение перехода выполняет drawTransition() в anim-задаче.
    } else {
        bool hourChanged = (h != d._lastH);
        bool minuteChanged = (m != d._lastM);
        bool secondChanged = (s != d._lastS);

        if (hourChanged) {
            d._lastH = h;
            d._lastM = m;
            d._lastS = s;
            if (d._config.hourFx != PROJ_CLOCK_TFX_OFF && d._config.hourFxDur > 0) {
                d.startTransition(d._config.hourFx, d._config.hourFxDur, true);
            } else {
                d.refreshTimeDigits(d._digits);
                d.renderNow();
                d.logFrame();
            }
        } else if (minuteChanged) {
            bool doFx = (d._config.minuteFx != PROJ_CLOCK_TFX_OFF) &&
                        (d._config.minuteFxFreq > 0) &&
                        ((m % d._config.minuteFxFreq) == 0);
            d._lastH = h;
            d._lastM = m;
            d._lastS = s;
            if (doFx && d._config.minuteFxDur > 0) {
                d.startTransition(d._config.minuteFx, d._config.minuteFxDur, false);
            } else {
                d.refreshTimeDigits(d._digits);
                d.renderNow();
                d.logFrame();
            }
        } else if (secondChanged || d._forceRedraw) {
            d._lastH = h;
            d._lastM = m;
            d._lastS = s;
            d.refreshTimeDigits(d._digits);
            d.renderNow();
            d.logFrame();
        }
    }

    SetTimerTask(projClockSecondTask, 1000);
}

// ============================================================
// Задача 50 мс: цветовая фаза, анимации, дождь, sweep
// ============================================================
void projClockAnimTask() {
    CLASS_DEVICE_PROJ_CLOCK& d = proj_clock;

    if (d._config.busMode == PROJ_CLOCK_BUS_OFF) {
        SetTimerTask(projClockAnimTask, PROJ_CLOCK_RENDER_MS);
        return;
    }

    if (d._pendingTest != PROJ_CLOCK_TEST_NONE) {
        uint8_t test = d._pendingTest;
        d._pendingTest = PROJ_CLOCK_TEST_NONE;
        if (test == PROJ_CLOCK_TEST_FX_MINUTE) {
            d.refreshTimeDigits(d._toDigits);
            d.startTransition(d._config.minuteFx, d._config.minuteFxDur, false);
        } else if (test == PROJ_CLOCK_TEST_FX_HOUR) {
            d.refreshTimeDigits(d._toDigits);
            d.startTransition(d._config.hourFx, d._config.hourFxDur, true);
        } else if (test == PROJ_CLOCK_TEST_RAIN) {
            d.startRain(true);
        }
    }

    // Отладочный «дождь» в ручном режиме: вторая задача в manual не работает,
    // поэтому ограничиваем его по времени здесь.
    if (d._view == PROJ_CLOCK_VIEW_RAIN && d._rainForced &&
        (uint32_t)(millis() - d._rainStartMs) >= (uint32_t)d._config.rainDurMin * 1000u) {
        d._view = PROJ_CLOCK_VIEW_NORMAL;
        d._rainForced = false;
        d._forceRedraw = true;
    }

    if (d._sweepActive) {
        d.updateSweep();
        SetTimerTask(projClockAnimTask, PROJ_CLOCK_RENDER_MS);
        return;
    }

    bool phaseAdvanced = false;
    d._fxAccumMs += PROJ_CLOCK_RENDER_MS;
    if (d._fxAccumMs >= (uint32_t)PROJ_CLOCK_ANIM_MS) {
        d._fxAccumMs -= PROJ_CLOCK_ANIM_MS;
        phaseAdvanced = true;

        float next = d._animPhase + (float)d._config.animSpeed;
        if (next >= 360.0f) {
            next -= 360.0f;
            d._fxLap++;
            if (d._config.effect == PROJ_CLOCK_EFFECT_COLORCYCLE &&
                d._config.cycleMode == PROJ_CLOCK_CYCLE_RANDOM) {
                uint8_t n = d._config.colorsCount;
                if (n < 1) { n = 1; }
                if (n > PROJ_CLOCK_MAX_COLORS) { n = PROJ_CLOCK_MAX_COLORS; }
                ns_device_projection_clock::shuffleOrder(d._fxOrder, n);
            }
        }
        d._animPhase = next;
    }

    if (d._view == PROJ_CLOCK_VIEW_RAIN) {
        d.drawRain();
    } else if (d._view == PROJ_CLOCK_VIEW_SETTLE) {
        d.drawSettle();
    } else if (d._view == PROJ_CLOCK_VIEW_TRANS) {
        d.drawTransition();
    } else if (d._config.mode == PROJ_CLOCK_MODE_MANUAL) {
        if (d._forceRedraw ||
            (phaseAdvanced && ns_device_projection_clock::fxAnimated(d._config.effect))) {
            d.renderManual();
        }
    } else {
        if (d._forceRedraw ||
            (phaseAdvanced && ns_device_projection_clock::fxAnimated(d._config.effect))) {
            d.renderNow();
        }
    }

    SetTimerTask(projClockAnimTask, PROJ_CLOCK_RENDER_MS);
}
