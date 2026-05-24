#ifndef SLEEP_MANAGER_H
#define SLEEP_MANAGER_H

#include <Arduino.h>

#define SLEEP_MODE_CONTINUOUS
#if defined(SLEEP_MODE_CONTINUOUS) && defined(SLEEP_MODE_DEEP_SLEEP)
  #error "Cannot define both SLEEP_MODE_CONTINUOUS and SLEEP_MODE_DEEP_SLEEP"
#endif

// Wake-on-button pin (EXT0). На ESP8266 — D3 (GPIO0), на ESP32 — MAIN button GPIO27.
#if defined(ESP32)
  #define SLEEP_WAKEUP_PIN  27
#else
  #define SLEEP_WAKEUP_PIN   0
#endif

// PERIPHERAL_POWER_PIN — high-side P-MOSFET для отключения LCD/HX711/RTC во сне.
// Не используется в v5.0.30 — для power-cut используем AO3401 + GPIO25 HOLD (см. ниже).
#define PERIPHERAL_POWER_PIN -1

// v5.0.34: POWER_HOLD_PIN убран. DS3231 SQW open-drain напрямую к Gate AO3401 +
// 1MΩ pullup Gate-Source. ESP не управляет MOSFET — латч работает на уровне RTC.
// Цикл: alarm fires → SQW LOW → MOSFET ON → boot → ESP программирует next alarm +
// clearAlarm → SQW HIGH → 1MΩ тянет Gate к Source → MOSFET OFF → power off.
// GPIO25 теперь свободен для других нужд.

struct SleepPersistData {
  uint32_t magic;
  float    lastWeight;
  float    lastTempC;
  uint32_t wakeupCount;
  bool     alertSent;
  float    lastAlertWeight;     // вес на момент последнего алерта (для повторных срабатываний)
  float    lastReportWeight;    // вес на момент прошлого TG-отчёта (для дельты "за сутки")
  bool     hasLastReport;       // true после первого отправленного отчёта (иначе дельта = 0)
};
// RTC user memory: max 512 bytes (128 uint32_t words). Assert size fits.
static_assert(sizeof(SleepPersistData) <= 64, "SleepPersistData too large for RTC user memory");
static_assert(sizeof(SleepPersistData) % 4 == 0, "SleepPersistData must be 4-byte aligned for RTC memory");


void sleep_init();
void sleep_load_persistent(SleepPersistData &data);
void sleep_save_persistent(const SleepPersistData &data);
void sleep_enter(uint64_t seconds);
bool sleep_was_wakeup_by_timer();
bool sleep_was_wakeup_by_button();

#endif
