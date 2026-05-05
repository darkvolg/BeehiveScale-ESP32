#ifndef TEMPERATURE_H
#define TEMPERATURE_H

#include <Arduino.h>

#define TEMP_SENSOR_DS18B20
#if defined(ESP8266)
#define TEMP_PIN        3   // GPIO3 (D9/RX) — D7/GPIO13 занят SPI MOSI для SD-карты
#else
#define TEMP_PIN        4
#endif

#define TEMP_READ_INTERVAL_MS  10000UL
#define TEMP_ERROR_VALUE       -99.0f

struct TempData {
  float temperature = TEMP_ERROR_VALUE;
  float humidity    = TEMP_ERROR_VALUE;
  bool  valid       = false;
};

bool     temp_init();
TempData temp_read();
bool     temp_available();  // true если датчик найден при init

// Force-read с блокировкой до завершения конверсии (~750мс на 12-bit).
// Используется перед отправкой Telegram-отчёта, чтобы гарантировать актуальное
// значение даже если регулярное process_temperature() ещё не успел сделать
// второе чтение после wake-up (первое всегда пропускается из-за power-on 85°C).
TempData temp_force_read();

#endif
