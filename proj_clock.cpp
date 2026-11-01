#include "core_web/FSWebServerLib.h"

#include "core_json/core_json.h"
#include "core_json/common_module.h"

#include "core_sys/core_sys.h"

#include "proj_clock.h"
#include "common_module.h"
#include "common/common.h"
#include "common/TimeLib.h"
#include "device_projection_clock_version.h"
#include "core_state/core_state.h"
#include "core_sys/eertos.h"

// ============================================================
// Глобальные объекты и переменные
// ============================================================

CLASS_DEVICE_PROJ_CLOCK proj_clock;

CLASS_DEVICE_PROJ_CLOCK::CLASS_DEVICE_PROJ_CLOCK() {
    _fs = NULL;
    _pendingSave = false;
    _pendingApply = false;
    _pendingReinit = false;
    _pendingDataPin = -1;
    _pendingTest = PROJ_CLOCK_TEST_NONE;

    _view = PROJ_CLOCK_VIEW_NORMAL;
    memset(_digits, 0, sizeof(_digits));
    memset(_fromDigits, 0, sizeof(_fromDigits));
    memset(_toDigits, 0, sizeof(_toDigits));
    _lastH = _lastM = _lastS = 0;
    _hadValidTime = false;
    _forceRedraw = false;

    _transType = PROJ_CLOCK_TFX_OFF;
    _transStartMs = 0;
    _transDurMs = 0;
    memset(_transRand, 0, sizeof(_transRand));
    memset(_spinFrom, 0, sizeof(_spinFrom));
    memset(_spinDelta, 0, sizeof(_spinDelta));

    _rainStartMs = 0;
    memset(_rainNextMs, 0, sizeof(_rainNextMs));
    memset(_rainDigits, 0, sizeof(_rainDigits));
    _rainForced = false;
    _settleStartMs = 0;
    memset(_settleThreshold, 0, sizeof(_settleThreshold));

    _animPhase = 0.0f;
    _fxAccumMs = 0;
    _fxLap = 0;
    for (uint8_t i = 0; i < PROJ_CLOCK_MAX_COLORS; i++) { _fxOrder[i] = i; }

    _sweepActive = false;
    _sweepAll = true;
    _sweepBlock = 0;
    _sweepIdx = 0;
    _sweepLastMs = 0;
}

// Forward-объявления свободных функций, используемых в шаблонном блоке
void projClockSecondTask();
void projClockAnimTask();

// ============================================================
// setFs()
// ============================================================
void CLASS_DEVICE_PROJ_CLOCK::setFs(fs::LittleFSFS* fs) {
    _fs = fs;
}

// ============================================================
// begin()
// ============================================================
void CLASS_DEVICE_PROJ_CLOCK::begin() {
    DEBUGPROJCLOCK("%s\r\n", __FUNCTION__);

    _pendingSave = false;
    _pendingApply = false;
    _pendingReinit = false;
    _pendingDataPin = -1;
    _pendingTest = PROJ_CLOCK_TEST_NONE;
    _forceRedraw = false;
    _sweepActive = false;
    _rainForced = false;
    _view = PROJ_CLOCK_VIEW_NORMAL;
    _transType = PROJ_CLOCK_TFX_OFF;
    _animPhase = 0.0f;
    _fxAccumMs = 0;
    _fxLap = 0;
    for (uint8_t i = 0; i < PROJ_CLOCK_MAX_COLORS; i++) { _fxOrder[i] = i; }
    ns_device_projection_clock::seedRng();

    defaultConfig();
    if (loadConfig() == false) { saveConfig(); }

    core_state.signal("proj_clock.mode", BusValue::en(_config.busMode));
    core_state.signal("proj_clock.effect", BusValue::en(_config.effect));
    core_state.signal("proj_clock.brightness", BusValue::i32(_config.brightness));
    core_state.signal("proj_clock.color", BusValue::str(ns_core_json::colorToHex(_config.digitsColor)));
    core_state.signal("proj_clock.hour_format", BusValue::en(_config.hourFormat));
    core_state.signal("proj_clock.text", BusValue::str(_config.manualText));
    core_state.signal("proj_clock.minute_fx", BusValue::i32(_config.minuteFx));
    core_state.signal("proj_clock.hour_fx", BusValue::i32(_config.hourFx));

    initStrip();
    if (_strip.ready() == false) {
        DEBUGPROJCLOCK("strip init failed (dataPin=%d)\r\n", _config.dataPin);
    }

    time_t now = core_sys.timeNow();
    _lastH = (uint8_t)hour(now);
    _lastM = (uint8_t)minute(now);
    _lastS = (uint8_t)second(now);
    _hadValidTime = core_sys.timeValid();

    if (_config.busMode == PROJ_CLOCK_BUS_OFF) {
        _strip.clear();
        _strip.show();
    } else if (_config.mode == PROJ_CLOCK_MODE_MANUAL) {
        renderManual();
    } else if (_config.rainEnabled == 1 && core_sys.timeValid() == false) {
        startRain(false);
    } else {
        refreshTimeDigits(_digits);
        renderNow();
    }

    DEBUGPROJCLOCK("dataPin=%d brightness=%d format=%s effect=%s minuteFx=%s/%us hourFx=%s/%us rain=%u\r\n",
                   _config.dataPin, _config.brightness,
                   ns_device_projection_clock::hourFormatName(_config.hourFormat),
                   ns_device_projection_clock::effectName(_config.effect),
                   ns_device_projection_clock::tfxName(_config.minuteFx), _config.minuteFxDur,
                   ns_device_projection_clock::tfxName(_config.hourFx), _config.hourFxDur,
                   _config.rainEnabled);

    SetTimerTask(projClockSecondTask, 1000);
    SetTimerTask(projClockAnimTask, PROJ_CLOCK_RENDER_MS);
}

void CLASS_DEVICE_PROJ_CLOCK::begin(ModContext& ctx) {
    _fs = ctx.fs;
    begin();
}

// ============================================================
// register_resources() — публикация ресурсов на шине core_state
// ============================================================
static const char* const projBusModeNames[3] = { "off", "auto", "macro" };
static const char* const projEffectNames[PROJ_CLOCK_EFFECT_COUNT] = {
    "mono", "rainbow", "grad_static", "grad_dynamic", "colorcycle",
};
static const char* const projHourFormatNames[2] = { "24h", "12h" };

static int projArgInt(const BusValue* a) {
    if (a == nullptr) { return 0; }
    if (a->kind == BusValue::F32) { return (int)a->f; }
    if (a->kind == BusValue::BOOL) { return a->b ? 1 : 0; }
    return (int)a->i;
}

// Bus-запись (кроме mode и save_mode) разрешена только в режиме MACRO.
static bool projBusAllowed() {
    return proj_clock.getBusMode() == PROJ_CLOCK_BUS_MACRO;
}

static int projBusEffect(void*, int argc, const BusValue* a, BusValue&) {
    if (projBusAllowed() == false) { return BUS_ERR_DENIED; }
    if (argc < 1) { return BUS_ERR_BAD_ARGC; }
    return proj_clock.setEffect((uint8_t)projArgInt(&a[0])) ? BUS_OK : BUS_ERR_BAD_VALUE;
}
static int projBusBrightness(void*, int argc, const BusValue* a, BusValue&) {
    if (projBusAllowed() == false) { return BUS_ERR_DENIED; }
    if (argc < 1) { return BUS_ERR_BAD_ARGC; }
    return proj_clock.setBrightness((uint8_t)projArgInt(&a[0])) ? BUS_OK : BUS_ERR_BAD_VALUE;
}
static int projBusColor(void*, int argc, const BusValue* a, BusValue&) {
    if (projBusAllowed() == false) { return BUS_ERR_DENIED; }
    if (argc < 1) { return BUS_ERR_BAD_ARGC; }
    if (a[0].kind != BusValue::STR) { return BUS_ERR_BAD_VALUE; }
    uint32_t color = 0;
    if (ns_core_json::colorParseHex(a[0].s, color) == false) { return BUS_ERR_BAD_VALUE; }
    return proj_clock.setColor(color) ? BUS_OK : BUS_ERR_BAD_VALUE;
}
static int projBusHourFormat(void*, int argc, const BusValue* a, BusValue&) {
    if (projBusAllowed() == false) { return BUS_ERR_DENIED; }
    if (argc < 1) { return BUS_ERR_BAD_ARGC; }
    return proj_clock.setHourFormat((uint8_t)projArgInt(&a[0])) ? BUS_OK : BUS_ERR_BAD_VALUE;
}
static int projBusText(void*, int argc, const BusValue* a, BusValue&) {
    if (projBusAllowed() == false) { return BUS_ERR_DENIED; }
    if (argc < 1) { return BUS_ERR_BAD_ARGC; }
    return proj_clock.setManualText(a[0].s) ? BUS_OK : BUS_ERR_BAD_VALUE;
}
static int projBusMinuteFx(void*, int argc, const BusValue* a, BusValue&) {
    if (projBusAllowed() == false) { return BUS_ERR_DENIED; }
    if (argc < 1) { return BUS_ERR_BAD_ARGC; }
    return proj_clock.setMinuteFx((uint8_t)projArgInt(&a[0])) ? BUS_OK : BUS_ERR_BAD_VALUE;
}
static int projBusHourFx(void*, int argc, const BusValue* a, BusValue&) {
    if (projBusAllowed() == false) { return BUS_ERR_DENIED; }
    if (argc < 1) { return BUS_ERR_BAD_ARGC; }
    return proj_clock.setHourFx((uint8_t)projArgInt(&a[0])) ? BUS_OK : BUS_ERR_BAD_VALUE;
}
static int projBusMode(void*, int argc, const BusValue* a, BusValue&) {
    if (argc < 1) { return BUS_ERR_BAD_ARGC; }
    return proj_clock.setBusMode((uint8_t)projArgInt(&a[0])) ? BUS_OK : BUS_ERR_BAD_VALUE;
}
static int projBusSave(void*, int argc, const BusValue* a, BusValue&) {
    if (projBusAllowed() == false) { return BUS_ERR_DENIED; }
    (void)argc; (void)a;
    proj_clock.saveNow();
    return BUS_OK;
}
static int projBusSaveMode(void*, int argc, const BusValue* a, BusValue&) {
    (void)argc; (void)a;
    return proj_clock.saveModeConfig() ? BUS_OK : BUS_ERR_INTERNAL;
}

void CLASS_DEVICE_PROJ_CLOCK::register_resources() {
    DEBUGPROJCLOCK("%s\r\n", __FUNCTION__);

    core_state.regEnum("mode", 3, projBusModeNames, "device control mode (off/auto/macro)");
    core_state.regEnum("effect", PROJ_CLOCK_EFFECT_COUNT, projEffectNames, "color effect");
    core_state.regState("brightness", BusValue::I32, "brightness 0..255", true);
    core_state.regState("color", BusValue::STR, "digits color #RRGGBB", true);
    core_state.regEnum("hour_format", 2, projHourFormatNames, "hour format 24h/12h");
    core_state.regState("text", BusValue::STR, "manual 6 digits", true);
    // Ядро ограничивает ENUM 8 значениями (CORE_STATE_MAX_ENUM), а
    // переходных эффектов 11 — публикуем их как I32 с проверкой в setter.
    core_state.regState("minute_fx", BusValue::I32, "minute transition effect 0..10", true);
    core_state.regState("hour_fx", BusValue::I32, "hour transition effect 0..10", true);

    core_state.regFunc("mode", "i->", "set control mode", projBusMode, nullptr);
    core_state.regFunc("effect", "i->", "set color effect", projBusEffect, nullptr);
    core_state.regFunc("brightness", "i->", "set brightness", projBusBrightness, nullptr);
    core_state.regFunc("color", "s->", "set digits color", projBusColor, nullptr);
    core_state.regFunc("hour_format", "i->", "set hour format", projBusHourFormat, nullptr);
    core_state.regFunc("text", "s->", "set manual 6 digits", projBusText, nullptr);
    core_state.regFunc("minute_fx", "i->", "set minute transition", projBusMinuteFx, nullptr);
    core_state.regFunc("hour_fx", "i->", "set hour transition", projBusHourFx, nullptr);
    core_state.regFunc("save", "->", "persist current settings to config", projBusSave, nullptr);
    core_state.regFunc("save_mode", "->", "persist only module mode to config", projBusSaveMode, nullptr);
}

// ============================================================
// Публичные сеттеры (apply без persist)
// ============================================================
bool CLASS_DEVICE_PROJ_CLOCK::setEffect(uint8_t e) {
    if (e >= PROJ_CLOCK_EFFECT_COUNT) { return false; }
    _config.effect = e;
    _forceRedraw = true;
    _pendingApply = true;
    SetTask(deferredApplyTask);
    core_state.signal("proj_clock.effect", BusValue::en(e));
    return true;
}

bool CLASS_DEVICE_PROJ_CLOCK::setBrightness(uint8_t b) {
    _config.brightness = b;
    _forceRedraw = true;
    _pendingApply = true;
    SetTask(deferredApplyTask);
    core_state.signal("proj_clock.brightness", BusValue::i32(b));
    return true;
}

bool CLASS_DEVICE_PROJ_CLOCK::setColor(uint32_t c) {
    _config.digitsColor = c & 0xFFFFFF;
    _forceRedraw = true;
    _pendingApply = true;
    SetTask(deferredApplyTask);
    core_state.signal("proj_clock.color", BusValue::str(ns_core_json::colorToHex(_config.digitsColor)));
    return true;
}

bool CLASS_DEVICE_PROJ_CLOCK::setHourFormat(uint8_t f) {
    if (f > PROJ_CLOCK_HOUR_12) { return false; }
    _config.hourFormat = f;
    _forceRedraw = true;
    _pendingApply = true;
    SetTask(deferredApplyTask);
    core_state.signal("proj_clock.hour_format", BusValue::en(f));
    return true;
}

bool CLASS_DEVICE_PROJ_CLOCK::setManualText(const String& t) {
    if (t.length() == 0) { return false; }
    String out;
    ns_device_projection_clock::normalizeDigitsText(t, out);
    _config.manualText = out;
    _config.mode = PROJ_CLOCK_MODE_MANUAL;
    _view = PROJ_CLOCK_VIEW_NORMAL;
    _sweepActive = false;
    _forceRedraw = true;
    _pendingApply = true;
    SetTask(deferredApplyTask);
    core_state.signal("proj_clock.text", BusValue::str(out));
    return true;
}

bool CLASS_DEVICE_PROJ_CLOCK::setMinuteFx(uint8_t f) {
    if (f >= PROJ_CLOCK_TFX_COUNT) { return false; }
    _config.minuteFx = f;
    _forceRedraw = true;
    _pendingApply = true;
    SetTask(deferredApplyTask);
    core_state.signal("proj_clock.minute_fx", BusValue::i32(f));
    return true;
}

bool CLASS_DEVICE_PROJ_CLOCK::setHourFx(uint8_t f) {
    if (f >= PROJ_CLOCK_TFX_COUNT) { return false; }
    _config.hourFx = f;
    _forceRedraw = true;
    _pendingApply = true;
    SetTask(deferredApplyTask);
    core_state.signal("proj_clock.hour_fx", BusValue::i32(f));
    return true;
}

bool CLASS_DEVICE_PROJ_CLOCK::setBusMode(uint8_t m) {
    if (m > PROJ_CLOCK_BUS_MACRO) { return false; }
    _config.busMode = m;
    _forceRedraw = true;
    _pendingApply = true;
    SetTask(deferredApplyTask);
    core_state.signal("proj_clock.mode", BusValue::en(m));
    return true;
}

uint8_t CLASS_DEVICE_PROJ_CLOCK::getBusMode() {
    return _config.busMode;
}

void CLASS_DEVICE_PROJ_CLOCK::saveNow() {
    _pendingSave = true;
    SetTask(deferredApplyTask);
}

// ============================================================
// web_Init()
// ============================================================
void CLASS_DEVICE_PROJ_CLOCK::web_Init() {
    DEBUGPROJCLOCK("%s\r\n", __FUNCTION__);

    ESPHTTPServer.on("/proj_clock/save", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleSave(request);
    });

    ESPHTTPServer.on("/proj_clock/info", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleInfo(request);
    });

    ESPHTTPServer.on("/proj_clock/test", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleTest(request);
    });

    ESPHTTPServer.on("/proj_clock/ver", HTTP_GET, [this](AsyncWebServerRequest *request) {
        this->html_ver_get(request);
    });
}

// ============================================================
// Веб-обработчики
// ============================================================
void CLASS_DEVICE_PROJ_CLOCK::handleInfo(AsyncWebServerRequest *request) {
    DEBUGPROJCLOCK("%s\r\n", __FUNCTION__);

    uint8_t d[PROJ_CLOCK_DIGITS];
    if (_config.mode == PROJ_CLOCK_MODE_MANUAL) {
        for (uint8_t i = 0; i < PROJ_CLOCK_DIGITS; i++) {
            char c = (i < _config.manualText.length()) ? _config.manualText.charAt(i) : '0';
            d[i] = (c >= '0' && c <= '9') ? (uint8_t)(c - '0') : 0;
        }
    } else {
        ns_device_projection_clock::timeDigits(core_sys.timeNow(), _config.hourFormat, d);
    }

    String digits;
    String leds;
    for (uint8_t i = 0; i < PROJ_CLOCK_DIGITS; i++) {
        digits += (char)('0' + d[i]);
        if (i) { leds += ","; }
        leds += String(ns_device_projection_clock::ledIndex(i, d[i]));
    }

    const char* view = "normal";
    if (_sweepActive) { view = "sweep"; }
    else if (_config.busMode == PROJ_CLOCK_BUS_OFF) { view = "off"; }
    else if (_config.mode == PROJ_CLOCK_MODE_MANUAL) { view = "manual"; }
    else if (_view == PROJ_CLOCK_VIEW_RAIN) { view = "rain"; }
    else if (_view == PROJ_CLOCK_VIEW_SETTLE) { view = "settle"; }
    else if (_view == PROJ_CLOCK_VIEW_TRANS) { view = "trans"; }

    String values = "";
    values += "busMode|"       + String(_config.busMode)       + "|input\n";
    values += "mode|"          + String(_config.mode)          + "|input\n";
    values += "dataPin|"       + String(_config.dataPin)       + "|input\n";
    values += "brightness|"    + String(_config.brightness)    + "|input\n";
    values += "hourFormat|"    + String(_config.hourFormat)    + "|input\n";
    values += "effect|"        + String(_config.effect)        + "|input\n";
    values += "effectDir|"     + String(_config.effectDir)     + "|input\n";
    values += "digitsColor|"   + ns_core_json::colorToHex(_config.digitsColor) + "|input\n";
    values += "digitsColor2|"  + ns_core_json::colorToHex(_config.digitsColor2) + "|input\n";
    values += "animSpeed|"     + String(_config.animSpeed)     + "|input\n";
    values += "colorsCount|"   + String(_config.colorsCount)   + "|input\n";
    values += "cycleMode|"     + String(_config.cycleMode)     + "|input\n";
    for (uint8_t i = 0; i < PROJ_CLOCK_MAX_COLORS; i++) {
        values += "palette" + String(i) + "|" + ns_core_json::colorToHex(_config.palette[i]) + "|input\n";
    }
    values += "minuteFx|"      + String(_config.minuteFx)      + "|input\n";
    values += "minuteFxDur|"   + String(_config.minuteFxDur)   + "|input\n";
    values += "minuteFxFreq|"  + String(_config.minuteFxFreq)  + "|input\n";
    values += "hourFx|"        + String(_config.hourFx)        + "|input\n";
    values += "hourFxDur|"     + String(_config.hourFxDur)     + "|input\n";
    values += "rainEnabled|"   + String(_config.rainEnabled)   + "|input\n";
    values += "rainDurMin|"    + String(_config.rainDurMin)    + "|input\n";
    values += "rainIntensity|" + String(_config.rainIntensity) + "|input\n";
    values += "rainSettleDur|" + String(_config.rainSettleDur) + "|input\n";
    values += "manualText|"    + _config.manualText            + "|input\n";

    values += "x_ntp_sync|"    + String(core_sys.timeValid() ? 1 : 0) + "|div\n";
    values += "x_time|"        + ns_device_projection_clock::timeHhMmSs(core_sys.timeNow(), _config.hourFormat) + "|div\n";
    values += "x_digits|"      + digits + "|div\n";
    values += "x_leds|"        + leds + "|div\n";
    values += "x_view|"        + String(view) + "|div\n";
    values += "x_strip|"       + String(_strip.ready() ? 1 : 0) + "|div\n";
    values += "x_sweep|"       + String(_sweepActive ? (_sweepAll ? "all:" : "digit:") : "off");
    if (_sweepActive) { values += String(_sweepIdx); }
    values += "|div\n";
    values += "x_uptime|"      + String(millis() / 1000) + "|div\n";
    values += "x_heap|"        + String(ESP.getFreeHeap()) + "|div\n";

    request->send(200, "text/plain", values);
}

void CLASS_DEVICE_PROJ_CLOCK::handleSave(AsyncWebServerRequest *request) {
    DEBUGPROJCLOCK("%s\r\n", __FUNCTION__);

    if (request->args() == 0) { request->send(400, "text/plain", "No args"); return; }

    for (uint8_t i = 0; i < request->args(); i++) {
        String name = request->argName(i);
        String val  = request->arg(i);
        DEBUGPROJCLOCK("Arg %d: %s %s\r\n", i, name.c_str(), val.c_str());

        if (name == "busMode") {
            _config.busMode = (uint8_t)constrain(val.toInt(), PROJ_CLOCK_BUS_OFF, PROJ_CLOCK_BUS_MACRO);
            core_state.signal("proj_clock.mode", BusValue::en(_config.busMode));
            continue;
        }
        if (name == "mode") {
            _config.mode = (uint8_t)constrain(val.toInt(), PROJ_CLOCK_MODE_WORK, PROJ_CLOCK_MODE_MANUAL);
            continue;
        }
        if (name == "dataPin") {
            int16_t pin = (int16_t)val.toInt();
            if (pin >= -1 && pin < 34 && pin != _config.dataPin) {
                _pendingDataPin = pin;
                _pendingReinit = true;
            }
            continue;
        }
        if (name == "brightness") {
            _config.brightness = (uint8_t)constrain(val.toInt(), 0, 255);
            continue;
        }
        if (name == "hourFormat") {
            _config.hourFormat = (uint8_t)constrain(val.toInt(), PROJ_CLOCK_HOUR_24, PROJ_CLOCK_HOUR_12);
            core_state.signal("proj_clock.hour_format", BusValue::en(_config.hourFormat));
            continue;
        }
        if (name == "effect") {
            _config.effect = (uint8_t)constrain(val.toInt(), 0, PROJ_CLOCK_EFFECT_COUNT - 1);
            core_state.signal("proj_clock.effect", BusValue::en(_config.effect));
            continue;
        }
        if (name == "effectDir") {
            _config.effectDir = (uint8_t)constrain(val.toInt(), 0, PROJ_CLOCK_FXDIR_COUNT - 1);
            continue;
        }
        if (name == "colorsCount") {
            _config.colorsCount = (uint8_t)constrain(val.toInt(), 1, PROJ_CLOCK_MAX_COLORS);
            continue;
        }
        if (name == "cycleMode") {
            _config.cycleMode = (uint8_t)constrain(val.toInt(), PROJ_CLOCK_CYCLE_SEQUENTIAL, PROJ_CLOCK_CYCLE_RAINBOW);
            continue;
        }
        if (name == "animSpeed") {
            _config.animSpeed = (uint8_t)constrain(val.toInt(), 1, 50);
            continue;
        }
        if (name.startsWith("palette")) {
            int idx = name.substring(7).toInt();
            if (idx >= 0 && idx < PROJ_CLOCK_MAX_COLORS) {
                _config.palette[idx] = ns_core_json::colorParseOrWhite(val);
            }
            continue;
        }
        if (name == "digitsColor") {
            _config.digitsColor = ns_core_json::colorParseOrWhite(val);
            core_state.signal("proj_clock.color", BusValue::str(ns_core_json::colorToHex(_config.digitsColor)));
            continue;
        }
        if (name == "digitsColor2") {
            _config.digitsColor2 = ns_core_json::colorParseOrWhite(val);
            continue;
        }
        if (name == "minuteFx") {
            _config.minuteFx = (uint8_t)constrain(val.toInt(), 0, PROJ_CLOCK_TFX_COUNT - 1);
            core_state.signal("proj_clock.minute_fx", BusValue::i32(_config.minuteFx));
            continue;
        }
        if (name == "minuteFxDur") {
            _config.minuteFxDur = (uint8_t)constrain(val.toInt(), 1, PROJ_CLOCK_DUR_MAX);
            continue;
        }
        if (name == "minuteFxFreq") {
            int f = val.toInt();
            if (f != 1 && f != 3 && f != 5 && f != 10 && f != 15 && f != 20 && f != 40) {
                f = PROJ_CLOCK_TFX_FREQ_DEFAULT;
            }
            _config.minuteFxFreq = (uint8_t)f;
            continue;
        }
        if (name == "hourFx") {
            _config.hourFx = (uint8_t)constrain(val.toInt(), 0, PROJ_CLOCK_TFX_COUNT - 1);
            core_state.signal("proj_clock.hour_fx", BusValue::i32(_config.hourFx));
            continue;
        }
        if (name == "hourFxDur") {
            _config.hourFxDur = (uint8_t)constrain(val.toInt(), 1, PROJ_CLOCK_DUR_MAX);
            continue;
        }
        if (name == "rainEnabled") {
            _config.rainEnabled = (uint8_t)constrain(val.toInt(), 0, 1);
            continue;
        }
        if (name == "rainDurMin") {
            _config.rainDurMin = (uint8_t)constrain(val.toInt(), 1, PROJ_CLOCK_RAIN_DUR_MAX);
            continue;
        }
        if (name == "rainIntensity") {
            _config.rainIntensity = (uint8_t)constrain(val.toInt(), 1, 10);
            continue;
        }
        if (name == "rainSettleDur") {
            _config.rainSettleDur = (uint8_t)constrain(val.toInt(), 0, 10);
            continue;
        }
        if (name == "manualText") {
            String out;
            ns_device_projection_clock::normalizeDigitsText(val, out);
            _config.manualText = out;
            core_state.signal("proj_clock.text", BusValue::str(out));
            continue;
        }
    }

    stopSweepInternal();
    _view = PROJ_CLOCK_VIEW_NORMAL;
    _forceRedraw = true;
    _pendingApply = true;
    if (_pendingReinit == false) { _pendingSave = true; }

    request->send(200, "text/plain", "OK");
    SetTask(deferredApplyTask);
}

void CLASS_DEVICE_PROJ_CLOCK::handleTest(AsyncWebServerRequest *request) {
    DEBUGPROJCLOCK("%s\r\n", __FUNCTION__);

    if (_config.busMode == PROJ_CLOCK_BUS_OFF) {
        request->send(409, "text/plain", "OFF");
        return;
    }
    if (!request->hasArg("action")) {
        request->send(400, "text/plain", "No action");
        return;
    }

    String action = request->arg("action");
    if (action == "sweep_all") {
        startSweepAll();
    } else if (action == "sweep_digit") {
        int d = request->hasArg("digit") ? request->arg("digit").toInt() : 1;
        if (d < 1) { d = 1; }
        if (d > PROJ_CLOCK_DIGITS) { d = PROJ_CLOCK_DIGITS; }
        startSweepDigit((uint8_t)(d - 1));
    } else if (action == "stop") {
        stopSweep();
    } else if (action == "fx_minute") {
        triggerFxMinute();
    } else if (action == "fx_hour") {
        triggerFxHour();
    } else if (action == "rain") {
        triggerRain();
    } else {
        request->send(400, "text/plain", "Bad action");
        return;
    }

    request->send(200, "text/plain", "OK");
}

// ============================================================
// Конфиг
// ============================================================
void CLASS_DEVICE_PROJ_CLOCK::defaultConfig() {
    _config.busMode     = PROJ_CLOCK_BUS_AUTO;
    _config.mode        = PROJ_CLOCK_MODE_WORK;
    _config.dataPin     = 16;
    _config.brightness  = 25;
    _config.hourFormat  = PROJ_CLOCK_HOUR_24;
    _config.effect      = PROJ_CLOCK_EFFECT_MONO;
    _config.effectDir   = PROJ_CLOCK_FXDIR_LR;
    _config.digitsColor = 0x00FF00;
    _config.digitsColor2 = 0x0000FF;
    _config.animSpeed   = 45;
    _config.colorsCount = 4;
    _config.cycleMode   = PROJ_CLOCK_CYCLE_SEQUENTIAL;
    _config.palette[0]  = 0x00FF00;
    _config.palette[1]  = 0xFFFF00;
    _config.palette[2]  = 0x00FF00;
    _config.palette[3]  = 0x00FFFF;
    _config.palette[4]  = 0x0000FF;
    _config.palette[5]  = 0x8000FF;
    _config.palette[6]  = 0xFF8000;
    _config.palette[7]  = 0xFFFFFF;

    _config.minuteFx    = PROJ_CLOCK_TFX_SPIN;
    _config.minuteFxDur = 1;
    _config.minuteFxFreq = PROJ_CLOCK_TFX_FREQ_DEFAULT;
    _config.hourFx      = PROJ_CLOCK_TFX_SWEEP;
    _config.hourFxDur   = 3;

    _config.rainEnabled = 1;
    _config.rainDurMin  = 2;
    _config.rainIntensity = 6;
    _config.rainSettleDur = 1;

    _config.manualText  = "123456";
}

bool CLASS_DEVICE_PROJ_CLOCK::loadConfig() {
    DEBUGPROJCLOCK("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    if (core_json.jsonFileLoadDoc(CONFIG_FILE_PROJ_CLOCK, doc) == false) { return false; }

    _config.busMode    = (uint8_t)constrain(doc["busMode"].as<int>(), PROJ_CLOCK_BUS_OFF, PROJ_CLOCK_BUS_MACRO);
    _config.mode       = (uint8_t)constrain(doc["mode"].as<int>(), PROJ_CLOCK_MODE_WORK, PROJ_CLOCK_MODE_MANUAL);
    _config.dataPin    = (int16_t)constrain(doc["dataPin"].as<int>(), -1, 33);
    _config.brightness = (uint8_t)constrain(doc["brightness"].as<int>(), 0, 255);
    _config.hourFormat = (uint8_t)constrain(doc["hourFormat"].as<int>(), PROJ_CLOCK_HOUR_24, PROJ_CLOCK_HOUR_12);
    _config.effect     = (uint8_t)constrain(doc["effect"].as<int>(), 0, PROJ_CLOCK_EFFECT_COUNT - 1);
    _config.effectDir  = (uint8_t)constrain(doc["effectDir"].as<int>(), 0, PROJ_CLOCK_FXDIR_COUNT - 1);
    _config.digitsColor = ns_core_json::colorFromJson(doc["digitsColor"], _config.digitsColor);
    _config.digitsColor2 = ns_core_json::colorFromJson(doc["digitsColor2"], _config.digitsColor2);
    _config.animSpeed  = (uint8_t)constrain(doc["animSpeed"].as<int>(), 1, 50);
    _config.colorsCount = (uint8_t)constrain(doc["colorsCount"].as<int>(), 1, PROJ_CLOCK_MAX_COLORS);
    _config.cycleMode  = (uint8_t)constrain(doc["cycleMode"].as<int>(), PROJ_CLOCK_CYCLE_SEQUENTIAL, PROJ_CLOCK_CYCLE_RAINBOW);

    if (doc["palette"].is<JsonArray>()) {
        JsonArray arr = doc["palette"].as<JsonArray>();
        for (uint8_t i = 0; i < PROJ_CLOCK_MAX_COLORS; i++) {
            _config.palette[i] = ns_core_json::colorFromJson(arr[i], _config.palette[i]);
        }
    }

    if (doc["minuteFx"].is<int>()) {
        _config.minuteFx = (uint8_t)constrain(doc["minuteFx"].as<int>(), 0, PROJ_CLOCK_TFX_COUNT - 1);
    }
    if (doc["minuteFxDur"].is<int>()) {
        _config.minuteFxDur = (uint8_t)constrain(doc["minuteFxDur"].as<int>(), 1, PROJ_CLOCK_DUR_MAX);
    }
    if (doc["minuteFxFreq"].is<int>()) {
        int f = doc["minuteFxFreq"].as<int>();
        if (f != 1 && f != 3 && f != 5 && f != 10 && f != 15 && f != 20 && f != 40) {
            f = PROJ_CLOCK_TFX_FREQ_DEFAULT;
        }
        _config.minuteFxFreq = (uint8_t)f;
    }
    if (doc["hourFx"].is<int>()) {
        _config.hourFx = (uint8_t)constrain(doc["hourFx"].as<int>(), 0, PROJ_CLOCK_TFX_COUNT - 1);
    }
    if (doc["hourFxDur"].is<int>()) {
        _config.hourFxDur = (uint8_t)constrain(doc["hourFxDur"].as<int>(), 1, PROJ_CLOCK_DUR_MAX);
    }

    if (doc["rainEnabled"].is<int>()) {
        _config.rainEnabled = (uint8_t)constrain(doc["rainEnabled"].as<int>(), 0, 1);
    }
    if (doc["rainDurMin"].is<int>()) {
        _config.rainDurMin = (uint8_t)constrain(doc["rainDurMin"].as<int>(), 1, PROJ_CLOCK_RAIN_DUR_MAX);
    }
    if (doc["rainIntensity"].is<int>()) {
        _config.rainIntensity = (uint8_t)constrain(doc["rainIntensity"].as<int>(), 1, 10);
    }
    if (doc["rainSettleDur"].is<int>()) {
        _config.rainSettleDur = (uint8_t)constrain(doc["rainSettleDur"].as<int>(), 0, 10);
    }

    String txt = doc["manualText"].as<String>();
    ns_device_projection_clock::normalizeDigitsText(txt, _config.manualText);

    DEBUGPROJCLOCK("dataPin=%d mode=%u brightness=%u format=%u effect=%u\r\n",
                   _config.dataPin, _config.mode, _config.brightness, _config.hourFormat, _config.effect);
    return true;
}

bool CLASS_DEVICE_PROJ_CLOCK::saveConfig() {
    DEBUGPROJCLOCK("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    core_json.jsonFileLoadDoc(CONFIG_FILE_PROJ_CLOCK, doc);

    doc["busMode"]     = _config.busMode;
    doc["mode"]        = _config.mode;
    doc["dataPin"]     = _config.dataPin;
    doc["brightness"]  = _config.brightness;
    doc["hourFormat"]  = _config.hourFormat;
    doc["effect"]      = _config.effect;
    doc["effectDir"]   = _config.effectDir;
    doc["digitsColor"] = ns_core_json::colorToHex(_config.digitsColor);
    doc["digitsColor2"] = ns_core_json::colorToHex(_config.digitsColor2);
    doc["animSpeed"]   = _config.animSpeed;
    doc["colorsCount"] = _config.colorsCount;
    doc["cycleMode"]   = _config.cycleMode;

    JsonArray palArr = doc["palette"].to<JsonArray>();
    palArr.clear();
    for (uint8_t i = 0; i < PROJ_CLOCK_MAX_COLORS; i++) {
        palArr.add(ns_core_json::colorToHex(_config.palette[i]));
    }

    doc["minuteFx"]      = _config.minuteFx;
    doc["minuteFxDur"]   = _config.minuteFxDur;
    doc["minuteFxFreq"]  = _config.minuteFxFreq;
    doc["hourFx"]        = _config.hourFx;
    doc["hourFxDur"]     = _config.hourFxDur;
    doc["rainEnabled"]   = _config.rainEnabled;
    doc["rainDurMin"]    = _config.rainDurMin;
    doc["rainIntensity"] = _config.rainIntensity;
    doc["rainSettleDur"] = _config.rainSettleDur;
    doc["manualText"]    = _config.manualText;

    return core_json.jsonFileSaveDoc(CONFIG_FILE_PROJ_CLOCK, doc);
}

bool CLASS_DEVICE_PROJ_CLOCK::saveModeConfig() {
    DEBUGPROJCLOCK("%s: busMode=%u\r\n", __FUNCTION__, _config.busMode);
    JsonDocument doc;
    if (core_json.jsonFileLoadDoc(CONFIG_FILE_PROJ_CLOCK, doc) == false) {
        return saveConfig();
    }
    doc["busMode"] = _config.busMode;
    return core_json.jsonFileSaveDoc(CONFIG_FILE_PROJ_CLOCK, doc);
}

// ============================================================
// Версионные методы
// ============================================================
String CLASS_DEVICE_PROJ_CLOCK::getVersionStr() {
    return String(DEVICE_PROJECTION_CLOCK_VERSION);
}

String CLASS_DEVICE_PROJ_CLOCK::getGeneratedTime() {
    return String(DEVICE_PROJECTION_CLOCK_GENERATED_TIME);
}

String CLASS_DEVICE_PROJ_CLOCK::getCommitDateStr() {
    return String(DEVICE_PROJECTION_CLOCK_COMMIT_DATE_STR);
}

void CLASS_DEVICE_PROJ_CLOCK::html_ver_get(AsyncWebServerRequest *request) {
    DEBUGPROJCLOCK("%s\r\n", __FUNCTION__);
    String values = "";
    values += "projclockversion|" + getVersionStr()    + "|div\n";
    values += "projclockgentime|" + getGeneratedTime() + "|div\n";
    values += "projclockgendate|" + getCommitDateStr() + "|div\n";
    request->send(200, "text/plain", values);
}

// ============================================================
// Отладочные действия
// ============================================================
void CLASS_DEVICE_PROJ_CLOCK::startSweepAll() {
    if (_config.busMode == PROJ_CLOCK_BUS_OFF) { return; }
    _rainForced = false;
    _view = PROJ_CLOCK_VIEW_NORMAL;
    _sweepActive = true;
    _sweepAll = true;
    _sweepBlock = 0;
    _sweepIdx = 0;
    _sweepLastMs = millis() - PROJ_CLOCK_SWEEP_STEP_MS;
    _forceRedraw = false;
    DEBUGPROJCLOCK("sweep all start\r\n");
}

void CLASS_DEVICE_PROJ_CLOCK::startSweepDigit(uint8_t block) {
    if (_config.busMode == PROJ_CLOCK_BUS_OFF) { return; }
    if (block >= PROJ_CLOCK_DIGITS) { block = 0; }
    _rainForced = false;
    _view = PROJ_CLOCK_VIEW_NORMAL;
    _sweepActive = true;
    _sweepAll = false;
    _sweepBlock = block;
    _sweepIdx = 0;
    _sweepLastMs = millis() - PROJ_CLOCK_SWEEP_STEP_MS;
    _forceRedraw = false;
    DEBUGPROJCLOCK("sweep digit %u start\r\n", block + 1);
}

void CLASS_DEVICE_PROJ_CLOCK::stopSweep() {
    stopSweepInternal();
}

void CLASS_DEVICE_PROJ_CLOCK::stopSweepInternal() {
    if (_sweepActive) {
        DEBUGPROJCLOCK("sweep stop\r\n");
    }
    _sweepActive = false;
    _sweepIdx = 0;
    _forceRedraw = true;
}

void CLASS_DEVICE_PROJ_CLOCK::triggerFxMinute() {
    if (_config.busMode == PROJ_CLOCK_BUS_OFF) { return; }
    _sweepActive = false;
    _rainForced = false;
    _view = PROJ_CLOCK_VIEW_NORMAL;
    _pendingTest = PROJ_CLOCK_TEST_FX_MINUTE;
}

void CLASS_DEVICE_PROJ_CLOCK::triggerFxHour() {
    if (_config.busMode == PROJ_CLOCK_BUS_OFF) { return; }
    _sweepActive = false;
    _rainForced = false;
    _view = PROJ_CLOCK_VIEW_NORMAL;
    _pendingTest = PROJ_CLOCK_TEST_FX_HOUR;
}

void CLASS_DEVICE_PROJ_CLOCK::triggerRain() {
    if (_config.busMode == PROJ_CLOCK_BUS_OFF) { return; }
    _sweepActive = false;
    _pendingTest = PROJ_CLOCK_TEST_RAIN;
}
