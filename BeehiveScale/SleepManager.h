#ifndef SLEEP_MANAGER_H
#define SLEEP_MANAGER_H

#include <Arduino.h>

#define SLEEP_MODE_CONTINUOUS
#if defined(SLEEP_MODE_CONTINUOUS) && defined(SLEEP_MODE_DEEP_SLEEP)
  #error "Cannot define both SLEEP_MODE_CONTINUOUS and SLEEP_MODE_DEEP_SLEEP"
#endif
#define SLEEP_WAKEUP_PIN     0

#if defined(ESP8266)
#define PERIPHERAL_POWER_PIN -1
#else
#define PERIPHERAL_POWER_PIN 26
#endif

struct SleepPersistData {
  uint32_t magic;
  float    lastWeight;
  float    lastTempC;
  uint32_t wakeupCount;
  bool     alertSent;
  float    lastAlertWeight;  // вес на момент последнего алерта (для повторных срабатываний)
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
