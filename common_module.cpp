#include "common_module.h"

#include <math.h>
#include <stdio.h>

// ============================================================
// Вспомогательные функции устройства «Проекционные часы»
// ============================================================

namespace ns_device_projection_clock {

// Физический порядок LED внутри одного разряда.
static const uint8_t kDigitSeq[PROJ_CLOCK_LEDS_PER_DIGIT] = { 4, 3, 2, 1, 5, 6, 0, 9, 8, 7 };

uint8_t digitToLed(uint8_t digit) {
    digit %= PROJ_CLOCK_LEDS_PER_DIGIT;
    for (uint8_t i = 0; i < PROJ_CLOCK_LEDS_PER_DIGIT; i++) {
        if (kDigitSeq[i] == digit) { return i; }
    }
    return 0;
}

uint8_t ledToDigit(uint8_t ledIdx) {
    return kDigitSeq[ledIdx % PROJ_CLOCK_LEDS_PER_DIGIT];
}

uint16_t ledIndex(uint8_t pos, uint8_t digit) {
    if (pos >= PROJ_CLOCK_DIGITS) { return 0; }
    return (uint16_t)pos * PROJ_CLOCK_LEDS_PER_DIGIT + digitToLed(digit);
}

bool normalizeDigitsText(const String& in, String& out) {
    String tmp;
    for (uint16_t i = 0; i < in.length() && tmp.length() < PROJ_CLOCK_DIGITS; i++) {
        char c = in.charAt(i);
        if (c >= '0' && c <= '9') { tmp += c; }
    }
    while (tmp.length() < PROJ_CLOCK_DIGITS) { tmp += '0'; }
    out = tmp;
    return out.length() == PROJ_CLOCK_DIGITS;
}

void timeDigits(time_t t, uint8_t hourFormat, uint8_t out[PROJ_CLOCK_DIGITS]) {
    int h = hour(t);
    int m = minute(t);
    int s = second(t);
    if (hourFormat == PROJ_CLOCK_HOUR_12) {
        h %= 12;
        if (h == 0) { h = 12; }
    }
    out[0] = (uint8_t)(h / 10);
    out[1] = (uint8_t)(h % 10);
    out[2] = (uint8_t)(m / 10);
    out[3] = (uint8_t)(m % 10);
    out[4] = (uint8_t)(s / 10);
    out[5] = (uint8_t)(s % 10);
}

String timeHhMmSs(time_t t, uint8_t hourFormat) {
    int h = hour(t);
    int m = minute(t);
    int s = second(t);
    if (hourFormat == PROJ_CLOCK_HOUR_12) {
        h %= 12;
        if (h == 0) { h = 12; }
    }
    char buf[10];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
    return String(buf);
}

float posOnAxis(int pos, uint8_t dir) {
    if (pos < 0) { pos = 0; }
    if (pos >= PROJ_CLOCK_DIGITS) { pos = PROJ_CLOCK_DIGITS - 1; }
    float t = (float)pos / (float)(PROJ_CLOCK_DIGITS - 1);
    switch (dir) {
        case PROJ_CLOCK_FXDIR_RL:     return 1.0f - t;
        case PROJ_CLOCK_FXDIR_CENTER: {
            float d = fabsf((float)pos - (float)(PROJ_CLOCK_DIGITS - 1) * 0.5f);
            return d / ((float)(PROJ_CLOCK_DIGITS - 1) * 0.5f);
        }
        case PROJ_CLOCK_FXDIR_EDGES: {
            float d = fabsf((float)pos - (float)(PROJ_CLOCK_DIGITS - 1) * 0.5f);
            return 1.0f - d / ((float)(PROJ_CLOCK_DIGITS - 1) * 0.5f);
        }
        case PROJ_CLOCK_FXDIR_LR:
        default:
            return t;
    }
}

// ============================================================
// Цветовые помощники
// ============================================================

static uint32_t s_rng = 0x9E3779B9u;

void seedRng() {
    s_rng = micros() ^ 0x9E3779B9u;
    if (s_rng == 0) { s_rng = 0x12345678u; }
}

uint32_t rand32() {
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

uint32_t hsvToRgb(float h, float s, float v) {
    while (h < 0.0f) { h += 360.0f; }
    while (h >= 360.0f) { h -= 360.0f; }
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (h < 60.0f)       { r = c; g = x; }
    else if (h < 120.0f) { r = x; g = c; }
    else if (h < 180.0f) { g = c; b = x; }
    else if (h < 240.0f) { g = x; b = c; }
    else if (h < 300.0f) { r = x; b = c; }
    else                 { r = c; b = x; }
    uint8_t r8 = (uint8_t)((r + m) * 255.0f);
    uint8_t g8 = (uint8_t)((g + m) * 255.0f);
    uint8_t b8 = (uint8_t)((b + m) * 255.0f);
    return ((uint32_t)r8 << 16) | ((uint32_t)g8 << 8) | b8;
}

uint32_t lerpColor(uint32_t c1, uint32_t c2, float t) {
    if (t <= 0.0f) { return c1; }
    if (t >= 1.0f) { return c2; }
    uint8_t r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    uint8_t r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
    uint8_t r = (uint8_t)(r1 + (r2 - r1) * t);
    uint8_t g = (uint8_t)(g1 + (g2 - g1) * t);
    uint8_t b = (uint8_t)(b1 + (b2 - b1) * t);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

void shuffleOrder(uint8_t* arr, int n) {
    for (int i = n - 1; i > 0; i--) {
        int j = (int)(rand32() % (uint32_t)(i + 1));
        uint8_t t = arr[i];
        arr[i] = arr[j];
        arr[j] = t;
    }
}

// ============================================================
// Цвет разряда под текущим эффектом
// ============================================================

uint32_t fxColor(const strProjClockConfig& cfg, int pos, float phaseDeg, const uint8_t* order) {
    switch (cfg.effect) {
        case PROJ_CLOCK_EFFECT_RAINBOW: {
            float t = posOnAxis(pos, cfg.effectDir);
            float hue = fmodf(phaseDeg + t * 300.0f, 360.0f);
            return hsvToRgb(hue, 1.0f, 1.0f);
        }
        case PROJ_CLOCK_EFFECT_GRAD_STATIC: {
            float t = posOnAxis(pos, cfg.effectDir);
            return lerpColor(cfg.digitsColor, cfg.digitsColor2, t);
        }
        case PROJ_CLOCK_EFFECT_GRAD_DYNAMIC: {
            float t = posOnAxis(pos, cfg.effectDir);
            float ph = fmodf(phaseDeg, 360.0f) / 360.0f;
            float w = 0.5f - 0.5f * cosf(6.2831853f * (t - ph));
            return lerpColor(cfg.digitsColor, cfg.digitsColor2, w);
        }
        case PROJ_CLOCK_EFFECT_COLORCYCLE: {
            if (cfg.cycleMode == PROJ_CLOCK_CYCLE_RAINBOW) {
                return hsvToRgb(phaseDeg, 1.0f, 1.0f);
            }
            uint8_t n = cfg.colorsCount;
            if (n < 1) { n = 1; }
            if (n > PROJ_CLOCK_MAX_COLORS) { n = PROJ_CLOCK_MAX_COLORS; }
            float segLen = 360.0f / (float)n;
            int seg = (int)(phaseDeg / segLen);
            if (seg >= n) { seg = n - 1; }
            float frac = phaseDeg - (float)seg * segLen;
            if (frac > segLen) { frac = segLen; }
            float t = frac / segLen;
            t = 0.5f - 0.5f * cosf(3.14159265f * t);
            int iFrom, iTo;
            if (cfg.cycleMode == PROJ_CLOCK_CYCLE_RANDOM && order != NULL) {
                iFrom = order[seg];
                iTo = order[(seg + 1) % n];
            } else {
                iFrom = seg;
                iTo = (seg + 1) % n;
            }
            return lerpColor(cfg.palette[iFrom], cfg.palette[iTo], t);
        }
        case PROJ_CLOCK_EFFECT_MONO:
        default:
            return cfg.digitsColor;
    }
}

bool fxAnimated(uint8_t effect) {
    return (effect == PROJ_CLOCK_EFFECT_RAINBOW ||
            effect == PROJ_CLOCK_EFFECT_GRAD_DYNAMIC ||
            effect == PROJ_CLOCK_EFFECT_COLORCYCLE);
}

const char* effectName(uint8_t effect) {
    static const char* const names[PROJ_CLOCK_EFFECT_COUNT] = {
        "mono", "rainbow", "grad_static", "grad_dynamic", "colorcycle"
    };
    return (effect < PROJ_CLOCK_EFFECT_COUNT) ? names[effect] : "?";
}

const char* tfxName(uint8_t tfx) {
    static const char* const names[PROJ_CLOCK_TFX_COUNT] = {
        "off", "fade_all", "fade_seq", "wipe_lr", "wipe_rl", "dissolve",
        "flash", "spin", "slot", "sweep", "crossfade"
    };
    return (tfx < PROJ_CLOCK_TFX_COUNT) ? names[tfx] : "?";
}

const char* hourFormatName(uint8_t fmt) {
    return (fmt == PROJ_CLOCK_HOUR_12) ? "12h" : "24h";
}

} // namespace ns_device_projection_clock
