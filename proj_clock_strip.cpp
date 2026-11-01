#include "proj_clock_strip.h"

// ============================================================
// proj_clock_strip.cpp — реализация драйвера ленты.
// ============================================================

ProjClockStrip::ProjClockStrip() : _strip(NULL) {
}

void ProjClockStrip::init(int16_t dataPin) {
    destroy();
    if (dataPin < 0) { return; }
    _strip = new ProjClockStripType(PROJ_CLOCK_LEDS, (uint8_t)dataPin);
    if (_strip == NULL) { return; }
    _strip->Begin();
    _strip->ClearTo(RgbColor(0, 0, 0));
    _strip->Show();
}

void ProjClockStrip::destroy() {
    if (_strip != NULL) {
        delete _strip;
        _strip = NULL;
    }
}

bool ProjClockStrip::ready() const {
    return (_strip != NULL);
}

void ProjClockStrip::setLed(uint16_t idx, uint32_t rgb, uint8_t brightness) {
    if (_strip == NULL) { return; }
    if (idx >= PROJ_CLOCK_LEDS) { return; }
    float b = brightness / 255.0f;
    uint16_t r  = (uint16_t)(((rgb >> 16) & 0xFF) * b);
    uint16_t g  = (uint16_t)(((rgb >> 8)  & 0xFF) * b);
    uint16_t bl = (uint16_t)((rgb & 0xFF) * b);
    _strip->SetPixelColor(idx, RgbColor(r, g, bl));
}

void ProjClockStrip::clear() {
    if (_strip == NULL) { return; }
    _strip->ClearTo(RgbColor(0, 0, 0));
}

void ProjClockStrip::show() {
    if (_strip == NULL) { return; }
    _strip->Show();
}
