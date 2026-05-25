#include "SleepManager.h"
#include "Temperature.h"
#include "RTC_Module.h"

#if defined(ESP32)
#include <esp_sleep.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include "driver/rtc_io.h"
#include "driver/uart.h"
#endif

#if defined(ESP32)
RTC_DATA_ATTR static SleepPersistData _persist;
#else
static SleepPersistData _persist;  // загружается из RTC RAM в sleep_load_persistent
#endif

void sleep_init() {
#if PERIPHERAL_POWER_PIN >= 0
  pinMode(PERIPHERAL_POWER_PIN, OUTPUT);
  digitalWrite(PERIPHERAL_POWER_PIN, HIGH);
#endif
#if defined(ESP32)
  // v5.0.27: освободить SLEEP_WAKEUP_PIN из RTC mux ПЕРЕД enable_ext0_wakeup.
  // Без этого state накапливается между wake → конфликт RTC/PHY clock.
  rtc_gpio_deinit((gpio_num_t)SLEEP_WAKEUP_PIN);

  esp_sleep_enable_ext0_wakeup((gpio_num_t)SLEEP_WAKEUP_PIN, LOW);

  // v5.0.27: явно держим RTC domains ON во время deep sleep.
  // По умолчанию в IDF 5.x они OFF для экономии — но это ломает ext0 wakeup latch
  // и PHY calibration cache в RTC FAST RAM (Issue #9913).
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_ON);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_ON);
#endif
  Serial.println(F("[Sleep] Init OK"));
}

void sleep_load_persistent(SleepPersistData &data) {
#if defined(ESP8266)
  if (!ESP.rtcUserMemoryRead(0, (uint32_t*)&_persist, sizeof(_persist))) {
    Serial.println(F("[Sleep] RTC memory read failed"));
    data.magic = 0xDEADBEEF;
    data.lastWeight = 0.0f;
    data.lastTempC = TEMP_ERROR_VALUE;
    data.wakeupCount = 0;
    data.alertSent = false;
    data.lastAlertWeight = 0.0f;
    data.lastReportWeight = 0.0f;
    data.hasLastReport = false;
    _persist = data;
    return;
  }
#endif
  if (_persist.magic == 0xDEADBEEF) {
    data = _persist;
    Serial.print(F("[Sleep] Wakeup #"));
    Serial.println(_persist.wakeupCount);
  } else {
    data.magic = 0xDEADBEEF;
    data.lastWeight = 0.0f;
    data.lastTempC = TEMP_ERROR_VALUE;
    data.wakeupCount = 0;
    data.alertSent = false;
    data.lastAlertWeight = 0.0f;
    data.lastReportWeight = 0.0f;
    data.hasLastReport = false;
    _persist = data;
    Serial.println(F("[Sleep] First boot."));
  }
}

void sleep_save_persistent(const SleepPersistData &data) {
  _persist = data;
  _persist.magic = 0xDEADBEEF;
#if defined(ESP8266)
  ESP.rtcUserMemoryWrite(0, (uint32_t*)&_persist, sizeof(_persist));
#endif
}

void sleep_enter(uint64_t seconds) {
  Serial.print(F("[PowerCut] sleep for "));
  Serial.print(seconds);
  Serial.println(F(" sec — programming DS3231 alarm..."));
  Serial.flush();

#if defined(ESP32)
  // ═══ v5.0.47 — ПОЛНЫЙ POWER-CUT через DS3231 Alarm2 ═══
  //
  // Архитектура:
  //   - HOLD: Alarm1 в PerSecond mode (set в setup() через rtc_enable_persecond_hold)
  //           → A1F=1 каждую секунду → SQW LOW → MOSFET ON
  //   - WAKE: Alarm2 на N сек вперёд (programm здесь через rtc_set_alarm_in_seconds)
  //           → A2F=1 при match → SQW LOW → MOSFET ON → ESP boot
  //
  // sleep_enter sequence:
  //   1. WiFi shutdown
  //   2. rtc_set_alarm_in_seconds(N) — programm Alarm2 + disable Alarm1 PerSecond
  //      (v5.0.46 fix: переключить Alarm1 на A1_Hour mode перед disable чтобы
  //       PerSecond не re-set A1F каждую секунду)
  //   3. После disable A1: SQW HIGH (A2F=0, A1F не set) → MOSFET OFF → power off ~100мс
  //   4. Fallback: esp_deep_sleep_start если hardware не сработало
  //
  // Sleep current: ~2 мА (MOSFET закрыт, потребляет только DS3231 module + утечка)
  // Wake: через DS3231 Alarm2 (POWERON_RESET) либо через timer fallback

  // Шаг 1: WiFi shutdown
  WiFi.disconnect(true, false);
  delay(100);
  esp_wifi_stop();
  delay(50);

  // Шаг 2: programm DS3231 Alarm2 wake (HOLD активен через Alarm1 PerSecond)
  if (!rtc_set_alarm_in_seconds((uint32_t)seconds)) {
    Serial.println(F("[PowerCut] WARN: DS3231 wake-alarm set failed"));
  }
  rtc_enable_alarm_interrupt();  // INTCN=1 (на всякий случай)

  Serial.println(F("[PowerCut] Alarm2 armed. Power off imminent..."));
  Serial.flush();
  uart_wait_tx_idle_polling(UART_NUM_0);

  // Шаг 3: ждать пока MOSFET физически отключит питание.
  // disableAlarm(1) уже выполнен внутри rtc_set_alarm_in_seconds → SQW HIGH → MOSFET OFF.
  // ESP теряет питание через ~100мс (cap discharge MT3608).
  delay(1000);

  // FALLBACK: если hardware power-cut не сработал (SQW обрыв, DS3231 module бракован) —
  // уходим в esp_deep_sleep с timer wake. Sleep current будет ~14 мА вместо 2 мА.
  Serial.println(F("[PowerCut] WARN: hardware power-cut DID NOT WORK — fallback to deep sleep"));
  Serial.flush();
  if (seconds > 0) {
    esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  }
  esp_deep_sleep_start();

#elif defined(ESP8266)
  // ESP8266 max deep sleep ~71 мин (32-битный мкс-таймер чипа). Передача большего значения
  // даёт неопределённое поведение — ESP может вообще не проснуться. Cap на 70 мин с запасом.
  // При длинном расписании (>70 мин до цели) — ESP проснётся, проверит RTC и снова уснёт.
  const uint32_t MAX_SLEEP_SEC = 4200UL;  // 70 минут
  if (seconds > 0) {
    if (seconds > MAX_SLEEP_SEC) {
      Serial.print(F("[Sleep] Capped from "));
      Serial.print(seconds);
      Serial.print(F(" to "));
      Serial.print(MAX_SLEEP_SEC);
      Serial.println(F(" sec (ESP8266 hw limit)"));
      Serial.flush();
      seconds = MAX_SLEEP_SEC;
    }
    ESP.deepSleep((uint64_t)seconds * 1000000ULL);
  } else {
    ESP.deepSleep(0);  // Бесконечный сон, пробуждение по RST
  }
#endif
}

bool sleep_was_wakeup_by_timer() {
#if defined(ESP32)
  return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
#else
  return false;
#endif
}

bool sleep_was_wakeup_by_button() {
#if defined(ESP32)
  return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0;
#else
  return false;
#endif
}
