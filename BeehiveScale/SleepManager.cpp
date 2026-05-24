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
  // ═══ v5.0.39 — TIMER DEEP SLEEP (без DS3231 alarm) ═══
  //
  // КОМПРОМИСС: hardware power-cut через DS3231 SQW не работает — chip одной партии
  // (3 модуля HW-084) теряет state каждый power-off несмотря на VCC=4V always-on,
  // CR2032=3.2V, явный clear EOSC. Альтернативы (cap фильтр, замена ZS-042) — потом.
  //
  // Текущий режим: НЕ отключаем MOSFET. ESP в esp_deep_sleep с internal timer.
  // - MOSFET остаётся ON (HOLD через Alarm1 PerSecond не отключаем)
  // - DS3231 alarm не используется — wake через esp_sleep_enable_timer_wakeup
  // - Sleep current ~5-10 мА (ESP deep sleep + MT3608 Iq + AMS1117 Iq)
  // - Автономия 30-60 дней без солнца, бесконечно с solar
  // - Работает гарантированно — internal timer ESP не зависит от внешних компонентов
  //
  // Когда DS3231 module заменим на рабочий — вернуть hardware power-cut логику.

  // WiFi shutdown
  WiFi.disconnect(true, false);
  delay(100);
  esp_wifi_stop();
  delay(50);

  Serial.print(F("[Sleep] esp_deep_sleep_start for "));
  Serial.print((uint32_t)seconds);
  Serial.println(F(" sec (timer wake, MOSFET stays ON)"));
  Serial.flush();
  uart_wait_tx_idle_polling(UART_NUM_0);

  // Timer wakeup через internal ESP RTC oscillator (не зависит от DS3231)
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
