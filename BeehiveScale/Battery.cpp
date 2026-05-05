#include "Battery.h"

static float _batSmoothed = 0.0f;
static bool  _batInitialized = false;

static float bat_read_raw() {
  int raw = analogRead(BAT_PIN);
  float vAdc = raw * 3.3f / BAT_ADC_MAX;
  return vAdc * BAT_DIVIDER_RATIO;
}

void bat_init() {
#if defined(ESP32)
  analogReadResolution(12);
  #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    analogSetAttenuation(ADC_ATTEN_DB_11);
  #else
    analogSetAttenuation(ADC_11db);
  #endif
#endif
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
