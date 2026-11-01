#ifndef _DEVICE_PROJECTION_CLOCK_COMMON_MODULE_h
#define _DEVICE_PROJECTION_CLOCK_COMMON_MODULE_h

#include <Arduino.h>
#include <stdint.h>

#include "common/TimeLib.h"
#include "proj_clock_types.h"

// Вспомогательные функции устройства «Проекционные часы»
// (чистые, без состояния). Namespace — по имени каталога компонента.
namespace ns_device_projection_clock {

// Карта «цифра -> индекс LED внутри блока» и обратно.
// Физический порядок: [4,3,2,1,5,6,0,9,8,7].
uint8_t  digitToLed(uint8_t digit);
uint8_t  ledToDigit(uint8_t ledIdx);

// Глобальный индекс LED для позиции разряда (0..5) и цифры (0..9).
uint16_t ledIndex(uint8_t pos, uint8_t digit);

// Нормализация ручного текста: только цифры, максимум 6; короткий текст
// дополняется нулями. Возвращает true, если получилось ровно 6 цифр.
bool normalizeDigitsText(const String& in, String& out);

// 6 цифр текущего времени (24h/12h).
void timeDigits(time_t t, uint8_t hourFormat, uint8_t out[PROJ_CLOCK_DIGITS]);
String timeHhMmSs(time_t t, uint8_t hourFormat);

// Нормированная позиция (0..1) вдоль выбранного направления.
float posOnAxis(int pos, uint8_t dir);

// Цветовые помощники (порт из device_electronica7_rgb).
void     seedRng();
uint32_t rand32();
uint32_t hsvToRgb(float h, float s, float v);
uint32_t lerpColor(uint32_t c1, uint32_t c2, float t);
void     shuffleOrder(uint8_t* arr, int n);

// Цвет разряда pos (0..5) под текущим эффектом.
uint32_t fxColor(const strProjClockConfig& cfg, int pos, float phaseDeg, const uint8_t* order);
bool     fxAnimated(uint8_t effect);

// Названия для отладки/страницы.
const char* effectName(uint8_t effect);
const char* tfxName(uint8_t tfx);
const char* hourFormatName(uint8_t fmt);

} // namespace ns_device_projection_clock

#endif // _DEVICE_PROJECTION_CLOCK_COMMON_MODULE_h
