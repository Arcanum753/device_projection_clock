#ifndef _PROJ_CLOCK_STRIP_h
#define _PROJ_CLOCK_STRIP_h

// ============================================================
// proj_clock_strip.h — драйвер адресной RGB-ленты 60 LED.
// 6 разрядов по 10 LED; в каждом разряде одновременно может
// гореть не более одного светодиода (обеспечивается рендером).
//
// NeoPixelBus. По умолчанию I2S+DMA (как в device_electronica7_rgb);
// -D PROJ_CLOCK_USE_RMT переключает на RMT (ESP32-C3/S3 и др.).
// ============================================================

#include <Arduino.h>
#include <NeoPixelBus.h>

#include "proj_clock_types.h"

#if defined(PROJ_CLOCK_USE_RMT)
typedef NeoPixelBus<NeoGrbFeature, NeoEsp32Rmt0Ws2812xMethod> ProjClockStripType;
#else
typedef NeoPixelBus<NeoGrbFeature, NeoEsp32I2s0Ws2812xMethod> ProjClockStripType;
#endif

class ProjClockStrip {
public:
    ProjClockStrip();
    void init(int16_t dataPin);
    void destroy();
    bool ready() const;
    void setLed(uint16_t idx, uint32_t rgb, uint8_t brightness);
    void clear();
    void show();

private:
    ProjClockStripType* _strip;
};

#endif // _PROJ_CLOCK_STRIP_h
