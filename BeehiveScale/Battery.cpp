#include "Battery.h"
#include "Memory.h"

static float _batSmoothed   = 0.0f;
static bool  _batInitialized = false;
static float _batRatio      = BAT_DIVIDER_RATIO_DEFAULT;  // v5.0.19: динамический, из EEPROM

// Напряжение на пине ADC (до умножения на ratio). Для калибровки через сайт.
float bat_read_raw_adc_voltage() {
  int raw = analogRead(BAT_PIN);
  return raw * 3.3f / BAT_ADC_MAX;
}

static float bat_read_raw() {
  return bat_read_raw_adc_voltage() * _batRatio;
}

void bat_init() {
#if defined(ESP32)
  analogReadResolution(12);
  // ADC_11db — универсальная Arduino-обёртка, работает во всех версиях ESP32 Core.
  // (В Core 3.0+ ESP-IDF переименовал ADC_ATTEN_DB_11 → ADC_ATTEN_DB_12, но Arduino-enum стабилен.)
  analogSetAttenuation(ADC_11db);
#endif
  // v5.0.19: загружаем калибровочный коэф. делителя из EEPROM (если есть)
  float saved = 0.0f;
  if (load_bat_calib(saved)) {
    _batRatio = saved;
  }
  // Усреднение по 10 выборкам при старте для стабильности
  float sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += bat_read_raw();
    delay(10);
  }
  _batSmoothed = sum / 10.0f;
  _batInitialized = true;
}

float bat_voltage() {
  if (!_batInitialized) return 0.0f;

  float raw = bat_read_raw();
  _batSmoothed = BAT_EMA_ALPHA * raw + (1.0f - BAT_EMA_ALPHA) * _batSmoothed;
  return _batSmoothed;
}

int bat_percent() {
  if (!_batInitialized) return 0;
  float v = _batSmoothed;
  // Кусочно-линейная аппроксимация LiPo кривой разряда
  float pct;
  if (v >= 4.10f)      pct = 95 + (v - 4.10f) / (4.20f - 4.10f) * 5.0f;   // 4.10-4.20 → 95-100%
  else if (v >= 3.90f) pct = 75 + (v - 3.90f) / (4.10f - 3.90f) * 20.0f;  // 3.90-4.10 → 75-95%
  else if (v >= 3.75f) pct = 45 + (v - 3.75f) / (3.90f - 3.75f) * 30.0f;  // 3.75-3.90 → 45-75%
  else if (v >= 3.60f) pct = 15 + (v - 3.60f) / (3.75f - 3.60f) * 30.0f;  // 3.60-3.75 → 15-45%
  else if (v >= 3.40f) pct =  5 + (v - 3.40f) / (3.60f - 3.40f) * 10.0f;  // 3.40-3.60 → 5-15%
  else if (v >= BAT_VMIN) pct = (v - BAT_VMIN) / (3.40f - BAT_VMIN) * 5.0f; // 3.00-3.40 → 0-5%
  else pct = 0.0f;
  return constrain((int)pct, 0, 100);
}

float bat_get_ratio() {
  return _batRatio;
}

void bat_set_ratio(float r) {
  if (isnan(r) || r < 1.5f || r > 3.0f) return;
  _batRatio = r;
  save_bat_calib(r);
  // Сбрасываем EMA-сглаживание, чтобы новые показания сразу применились
  _batSmoothed = bat_read_raw();
}
