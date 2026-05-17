/*
 * BeehiveScale - Весы пчеловода (ESP8266)
 * Версия определена в Version.h (FW_VERSION).
 */

#include "Version.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <HX711.h>
#include <EEPROM.h>
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#elif defined(ESP32)
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <esp_task_wdt.h>
#endif

#include "Display.h"
#include "Scale.h"
#include "Button.h"
#include "Memory.h"
#include "RTC_Module.h"
#include "Temperature.h"
#include "Connectivity.h"
#include "SleepManager.h"
#include "WebServerModule.h"
#include "Battery.h"
#include "Alerts.h"      // v5.0.20: Telegram-алерты по батарее/температуре/RTC
#include "Logger.h"

// Pin mapping — ESP8266 vs ESP32. ESP32 — основная плата с 2026-05-05.
#if defined(ESP32)
  // ESP32-WROOM-32 DevKit V1 (DOIT, 38 пинов)
  // GPIO 6-11 — внутренняя SPI flash (ЗАПРЕЩЕНО).
  // GPIO 0, 2, 12, 15 — boot-strap (осторожно).
  // GPIO 34-39 — input only.
  #define DT_PIN          16   // HX711 DOUT
  #define SCK_PIN         17   // HX711 SCK
  #define BUTTON_PIN      27   // MAIN button (RTC GPIO — можно wake)
  #define MENU_BTN_PIN    26   // MENU button (RTC GPIO)
  // SDA=21, SCL=22 (Wire.begin() default), DS3231 SQW=33, DS18B20=4, Battery ADC=34
#elif defined(ESP8266)
  // HX711 пины. По умолчанию D5/D6 (GPIO14/12) — стандарт ESP8266, как у Bee_Lite v1.1.
  // Для отладочного отката к старой распиновке (D0/TX) — раскомментировать LEGACY_HX711_PINS.
  // 2026-05-04: переход на D5/D6 для Фазы 2 (DEEP_SLEEP) — D0 нужен под перемычку D0→RST.
  // #define LEGACY_HX711_PINS  // OFF: HX711 на D5/D6, D0 свободен под wake-up
  #ifdef LEGACY_HX711_PINS
    #define DT_PIN        16   // D0 — HX711 DOUT (старая распиновка, без pull-up)
    #define SCK_PIN        1   // TX — HX711 SCK (старая распиновка, заблокирует Serial debug)
  #else
    #define DT_PIN        14   // D5 — HX711 DOUT (новая стандартная распиновка)
    #define SCK_PIN       12   // D6 — HX711 SCK
  #endif
  #define BUTTON_PIN       0   // D3 — boot-strap! Не держать при включении
  #define MENU_BTN_PIN     2   // D4 — boot-strap! Не держать при включении
#endif
#define LCD_ADDR      0x27

#define WEIGHT_SAVE_MS    300000UL
#define WEIGHT_SAVE_THR     0.05f
// v5.0.0: тарирование = удержание MAIN ~3 сек (MEDIUM_PRESS), калибровка = ~6 сек (LONG_PRESS).
// Старая логика двойного нажатия с подтверждением убрана.
#define MENU_SCREENS           8
#define STABLE_BUF_SIZE        6
#define STABLE_THR             0.02f
#define STABLE_SAVE_MIN_MS 600000UL  // 10 мин — минимальный интервал между EEPROM-записями при стабилизации
#define SPIKE_FILTER_KG      5.0f    // Отбросить показание если скачок > 5 кг
#define ZERO_DEADBAND_KG     0.05f   // Auto-zero: всё что |вес| < 50г → показываем 0
#define SCALE_READ_INTERVAL_MS  600UL // Период чтения HX711 (v5.0.0: 600мс — быстрый отклик)
#define WDT_TIMEOUT_SEC       30
// AUTO_SLEEP — настраивается через сайт (get_autosleep_sec()), значение в секундах. 0 = не засыпать.

static inline void app_wdt_init() {
#if defined(ESP32)
  #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_task_wdt_config_t wdt_cfg = {
      .timeout_ms = WDT_TIMEOUT_SEC * 1000,
      .idle_core_mask = 0,
      .trigger_panic = true
    };
    esp_task_wdt_reconfigure(&wdt_cfg);
    esp_task_wdt_add(NULL);
  #else
    esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
    esp_task_wdt_add(NULL);
  #endif
#elif defined(ESP8266)
  ESP.wdtFeed();
#endif
}

static inline void app_wdt_reset() {
#if defined(ESP32)
  esp_task_wdt_reset();
#elif defined(ESP8266)
  ESP.wdtFeed();
  yield();
#endif
}

LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);
HX711             scale;

// Serial доступен всю работу: HX711 теперь на D5/D6, UART TX (GPIO1) свободен.

struct SystemState {
  float calibrationFactor = 2280.0f;
  long  offset            = 0;
  float lastSavedWeight   = 0.0f;
  float smoothedWeight    = 0.0f;
  bool  sensorReady       = false;
  bool  emaInitialized    = false;
  TempData tempData;
  float    rtcTempC       = NAN;
  TimeStamp currentTime;
  int  menuScreen         = 0;
  int  lastMenuScreen     = -1;
  bool needsRedraw        = true;
  bool wifiOk             = false;
  float prevWeight        = 0.0f;
  long  prevOffset        = 0;
  bool  hasPrevOffset     = false;  // true после первого тарирования в сессии
  float batVoltage        = 0.0f;
  int   batPercent        = 0;
  String datetimeStr      = "--";
  bool  weightStable      = false;
};

SystemState    sys;
ButtonState    btnMain;
ButtonState    btnMenu;
SleepPersistData persist;
bool webServerStarted = false;
unsigned long lastActivityTime = 0;  // Таймер бездействия для auto-sleep
unsigned long extendSleepUntilMs = 0; // Продление работы по запросу из web (POST /api/keepalive). 0 = неактивно.
bool diagRunRequested = false;       // Флаг запуска диагностики
bool diagDone = false;               // Диагностика завершена (сводка на экране)
bool tgReportPending = false;        // Запрос на отправку TG-отчёта при следующей возможности

void handle_buttons();
void process_weight();
void process_temperature();
void update_interface();
void display_screen_weight();
void display_screen_temp();
void display_screen_diff();
void display_screen_status();
void display_screen_datetime();
void display_screen_battery();
void display_error();
void display_screen_calib_menu();
void display_screen_diag();
void perform_taring();
void undo_tare();
void perform_calibration();
void adjust_calibration();
void show_splash_screen();
void start_webserver();
void check_auto_sleep();

void setup() {
  Serial.begin(115200);
  Serial.println(F("\n[" FW_FULLNAME "] boot"));

#if defined(ESP8266)
  ESP.wdtDisable();  // Отключаем программный WDT на время setup (ESP8266 ~3сек по умолчанию)
#endif
  app_wdt_init();
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(MENU_BTN_PIN, INPUT_PULLUP);
  button_attach_interrupt(BUTTON_PIN, btnMain);
  button_attach_interrupt(MENU_BTN_PIN, btnMenu);

#if defined(ESP8266)
  Wire.begin(4, 5);
  Wire.setClock(100000);             // 100 кГц — стабильнее для длинных проводов
  Wire.setClockStretchLimit(2500);   // DS3231 может тянуть до 2ms при конверсии температуры
#else
  Wire.begin();
  // v5.0.8: 400 кГц вместо стандартных 100 кГц. Сокращает время записи на LCD
  // в 4× → каждый I2C-апдейт меньше виден глазом. PCF8574 (LCD-адаптер) и
  // DS3231 поддерживают Fast Mode 400 kHz по даташиту.
  Wire.setClock(400000);
#endif
#if defined(ESP32)
  if (!EEPROM.begin(EEPROM_SIZE)) {
    Serial.println(F("[EEPROM] Init FAILED!"));
  }
#elif defined(ESP8266)
  EEPROM.begin(EEPROM_SIZE);
#endif

  sleep_init();
  sleep_load_persistent(persist);
#ifdef SLEEP_MODE_CONTINUOUS
  persist.wakeupCount++;
  sleep_save_persistent(persist);
#endif

  // v5.0.4: восстанавливаем lastReportWeight из EEPROM (RTC RAM теряется при reset/прошивке).
  // Без этого после каждой перезагрузки первый отчёт шёл без дельты "С прошлого замера".
  {
    float savedReportW = 0.0f;
    if (load_last_report(savedReportW)) {
      persist.lastReportWeight = savedReportW;
      persist.hasLastReport = true;
    }
  }

  // v5.0.5: восстанавливаем lastTempC из EEPROM (fallback для отчётов когда
  // DS18B20 не успел прочитаться к моменту отправки после wake-up).
  {
    float savedTempC = 0.0f;
    if (load_last_temp(savedTempC)) {
      persist.lastTempC = savedTempC;
    }
  }

  lcd_init(lcd);

#ifdef LEGACY_HX711_PINS
  // Старая распиновка использует TX (GPIO1) как HX711 SCK.
  // Закрываем Serial чтобы не было UART-мусора на линии clock.
  Serial.flush();
  Serial.end();
#endif
  scale_init(scale, DT_PIN, SCK_PIN);
  sys.sensorReady = check_sensor(scale);

  bool rtcOk = rtc_init();

  if (!temp_init()) {
    Serial.println(F("[Temp] No sensor, readings disabled"));
  }

  load_calibration_data(sys.calibrationFactor, sys.offset, sys.lastSavedWeight);
  sys.prevOffset = load_prev_offset();
  web_settings_init();
  ext_settings_init();
  sched_settings_init();
  tg_report_settings_init();
  tg_settings_init();
  credentials_init();
  // prevWeight при загрузке из EEPROM addr 30 (эталон пользователя).
  // Fallback на lastSavedWeight если EEPROM addr 30 ещё не записан.
  sys.prevWeight = load_prev_weight(sys.lastSavedWeight);

  bat_init();
  alerts_init();   // v5.0.20: загрузить настройки алертов из EEPROM

  if (sys.sensorReady) {
    scale.set_scale(sys.calibrationFactor);
    scale.set_offset(sys.offset);
  }

  yield();
  // v5.0.21: показываем splash и инициализируем датчики ПЕРЕД WiFi,
  // даём 8 сек для стабилизации питания (Boost-преобразователь успевает
  // зарядить капы, банка восстанавливается после пика boot). Только потом
  // включаем WiFi (его пик 300-500мА — главный убийца слабого Boost'а
  // типа MT3608, который защёлкивается по OCP при перегрузке).
  show_splash_screen();
  yield();
  // Показать "Init..." на LCD пока ждём стабилизации
  lcd_clear_buf(lcd);
  lcd_set_cursor(lcd, 0, 0); lcd_print_padded(lcd, "Vesy Pchelovod  ");
  lcd_set_cursor(lcd, 0, 1); lcd_print_padded(lcd, "WiFi cherez 8s..");
  // Делитель ожидания на короткие задержки чтобы watchdog не сработал
  for (int i = 0; i < 80; i++) { delay(100); yield(); app_wdt_reset(); }
  // Теперь WiFi (пик потребления отдельно, после стабилизации питания)
  sys.wifiOk = wifi_init();
  yield();
#if defined(ESP32)
  // v5.0.21: понижаем TX power с 19.5dBm до 11dBm — снижает пик WiFi
  // с ~500мА до ~200мА. Дальность падает ~30% но в улье хватает.
  WiFi.setTxPower(WIFI_POWER_11dBm);
  // v5.0.23: setSleep(true) УБРАН — modem-sleep блокировал webserver
  // запросы (страница открывалась но /api/data не возвращался). На
  // active-фазе экономия минимальная, лучше стабильность.
  WiFi.setSleep(false);
#endif
  yield();
  if (sys.wifiOk) {
    Serial.println(F("[WiFi] Connected"));
    Serial.print(F("[WiFi] IP: "));
    Serial.println(WiFi.status() == WL_CONNECTED ? WiFi.localIP() : WiFi.softAPIP());
    // v5.0.22: NTP sync на boot ОТКЛЮЧЕНА — вызывает Guru Meditation
    // (InstrFetchProhibited) на ESP32 Arduino Core 3.0.7 при первом
    // вызове configTime/getLocalTime после WiFi connect. DS3231 даёт
    // точное время (±2 мин/год), NTP не критичен. Ручная синхронизация
    // доступна через сайт (Настройки → Sync NTP / /api/ntp).
    // TODO: исправить на esp_sntp_init() или дождаться фикса в Core.
    // if (get_wifi_mode() == 1) {
    //   ntp_sync_time();
    // }
    start_webserver();
    // ArduinoOTA — обновление прошивки по воздуху. Пароль хранится в EEPROM,
    // дефолт "ota_beehive" при пустом блоке credentials (UI показывает warning).
    ArduinoOTA.setHostname("beehivescale");
    {
      char otaPassBuf[32];
      get_ota_pass(otaPassBuf, sizeof(otaPassBuf));
      ArduinoOTA.setPassword(otaPassBuf);
    }
    ArduinoOTA.onStart([]() { lastActivityTime = millis(); });
    ArduinoOTA.onProgress([](unsigned int, unsigned int) { lastActivityTime = millis(); });
    ArduinoOTA.begin();
  } else {
    Serial.println(F("[WiFi] Initialization failed!"));
  }

  log_init();
  lastActivityTime = millis();
#if defined(ESP8266)
  ESP.wdtEnable(8000);  // Включаем программный WDT обратно: 8 сек
#endif

  // Сбросить ISR-флаги кнопок: при boot D3 (GPIO0) и D4 (GPIO2) — boot-strap пины,
  // на них происходят переходные процессы → FALLING-прерывания ловят ложные нажатия.
  // Чистим флаги перед стартом loop, чтобы не было автотарирования при включении.
  noInterrupts();
  btnMain.irqFell = false;
  btnMain.irqTime = 0;
  btnMenu.irqFell = false;
  btnMenu.irqTime = 0;
  interrupts();

  Serial.println(F("[Setup] Done"));
}

void start_webserver() {
  if (webServerStarted) return;

  WebData wd;
  wd.weight          = &sys.smoothedWeight;
  wd.lastSavedWeight = &sys.lastSavedWeight;
  wd.tempC           = &sys.tempData.temperature;
  // humidity поле убрано (пункт 22) — нет физического датчика.
  wd.rtcTempC        = &sys.rtcTempC;
  wd.calibFactor     = &sys.calibrationFactor;
  wd.offset          = &sys.offset;
  wd.sensorReady     = &sys.sensorReady;
  wd.wifiOk          = &sys.wifiOk;
  wd.datetime        = &sys.datetimeStr;
  wd.wakeupCount     = &persist.wakeupCount;
  wd.batVoltage      = &sys.batVoltage;
  wd.batPercent      = &sys.batPercent;
  wd.prevWeight      = &sys.prevWeight;
  wd.lastReportWeight = &persist.lastReportWeight;
  wd.hasLastReport    = &persist.hasLastReport;

  WebActions wa;
  wa.doTare = perform_taring;
  wa.doSave = []() {
    sys.prevWeight = sys.smoothedWeight;
    save_weight(sys.lastSavedWeight, sys.smoothedWeight);
    save_prev_weight(sys.prevWeight);
    // Сохраняем дату фиксации (для UI "зафикс. от ДД.ММ.ГГГГ")
    TimeStamp ts = rtc_now();
    if (ts.valid) {
      DateTime dt(ts.year, ts.month, ts.day, ts.hour, ts.minute, ts.second);
      save_prev_weight_date(dt.unixtime());
    }
  };
  wa.onActivity = []() { lcd_backlight_activity(lcd); };
  wa.doSetCalibFactor = [](float cf) {
    sys.calibrationFactor = cf;
    scale.set_scale(cf);
    save_calibration(cf);
  };
  wa.doSetCalibOffset = [](long ofs) {
    sys.offset = ofs;
    scale.set_offset(ofs);
    save_offset(ofs);
  };

  webserver_init(wd, wa);
  webServerStarted = true;
}

void loop() {
  app_wdt_reset();
  if (sys.wifiOk) {
    ArduinoOTA.handle();
#if defined(ESP8266)
    MDNS.update();
#endif
  }
  static unsigned long lastTempRead   = 0;
  static unsigned long lastTsUpload   = 0;
  static unsigned long lastTgReport   = millis();
  static unsigned long lastBatRead    = 0;
  static unsigned long lastLogWrite   = 0;
  static unsigned long lastSensorChk  = 0;  // фича 14: watchdog HX711
  static int           sensorFailCnt  = 0;
  unsigned long now = millis();

  if (now - lastTempRead >= TEMP_READ_INTERVAL_MS) {
    if (temp_available()) {
      process_temperature();
    }
    sys.currentTime = rtc_now();
    sys.rtcTempC = rtc_temperature();
    // Обновляем datetimeStr сразу после RTC-чтения, чтобы log_append ниже
    // использовал актуальное время (не из прошлой итерации цикла).
    if (sys.currentTime.valid) {
      sys.datetimeStr = rtc_format_datetime(sys.currentTime);
    } else {
      // RTC недоступен — используем uptime как fallback, чтобы запись в лог не блокировалась
      unsigned long sec = millis() / 1000;
      char tb[20];
      snprintf(tb, sizeof(tb), "01.01.2020 %02lu:%02lu:%02lu",
               (sec / 3600) % 24, (sec / 60) % 60, sec % 60);
      sys.datetimeStr = tb;
    }
    lastTempRead = now;
    sys.needsRedraw = true;
  }

  if (now - lastBatRead >= BAT_READ_INTERVAL_MS) {
    sys.batVoltage = bat_voltage();
    sys.batPercent  = bat_percent();
    lastBatRead = now;
    sys.needsRedraw = true;
  }

  handle_buttons();
  process_weight();

  // Auto-return на главный экран (1/8) если 20 сек никто не трогал кнопки.
  // Не применяется на экранах 6 (calib menu) и 7 (diag) — там пользователь работает с интерфейсом.
  static const unsigned long AUTO_HOME_MS = 20000UL;
  if (sys.menuScreen != 0 && sys.menuScreen != 6 && sys.menuScreen != 7
      && (millis() - lastActivityTime) > AUTO_HOME_MS) {
    sys.menuScreen = 0;
    sys.needsRedraw = true;
  }

  update_interface();

  // ── Фича 14: Watchdog HX711 — авто-перезапуск датчика при зависании ──
  if (now - lastSensorChk >= 5000UL) {
    lastSensorChk = now;
    if (!scale.is_ready()) {
      sensorFailCnt++;
      if (sensorFailCnt >= 6) {  // 6 × 5с = 30с без ответа → перезапуск
        Serial.println(F("[HX711] Watchdog: reinit"));
        scale_init(scale, DT_PIN, SCK_PIN);
        sys.sensorReady = check_sensor(scale);
        if (sys.sensorReady) {
          scale.set_scale(sys.calibrationFactor);
          scale.set_offset(sys.offset);
          sys.emaInitialized = false;
          sys.smoothedWeight = 0.0f;
          Serial.println(F("[HX711] Recovered"));
        }
        sensorFailCnt = 0;
      }
    } else {
      sensorFailCnt = 0;
      if (!sys.sensorReady) {
        sys.sensorReady = true;
        sys.needsRedraw = true;
      }
    }
  }

  // В deep sleep режиме лог пишется один раз перед сном (ниже), здесь пропускаем
#ifndef SLEEP_MODE_DEEP_SLEEP
  {
    static uint16_t _lastSchedLogMin = 0xFFFF;
    static bool _bootLogDone = false;
    uint16_t stimes[8]; uint8_t scnt;
    get_sched_times(stimes, scnt);
    bool schedLog = false;
    if (sys.currentTime.valid && scnt > 0) {
      uint16_t cur_min = (uint16_t)sys.currentTime.hour * 60 + sys.currentTime.minute;
      for (uint8_t i = 0; i < scnt; i++) {
        if (stimes[i] == cur_min && _lastSchedLogMin != cur_min) {
          _lastSchedLogMin = cur_min; schedLog = true; break;
        }
      }
    }
    // Запись при каждом включении (однократно, как только датчик готов)
    bool bootLog = false;
    if (!_bootLogDone && sys.sensorReady && sys.currentTime.valid) {
      _bootLogDone = true;
      if (!schedLog) {  // не дублировать если это уже расписанное время
        bootLog = true;
        // предотвратить повторное срабатывание schedLog в эту же минуту
        if (sys.currentTime.valid)
          _lastSchedLogMin = (uint16_t)sys.currentTime.hour * 60 + sys.currentTime.minute;
      }
    }
    bool useInterval = (scnt == 0);  // интервал только если расписание не задано
    if (schedLog || bootLog || (useInterval && now - lastLogWrite >= LOG_INTERVAL_MS)) {
      log_append(sys.datetimeStr, sys.smoothedWeight,
                 sys.tempData.temperature, sys.tempData.humidity, sys.batVoltage, sys.batPercent);
      // При срабатывании расписания — ставим флаг отправки TG-отчёта.
      // Это решает проблему "ESP спит между расписаниями": каждое запланированное
      // пробуждение в 09:00, 14:00, 21:00 → одна отправка в Telegram с весом и дельтой.
      if (schedLog) tgReportPending = true;
      lastLogWrite = now;
      // v5.0.20: проверяем алерты при каждой записи в лог
      // (точно когда есть свежие значения батареи/температуры)
      alerts_check(sys.batVoltage, sys.batPercent,
                   sys.tempData.temperature, sys.currentTime.valid);
    }
  }
#endif

  lcd_backlight_tick(lcd, get_lcd_bl_sec());

  wifi_ensure_connected();
  // Определяем "эффективный" режим WiFi по факту, а не из EEPROM:
  // если STA-подключение упало и wifi_init() сделал fallback на AP, EEPROM остаётся в STA,
  // но реально работаем в AP. Без этой проверки веб-сервер останавливается через 7 сек.
  bool isApMode = (get_wifi_mode() == 0) || (WiFi.getMode() == WIFI_AP);
  if (isApMode) {
    sys.wifiOk = (WiFi.softAPIP() != IPAddress(0,0,0,0));
  } else {
    sys.wifiOk = (WiFi.status() == WL_CONNECTED);
    if (!sys.wifiOk && webServerStarted) {
      // Корректно останавливаем web-сервер перед сбросом флага — иначе при
      // реконнекте повторный _srv.begin() поверх живого слушающего сокета
      // может вести к утечкам / двойным handlers (пункт 7).
      webserver_stop();
      webServerStarted = false;
    }
    // v5.0.23: ntp_loop() ОТКЛЮЧЁН — баг configTime/getLocalTime в ESP32
    // Core 3.0.7 даёт Guru Meditation InstrFetchProhibited.
    // DS3231 RTC даёт точное время самостоятельно. Ручной NTP остаётся
    // через POST /api/ntp если пользователь нажмёт кнопку.
    // ntp_loop();
  }

  if (sys.wifiOk && !webServerStarted) {
    start_webserver();
  }

  if (sys.wifiOk && webServerStarted) {
    webserver_handle();
    static unsigned long lastQueueProc = 0;
    if (now - lastQueueProc >= 60000UL) {
      queue_process();
      lastQueueProc = now;
    }
  }

  if (get_wifi_mode() == 1 && sys.wifiOk) {
    if (now - lastTsUpload >= TS_UPDATE_INTERVAL_MS) {
      float rtcT = rtc_temperature();
      if (!ts_send(sys.smoothedWeight, sys.tempData.temperature,
                   sys.tempData.humidity, rtcT)) {
        queue_add(sys.smoothedWeight, sys.tempData.temperature, 
                  sys.tempData.humidity, rtcT, sys.datetimeStr);
      }
      lastTsUpload = now;
    }

    // Отправка TG-отчёта в двух случаях:
    // 1. Сработало расписание (tgReportPending=true) — отправляем сразу в это пробуждение
    // 2. Расписания нет, и просто прошёл интервал (для непрерывного режима без расписания)
    uint32_t tgRptMs = get_tg_report_interval_min() * 60000UL;
    uint8_t schedCnt = 0; { uint16_t st[8]; get_sched_times(st, schedCnt); }
    bool doTgReport = false;
    if (tgReportPending) {
      doTgReport = true;
    } else if (schedCnt == 0 && tgRptMs > 0 && now - lastTgReport >= tgRptMs) {
      doTgReport = true;
    }
    if (doTgReport) {
      TimeStamp ts = rtc_now();
      String dt = rtc_format_datetime(ts);

      // v5.0.5: гарантируем актуальную температуру перед отправкой отчёта.
      // После deep-sleep wake-up DS18B20 часто не успевает прочитаться через async
      // process_temperature() — первое чтение всегда пропускается (power-on 85°C),
      // а отчёт уходит в первые 5-10 сек. Делаем синхронный force-read (~750мс).
      // Если всё равно невалидно — fallback на persist.lastTempC (последняя сохранённая).
      float reportTempC = sys.tempData.temperature;
      if (reportTempC <= -90.0f && temp_available()) {
        TempData fresh = temp_force_read();
        if (fresh.valid) {
          sys.tempData = fresh;
          reportTempC = fresh.temperature;
        }
      }
      if (reportTempC <= -90.0f && persist.lastTempC > -90.0f) {
        reportTempC = persist.lastTempC;  // fallback на старое валидное значение
      }

      if (tg_send_report(sys.smoothedWeight, reportTempC,
                         sys.tempData.humidity, dt,
                         sys.prevWeight, load_prev_weight_date(),
                         persist.lastReportWeight, persist.hasLastReport)) {
        tgReportPending = false;
        lastTgReport = now;
        // После успешной отправки: текущий вес становится "вчерашним" для следующего отчёта.
        persist.lastReportWeight = sys.smoothedWeight;
        persist.hasLastReport = true;
        if (reportTempC > -90.0f) persist.lastTempC = reportTempC;
        sleep_save_persistent(persist);
        // v5.0.4: дублируем reportWeight в EEPROM, чтобы дельта не пропадала после reset/прошивки.
        save_last_report(sys.smoothedWeight, true);
        // v5.0.5: то же для температуры — fallback после reset.
        if (reportTempC > -90.0f) save_last_temp(reportTempC);
      }
      // если не отправилось (нет WiFi, нет токена) — флаг остаётся, попробуем позже
    }
  }

  check_auto_sleep();

#ifdef SLEEP_MODE_DEEP_SLEEP
  {
    log_append(sys.datetimeStr, sys.smoothedWeight,
               sys.tempData.temperature, sys.tempData.humidity, sys.batVoltage, sys.batPercent);
  }
  persist.lastWeight = sys.smoothedWeight;
  persist.lastTempC = sys.tempData.temperature;
  persist.wakeupCount++;
  sleep_save_persistent(persist);
#if defined(ESP32)
  esp_task_wdt_delete(NULL);
#endif
  uint32_t sleepDur = sys.currentTime.valid
    ? sched_next_sec(sys.currentTime.hour, sys.currentTime.minute)
    : get_sleep_sec();
  // Guard: sched_next_sec() может вернуть 0 в пограничном случае ровно на минуте расписания.
  // Минимум 60 сек чтобы ESP не циклил "проснулся-уснул".
  if (sleepDur < 60) sleepDur = 60;
  Serial.print(F("[Sleep] Sleep for "));
  Serial.print(sleepDur);
  Serial.println(F(" sec"));
  sleep_enter(sleepDur);
#endif
}

void handle_buttons() {
  // Boot grace period: первые 2 секунды после старта игнорируем кнопки.
  // На ESP8266 это критично из-за boot-strap пинов GPIO0/2; на ESP32 (GPIO27/26 — RTC)
  // таких проблем нет, но небольшой grace всё равно защищает от наводок при инициализации I2C/WiFi.
  if (millis() < 2000UL) {
    // На всякий случай ещё раз чистим ISR-флаги
    noInterrupts();
    btnMain.irqFell = false;
    btnMenu.irqFell = false;
    interrupts();
    return;
  }

  // Wake-press guard: после boot / deep-sleep-wake кнопка часто ещё физически удерживается
  // (пользователь только что её нажал, чтобы разбудить). Если начать считать удержание прямо
  // отсюда, через ~3.5 сек реального удержания сработает MEDIUM_PRESS → выскочит диалог тары
  // сам по себе. Поэтому игнорируем MAIN-кнопку до её первого release. Pressing-цикл начнёт
  // считаться только со следующего, "осознанного" нажатия.
  static bool waitFirstReleaseMain = true;
  if (waitFirstReleaseMain) {
    if (digitalRead(BUTTON_PIN) == HIGH) {
      waitFirstReleaseMain = false;
      noInterrupts();
      btnMain.irqFell = false;
      interrupts();
      btnMain.lastRaw = false;
      btnMain.isPressed = false;
      btnMain.longFired = false;
    } else {
      // Кнопка ещё держится с момента wake — поддерживаем подсветку и выходим.
      lastActivityTime = millis();
      lcd_backlight_activity(lcd);
      return;
    }
  }

  // v5.0.3: раздельные пороги по кнопкам.
  // MAIN: тара 3.0 сек, калибровка 6.0 сек. MENU: на 1/8 за 2.5 сек, без MEDIUM.
  ButtonAction actMain = read_button(BUTTON_PIN,    btnMain, 3000UL, 6000UL);
  ButtonAction actMenu = read_button(MENU_BTN_PIN,  btnMenu,    0UL, 2500UL);

  // Подсветка: реагировать на физическое нажатие сразу, не ждать debounce/release
  bool anyPressed = (digitalRead(BUTTON_PIN) == LOW || digitalRead(MENU_BTN_PIN) == LOW);
  if (anyPressed || actMain != NO_ACTION || actMenu != NO_ACTION) {
    lastActivityTime = millis();
    lcd_backlight_activity(lcd);
  }

  // Визуальная подсказка во время удержания MAIN — пользователь сразу видит, когда отпускать.
  // Показываем только на главных экранах (не 6/7, там SHORT работает иначе).
  static int mainHintLevel = 0;
  if (sys.menuScreen != 6 && sys.menuScreen != 7) {
    unsigned long mainHeld = button_hold_ms(btnMain);
    int level = 0;
    if (mainHeld >= 6000UL) level = 2;
    else if (mainHeld >= 3000UL) level = 1;
    if (level != mainHintLevel) {
      if (level == 1) {
        lcd_set_cursor(lcd,0, 1); lcd_print_padded(lcd, "Otpust = TARA!  ");
      } else if (level == 2) {
        lcd_set_cursor(lcd,0, 1); lcd_print_padded(lcd, "Otpust = KALIBR ");
      }
      // level == 0 (release) — обработчики MEDIUM/LONG ниже сами перерисуют LCD.
      mainHintLevel = level;
    }
  }

  // Логика MAIN-кнопки (v5.0.2 — возврат к v5.0.0 схеме):
  //   SHORT (быстрое нажатие, < 3.5 сек)         — на экранах 6/7 спец-режимы; иначе ничего
  //                                                 (просто будит экран / лечит подсветку,
  //                                                  случайных тарирований больше нет)
  //   MEDIUM (отпустил после ≥ 3.5 сек, < 6.5 c) — запрос подтверждения "Tara? MENU=OK".
  //                                                 Тара выполняется при нажатии MENU SHORT
  //                                                 в окне 4 сек, иначе — отмена.
  //   LONG (≥ 6.5 сек удержания, auto-fire)      — старт калибровочного мастера.
  static bool tareConfirmPending = false;
  static unsigned long tareConfirmStartMs = 0;

  if (actMain == SHORT_PRESS) {
    if (sys.menuScreen == 6) {
      adjust_calibration();
      sys.needsRedraw = true;
    } else if (sys.menuScreen == 7) {
      diagRunRequested = true;
      sys.needsRedraw = true;
    }
    // На остальных экранах MAIN SHORT — ничего: просто разбудили дисплей.
  }

  if (actMain == MEDIUM_PRESS && sys.menuScreen != 6 && sys.menuScreen != 7) {
    tareConfirmPending = true;
    tareConfirmStartMs = millis();
    lcd_clear_buf(lcd);
    lcd_set_cursor(lcd,0, 0); lcd_print_padded(lcd, "Tara? MENU=OK   ");
    lcd_set_cursor(lcd,0, 1); lcd_print_padded(lcd, "Otmena cherez 4s");
  }

  if (actMain == LONG_PRESS && sys.menuScreen != 6 && sys.menuScreen != 7) {
    tareConfirmPending = false;
    perform_calibration();
    sys.needsRedraw = true;
  }

  // Перехватываем MENU SHORT в режиме подтверждения тары — иначе он бы переключил меню.
  if (tareConfirmPending && actMenu == SHORT_PRESS) {
    tareConfirmPending = false;
    perform_taring();
    sys.needsRedraw = true;
    return;
  }

  // Таймаут окна подтверждения (4 сек) — тихая отмена с сообщением.
  if (tareConfirmPending && (millis() - tareConfirmStartMs > 4000UL)) {
    tareConfirmPending = false;
    lcd_clear_buf(lcd);
    lcd_set_cursor(lcd,0, 0); lcd_print_padded(lcd, "Tara: otmena    ");
    lcd_set_cursor(lcd,0, 1); lcd_print_padded(lcd, "                ");
    { unsigned long _t=millis(); while(millis()-_t<800UL){app_wdt_reset();yield();} }
    sys.needsRedraw = true;
  }
  static int menuPressCount = 0;
  static unsigned long lastMenuPressTime = 0;

  if (actMenu == SHORT_PRESS) {
    menuPressCount++;
    if (menuPressCount == 1) {
      lastMenuPressTime = millis();
    } else if (millis() - lastMenuPressTime < 500UL) {
      if (sys.hasPrevOffset) {
        undo_tare();
      } else {
        // Нет предыдущей тары — двойное нажатие = просто ещё один шаг меню
        sys.menuScreen = (sys.menuScreen + 1) % MENU_SCREENS;
        sys.needsRedraw = true;
      }
      menuPressCount = 0;
      return;
    } else {
      // Второе нажатие, но окно 500мс истекло (loop блокировался process_weight)
      // Прокручиваем меню за первое нажатие, текущее становится новым первым
      sys.menuScreen = (sys.menuScreen + 1) % MENU_SCREENS;
      sys.needsRedraw = true;
      menuPressCount = 1;
      lastMenuPressTime = millis();
    }
  }
  if (menuPressCount == 1 && millis() - lastMenuPressTime >= 500UL) {
    sys.menuScreen = (sys.menuScreen + 1) % MENU_SCREENS;
    sys.needsRedraw = true;
    menuPressCount = 0;
  }
  if (actMenu == LONG_PRESS) {
    sys.menuScreen = 0;
    menuPressCount = 0;
    sys.needsRedraw = true;
  }
}

void process_weight() {
  static unsigned long lastSaveTime         = 0;
  static unsigned long lastStableSaveTime   = 0;
  static unsigned long lastReadTime         = 0;
  static unsigned long lastPrevWeightSave   = 0;
  static float         lastSavedPrevWeight  = NAN;
  static float stableBuf[STABLE_BUF_SIZE];
  static int   stableBufIdx = 0;
  static int   stableBufCnt = 0;
  static bool  stableSaved  = false;

  // Период чтения HX711. 1500мс — компромисс между скоростью отклика UI
  // и нагрузкой на CPU/WebServer. HX711@10Hz даёт независимые сэмплы каждые 100мс.
  if (millis() - lastReadTime < SCALE_READ_INTERVAL_MS) return;

  // v5.0.6: пропускаем чтение HX711 если пользователь сейчас держит кнопку.
  // wait_ready_timeout(1000)+get_units() блокирует loop до 1.5 сек — за это время
  // теряются нажатия и сбивается отсчёт удержания. Откладываем чтение до отпускания.
  if (digitalRead(BUTTON_PIN) == LOW || digitalRead(MENU_BTN_PIN) == LOW) {
    return;
  }
  lastReadTime = millis();

  // spikeRejectCnt перенесён наверх функции чтобы его можно было сбросить
  // при раннем return по NaN (пункт 17). Ранее был static в середине ниже
  // по коду — это приводило к накоплению счётчика при серии NaN и возможному
  // случайному срабатыванию 5-rejection reset при первом валидном чтении.
  static int spikeRejectCnt = 0;

  float raw = scale_read_weight(scale, SCALE_READ_SAMPLES);
  if (isnan(raw)) {
    spikeRejectCnt = 0;
    if (sys.sensorReady) {
      sys.sensorReady = false;
      sys.needsRedraw = true;
    }
    return;
  }

  // --- Auto-zero deadband: |вес| < 50г показываем как 0 (убирает дрожание возле нуля) ---
  if (fabsf(raw) < ZERO_DEADBAND_KG) {
    raw = 0.0f;
  }

  if (!sys.sensorReady) {
    sys.sensorReady = true;
    sys.needsRedraw = true;
    Serial.println(F("[HX711] Sensor recovered!"));
  }

  // --- Spike-фильтр: отбросить показание если скачок > SPIKE_FILTER_KG ---
  // После 5 подряд отклонений — сброс EMA (вес мог резко измениться или была помеха)
  // (spikeRejectCnt объявлен в начале функции — пункт 17)
  if (sys.emaInitialized && fabsf(raw - sys.smoothedWeight) > SPIKE_FILTER_KG) {
    spikeRejectCnt++;
    Serial.print(F("[Spike] Rejected raw="));
    Serial.print(raw, 2);
    Serial.print(F(" cnt="));
    Serial.println(spikeRejectCnt);
    if (spikeRejectCnt >= 5) {
      spikeRejectCnt = 0;
      sys.emaInitialized = false;
      Serial.println(F("[Spike] EMA reset after 5 rejections"));
    }
    return;
  }
  spikeRejectCnt = 0;

  float alpha = web_get_ema_alpha();
  if (!sys.emaInitialized) {
    sys.smoothedWeight = raw;
    sys.emaInitialized = true;
  } else {
    sys.smoothedWeight = alpha * raw + (1.0f - alpha) * sys.smoothedWeight;
  }

  // --- Авто-фиксация стабильных показаний ---
  stableBuf[stableBufIdx] = sys.smoothedWeight;
  stableBufIdx = (stableBufIdx + 1) % STABLE_BUF_SIZE;
  if (stableBufCnt < STABLE_BUF_SIZE) stableBufCnt++;

  bool wasStable = sys.weightStable;
  if (stableBufCnt >= STABLE_BUF_SIZE) {
    float mn = stableBuf[0], mx = stableBuf[0];
    for (int i = 1; i < STABLE_BUF_SIZE; i++) {
      if (stableBuf[i] < mn) mn = stableBuf[i];
      if (stableBuf[i] > mx) mx = stableBuf[i];
    }
    sys.weightStable = (mx - mn) < STABLE_THR;
  } else {
    sys.weightStable = false;
  }

  if (sys.weightStable && !stableSaved) {
    unsigned long nowMs = millis();
    // При первой стабилизации в сессии:
    // 1) prevWeight — если не установлен (первый запуск), ставим текущий
    // 2) lastSavedWeight — сохраняем ВСЕГДА если вес изменился (не только при 0).
    //    Это позволяет следующей сессии знать "что было", даже если весы включали
    //    менее чем на 5 минут. prevWeight НЕ трогаем — он остаётся эталоном boot.
    if (sys.smoothedWeight > 0.1f) {
      if (sys.prevWeight < 0.1f) {
        sys.prevWeight = sys.smoothedWeight;
        save_prev_weight(sys.prevWeight);
        sys.needsRedraw = true;
      }
      if (fabsf(sys.smoothedWeight - sys.lastSavedWeight) > WEIGHT_SAVE_THR) {
        save_weight(sys.lastSavedWeight, sys.smoothedWeight);
        lastSaveTime = nowMs;
      }
    }
    if (nowMs - lastStableSaveTime >= STABLE_SAVE_MIN_MS) {
      save_weight(sys.lastSavedWeight, sys.smoothedWeight);
      // prevWeight НЕ обновляем — он меняется только вручную ("Сохранить" в веб)
      // или при первом запуске (auto-init выше). lastSavedWeight уже сохранён
      // при первой стабилизации сессии, поэтому следующий boot получит правильный
      // эталон без сброса дельты каждые 10 минут.
      stableSaved = true;
      lastStableSaveTime = nowMs;
      lastSaveTime = nowMs;
    } else {
      stableSaved = true;  // не писать в EEPROM, но флаг ставим
    }
  }
  if (!sys.weightStable) {
    stableSaved = false;
  }
  if (sys.weightStable != wasStable) {
    sys.needsRedraw = true;
  }
  // --- конец авто-фиксации ---

  float delta = fabsf(sys.smoothedWeight - sys.lastSavedWeight);
  unsigned long now = millis();
  if (delta > WEIGHT_SAVE_THR && now - lastSaveTime >= WEIGHT_SAVE_MS) {
    save_weight(sys.lastSavedWeight, sys.smoothedWeight);
    lastSaveTime = now;
    sys.needsRedraw = true;
  }

  // Периодическая запись prevWeight в EEPROM (раз в 5 мин, только если изменился и валиден)
  if (sys.prevWeight >= 0.1f &&
      (isnan(lastSavedPrevWeight) || fabsf(lastSavedPrevWeight - sys.prevWeight) > 0.001f)) {
    if (now - lastPrevWeightSave >= 5UL * 60UL * 1000UL) {
      save_prev_weight(sys.prevWeight);
      lastSavedPrevWeight = sys.prevWeight;
      lastPrevWeightSave  = now;
    }
  }

  if (get_wifi_mode() == 1) {
    static unsigned long lastAlertTime = 0;
    static const unsigned long ALERT_COOLDOWN_MS = 1800000UL;  // 30 мин между алертами
    float alertDelta = web_get_alert_delta();
    float deltaFromRef   = fabsf(sys.smoothedWeight - sys.prevWeight);
    float deltaFromAlert = fabsf(sys.smoothedWeight - persist.lastAlertWeight);
    bool firstAlert  = !persist.alertSent && deltaFromRef   >= alertDelta;
    bool repeatAlert =  persist.alertSent && deltaFromAlert >= alertDelta;
    bool cooldownOk  = (now - lastAlertTime >= ALERT_COOLDOWN_MS);
    if ((firstAlert || repeatAlert) && sys.wifiOk && cooldownOk) {
      TimeStamp ts = rtc_now();
      tg_send_alert(sys.smoothedWeight, sys.tempData.temperature,
                    rtc_format_datetime(ts));
      persist.alertSent    = true;
      persist.lastAlertWeight = sys.smoothedWeight;
      lastAlertTime = now;
    }
    // Сбрасываем флаг когда вес вернулся близко к эталону
    if (deltaFromRef < alertDelta * 0.5f) {
      persist.alertSent    = false;
      persist.lastAlertWeight = sys.prevWeight;
    }
  }
}

void process_temperature() {
  TempData td = temp_read();
  if (td.valid) {
    sys.tempData = td;  // обновляем только при успешном чтении
  }
  // При ошибке CRC — сохраняем предыдущее валидное значение
}

void show_screen_num(int n);  // forward declaration

void update_interface() {
  if (!sys.needsRedraw) return;
  if (sys.menuScreen != sys.lastMenuScreen) {
    lcd_clear_buf(lcd);
    if (sys.lastMenuScreen == 7) diagDone = false;  // сброс диагностики при уходе
    sys.lastMenuScreen = sys.menuScreen;
  }
  if (sys.sensorReady) {
    switch (sys.menuScreen) {
      case 0: display_screen_weight();     break;
      case 1: display_screen_diff();       break;
      case 2: display_screen_battery();    break;
      case 3: display_screen_temp();       break;
      case 4: display_screen_datetime();   break;
      case 5: display_screen_status();     break;
      case 6: display_screen_calib_menu(); break;
      case 7: display_screen_diag();       break;
    }
    // Номер экрана в правом углу строки 2 (экраны 6,7 — особый формат)
    if (sys.menuScreen < 6) {
      show_screen_num(sys.menuScreen);
    }
  } else {
    display_error();
  }
  sys.needsRedraw = sys.sensorReady ? false : true;  // мигать пока ошибка
}

// Показывает номер экрана "N/7" в позиции 13 строки 1
void show_screen_num(int n) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%d/%d", n + 1, MENU_SCREENS);
  lcd_set_cursor(lcd,13, 1);
  lcd.print(buf);
}

void display_screen_weight() {
  char buf[24];
  lcd_set_cursor(lcd,0, 0);
  snprintf(buf, sizeof(buf), "Ves:%6.2f%ckg", sys.smoothedWeight, sys.weightStable ? '*' : ' ');
  lcd_print_padded(lcd, buf);
  lcd_set_cursor(lcd,0, 1);
  if (sys.currentTime.valid) {
    snprintf(buf, sizeof(buf), "%02u:%02u:%02u %s",
             sys.currentTime.hour, sys.currentTime.minute, sys.currentTime.second,
             sys.wifiOk ? " W" : "  ");
  } else {
    snprintf(buf, sizeof(buf), "No RTC     %s", sys.wifiOk ? " W" : "  ");
  }
  lcd_print_padded(lcd, buf);
}

void display_screen_temp() {
  char buf[24];
  lcd_set_cursor(lcd,0, 0);
  if (sys.tempData.valid) {
    if (isnan(sys.rtcTempC)) snprintf(buf, sizeof(buf), "T:%.1fC  R:---", sys.tempData.temperature);
    else snprintf(buf, sizeof(buf), "T:%.1fC R:%.1fC", sys.tempData.temperature, sys.rtcTempC);
  } else {
    if (isnan(sys.rtcTempC)) snprintf(buf, sizeof(buf), "T:---   R:---");
    else snprintf(buf, sizeof(buf), "T:---  R:%.1fC", sys.rtcTempC);
  }
  lcd_print_padded(lcd, buf);

  lcd_set_cursor(lcd,0, 1);
  if (sys.tempData.humidity > -90) {
    snprintf(buf, sizeof(buf), "H: %4.1f %%", sys.tempData.humidity);
  } else {
    snprintf(buf, sizeof(buf), "H: -- %%  (DS18)");
  }
  lcd_print_padded(lcd, buf);
}

void display_screen_diff() {
  char buf[24];
  float diff = sys.smoothedWeight - sys.prevWeight;
  lcd_set_cursor(lcd,0, 0);
  snprintf(buf, sizeof(buf), "D:%+6.2fkg", diff);
  lcd_print_padded(lcd, buf);
  lcd_set_cursor(lcd,0, 1);
  snprintf(buf, sizeof(buf), "Pred:%5.2fkg", sys.prevWeight);
  lcd_print_padded(lcd, buf);
}

void display_screen_status() {
  char buf[24];
  lcd_set_cursor(lcd,0, 0);
  snprintf(buf, sizeof(buf), "CF:%.0f", sys.calibrationFactor);
  lcd_print_padded(lcd, buf);
  lcd_set_cursor(lcd,0, 1);
  snprintf(buf, sizeof(buf), "W:%s N:%lu",
           sys.wifiOk ? "OK" : "--", persist.wakeupCount);
  lcd_print_padded(lcd, buf);
}

void display_screen_datetime() {
  lcd_set_cursor(lcd,0, 0);
  lcd_print_padded(lcd, rtc_format_datetime(sys.currentTime));
  lcd_set_cursor(lcd,0, 1);
  lcd_print_padded(lcd, sys.currentTime.valid ? "RTC OK          " : "RTC ERROR!      ");
}

void display_screen_battery() {
  char buf[24];
  lcd_set_cursor(lcd,0, 0);
  snprintf(buf, sizeof(buf), "Bat:%4.2fV %3d%%", sys.batVoltage, sys.batPercent);
  lcd_print_padded(lcd, buf);
  lcd_set_cursor(lcd,0, 1);
  if (sys.batPercent < 10) {
    lcd_print_padded(lcd, "!LOW BATTERY!   ");
  } else {
    lcd_print_padded(lcd, "Li-Ion 1S       ");
  }
}

void display_screen_calib_menu() {
  char buf[24];
  lcd_set_cursor(lcd,0, 0);
  snprintf(buf, sizeof(buf), "CF:%.1f", sys.calibrationFactor);
  lcd_print_padded(lcd, buf);
  lcd_set_cursor(lcd,0, 1);
  lcd_print_padded(lcd, "MAIN=Vojti      ");
}

void display_screen_diag() {
  int diagPass = 0;
  const int diagTotal = 4;

  // Запуск теста: при входе на экран (diagDone ещё false) или по кнопке
  if (diagRunRequested || !diagDone) {
    diagRunRequested = false;

    char buf[17];
    lcd_clear_buf(lcd);
    lcd_set_cursor(lcd,0, 0);
    lcd_print_padded(lcd, "Diagnostika...");

    Serial.println(F("[DIAG] Start"));

    // --- HX711 ---
    lcd_set_cursor(lcd,0, 1);
    bool hx711ok = check_sensor(scale);
    if (hx711ok) {
      long raw = scale.read();
      lcd_print_padded(lcd, "HX711...    OK");
      Serial.print(F("[DIAG] HX711: OK (raw="));
      Serial.print(raw);
      Serial.println(F(")"));
      diagPass++;
    } else {
      lcd_print_padded(lcd, "HX711...  FAIL");
      Serial.println(F("[DIAG] HX711: FAIL"));
    }
    delay(1000);
    app_wdt_reset(); yield();

    // --- DS18B20 ---
    lcd_set_cursor(lcd,0, 1);
    if (!temp_available()) {
      lcd_print_padded(lcd, "DS18B20... FAIL");
      Serial.println(F("[DIAG] DS18B20: not found"));
    } else {
      // Первое чтение после init может быть невалидным (_firstRead).
      // Делаем два попытки с паузой, чтобы получить реальный результат.
      TempData td = temp_read();
      if (!td.valid) {
        delay(1100); app_wdt_reset();
        td = temp_read();
      }
      if (td.valid) {
        lcd_print_padded(lcd, "DS18B20...  OK");
        Serial.print(F("[DIAG] DS18B20: OK (temp="));
        Serial.print(td.temperature, 1);
        Serial.println(F(")"));
        diagPass++;
      } else {
        lcd_print_padded(lcd, "DS18B20..READ!");
        Serial.print(F("[DIAG] DS18B20: found but read FAIL ("));
        Serial.print(td.temperature, 1);
        Serial.println(F(")"));
      }
    }
    delay(1000);
    app_wdt_reset(); yield();

    // --- RTC ---
    lcd_set_cursor(lcd,0, 1);
    TimeStamp ts = rtc_now();
    bool rtcok = ts.valid;
    if (rtcok) {
      lcd_print_padded(lcd, "RTC...      OK");
      Serial.print(F("[DIAG] RTC: OK ("));
      snprintf(buf, sizeof(buf), "%02u.%02u.%04u", ts.day, ts.month, ts.year);
      Serial.print(buf);
      snprintf(buf, sizeof(buf), " %02u:%02u", ts.hour, ts.minute);
      Serial.print(buf);
      Serial.println(F(")"));
      diagPass++;
    } else {
      lcd_print_padded(lcd, "RTC...    FAIL");
      Serial.println(F("[DIAG] RTC: FAIL"));
    }
    delay(1000);
    app_wdt_reset(); yield();

    // --- Battery ---
    lcd_set_cursor(lcd,0, 1);
    float bv = bat_voltage();
    bool batok = bv > 0.5f;
    if (batok) {
      lcd_print_padded(lcd, "Battery...  OK");
      Serial.print(F("[DIAG] Battery: OK ("));
      Serial.print(bv, 2);
      Serial.print(F("V, "));
      Serial.print(bat_percent());
      Serial.println(F("%)"));
      diagPass++;
    } else {
      lcd_print_padded(lcd, "Battery.. FAIL");
      Serial.print(F("[DIAG] Battery: FAIL ("));
      Serial.print(bv, 2);
      Serial.println(F("V)"));
    }
    delay(1000);
    app_wdt_reset(); yield();

    // --- Сводка ---
    lcd_clear_buf(lcd);
    lcd_set_cursor(lcd,0, 0);
    snprintf(buf, sizeof(buf), "Diag: %d/%d %s", diagPass, diagTotal,
             diagPass == diagTotal ? " OK" : "FAIL");
    lcd_print_padded(lcd, buf);
    lcd_set_cursor(lcd,0, 1);
    lcd_print_padded(lcd, "MAIN=povtor");

    Serial.print(F("[DIAG] Result: "));
    Serial.print(diagPass);
    Serial.print('/');
    Serial.print(diagTotal);
    Serial.println(diagPass == diagTotal ? F(" OK") : F(" FAIL"));

    diagDone = true;
  }
  // Если diagDone — сводка уже на экране, ничего не делаем (ждём кнопку)
}

void adjust_calibration() {
  Serial.println(F("[AdjCal] Enter"));
  float cf = sys.calibrationFactor;
  const float steps[] = {10.0f, 1.0f, 0.1f};
  int stepIdx = 0;
  char buf[24];

  lcd_clear_buf(lcd);
  unsigned long lastWeighTime = 0;
  float liveWeight = sys.smoothedWeight;
  unsigned long adjustStart = millis();

  for (;;) {
    if (millis() - adjustStart > 300000UL) { lastActivityTime = millis(); break; } // 5 мин таймаут
    app_wdt_reset();

    // Обновляем вес каждые 500мс
    unsigned long now = millis();
    if (now - lastWeighTime >= 500UL) {
      float raw = scale_read_weight(scale, SCALE_READ_SAMPLES);
      if (!isnan(raw)) liveWeight = raw;
      lastWeighTime = now;

      // Обновляем LCD
      lcd_set_cursor(lcd,0, 0);
      if (steps[stepIdx] >= 1.0f)
        snprintf(buf, sizeof(buf), "CF:%-7.1f+/-%.0f", cf, steps[stepIdx]);
      else
        snprintf(buf, sizeof(buf), "CF:%-7.1f+/-.1", cf);
      lcd_print_padded(lcd, buf);

      lcd_set_cursor(lcd,0, 1);
      snprintf(buf, sizeof(buf), "Ves:%6.2fkg", liveWeight);
      lcd_print_padded(lcd, buf);
    }

    // Чтение кнопок (мастер adjust_calibration: внутри своего таймминга,
    // MEDIUM не используем; LONG для MAIN=сохранить, MENU=минус-шаг)
    ButtonAction actMain = read_button(BUTTON_PIN,   btnMain, 0UL, 2000UL);
    ButtonAction actMenu = read_button(MENU_BTN_PIN, btnMenu, 0UL, 1000UL);

    if (actMain == SHORT_PRESS) {
      // Переключить шаг: 10 → 1 → 0.1 → 10 ...
      stepIdx = (stepIdx + 1) % 3;
      lastWeighTime = 0;  // форсировать перерисовку
    }

    if (actMenu == SHORT_PRESS) {
      // CF + шаг
      cf = constrain(cf + steps[stepIdx], 100.0f, 50000.0f);
      scale.set_scale(cf);
      lastWeighTime = 0;
    }

    if (actMenu == LONG_PRESS) {
      // CF − шаг
      cf = constrain(cf - steps[stepIdx], 100.0f, 50000.0f);
      scale.set_scale(cf);
      lastWeighTime = 0;
    }

    if (actMain == LONG_PRESS) {
      // Сохранить и выйти
      cf = constrain(cf, 100.0f, 50000.0f);
      sys.calibrationFactor = cf;
      scale.set_scale(cf);
      save_calibration(cf);
      sys.emaInitialized = false;

      lcd_clear_buf(lcd);
      lcd_set_cursor(lcd,0, 0); lcd_print_padded(lcd, "Sokhraneno!     ");
      snprintf(buf, sizeof(buf), "CF:%.1f", cf);
      lcd_set_cursor(lcd,0, 1); lcd_print_padded(lcd, buf);
      { unsigned long _t0=millis(); while(millis()-_t0<1500UL){app_wdt_reset();yield();} }
      sys.needsRedraw = true;
      lastActivityTime = millis();
      Serial.print(F("[AdjCal] Saved CF=")); Serial.println(cf, 1);
      return;
    }

    yield();
  }
  // Restore CF after timeout (break exits without saving)
  scale.set_scale(sys.calibrationFactor);
  sys.emaInitialized = false;
  sys.needsRedraw = true;
}

void display_error() {
  static bool blink = false;
  static unsigned long lastBlink = 0;
  unsigned long _nb = millis();
  if (_nb - lastBlink >= 500UL) {
    blink = !blink;
    lastBlink = _nb;
  }
  lcd_set_cursor(lcd,0, 0);
  lcd_print_padded(lcd, blink ? "*** OSHIBKA! ***" : "                ");
  lcd_set_cursor(lcd,0, 1);
  lcd_print_padded(lcd, "Check HX711 wire");
}

void perform_taring() {
  Serial.println(F("[Tare] Start"));

  // Сохраняем предыдущий offset для возможности отмены
  sys.prevOffset = sys.offset;
  sys.hasPrevOffset = true;
  save_prev_offset(sys.prevOffset);

  lcd_clear_buf(lcd);
  lcd_set_cursor(lcd,0, 0); lcd_print_padded(lcd, " Tarirovka...   ");
  lcd_set_cursor(lcd,0, 1); lcd_print_padded(lcd, " Podozhdite...  ");

  // Пауза 1.5 сек — дать датчику успокоиться после нажатия кнопки
  { unsigned long _t=millis(); while(millis()-_t<1500UL){app_wdt_reset();yield();} }
  app_wdt_reset();

  // Проверяем готовность датчика перед тарированием
  if (!scale.wait_ready_timeout(3000)) {
    lcd_clear_buf(lcd);
    lcd_set_cursor(lcd,0, 0); lcd_print_padded(lcd, "Tara: OSHIBKA!  ");
    lcd_set_cursor(lcd,0, 1); lcd_print_padded(lcd, "HX711 ne otvech.");
    { unsigned long _t0=millis(); while(millis()-_t0<2000UL){app_wdt_reset();yield();} }
    sys.needsRedraw = true;
    return;
  }

  // Ручной tare с yield — вместо блокирующего scale.tare(30) который вызывал WDT reset
  {
    long sum = 0;
    const int TARE_SAMPLES = 20;
    for (int i = 0; i < TARE_SAMPLES; i++) {
      if (!scale.wait_ready_timeout(500)) continue;
      sum += scale.read();
      app_wdt_reset();
      yield();
    }
    scale.set_offset(sum / TARE_SAMPLES);
  }
  sys.offset = scale.get_offset();
  sys.smoothedWeight = 0.0f;
  sys.emaInitialized = false;
  save_offset(sys.offset);

  char _ofs[17]; snprintf(_ofs, sizeof(_ofs), "Ofs:%ld", sys.offset);
  lcd_clear_buf(lcd);
  lcd_set_cursor(lcd,0, 0); lcd_print_padded(lcd, "Tara: OK        ");
  lcd_set_cursor(lcd,0, 1); lcd_print_padded(lcd, _ofs);
  app_wdt_reset();
  { unsigned long _t0=millis(); while(millis()-_t0<1200UL){app_wdt_reset();yield();} }
  sys.needsRedraw = true;
  Serial.print(F("[Tare] Offset=")); Serial.println(sys.offset);
}

void undo_tare() {
  if (!sys.hasPrevOffset) {
    lcd_clear_buf(lcd);
    lcd_set_cursor(lcd,0, 0); lcd_print_padded(lcd, "Tara: net       ");
    lcd_set_cursor(lcd,0, 1); lcd_print_padded(lcd, "predydushchej   ");
    { unsigned long _t0=millis(); while(millis()-_t0<1200UL){app_wdt_reset();yield();} }
    sys.needsRedraw = true;
    return;
  }

  Serial.println(F("[Tare] Undo"));
  sys.offset = sys.prevOffset;
  scale.set_offset(sys.offset);
  save_offset(sys.offset);
  sys.emaInitialized = false;
  sys.smoothedWeight = 0.0f;

  lcd_clear_buf(lcd);
  lcd_set_cursor(lcd,0, 0); lcd_print_padded(lcd, "Tara: Otmena    ");
  char _ofs[17]; snprintf(_ofs, sizeof(_ofs), "Ofs:%ld", sys.offset);
  lcd_set_cursor(lcd,0, 1); lcd_print_padded(lcd, _ofs);
  app_wdt_reset();
  { unsigned long _t0=millis(); while(millis()-_t0<1200UL){app_wdt_reset();yield();} }
  sys.needsRedraw = true;
  Serial.print(F("[Tare] Restored offset=")); Serial.println(sys.offset);
}

void perform_calibration() {
  Serial.println(F("[Calib] Start"));

  auto wait_press = [](int pin, uint32_t timeout_ms) -> bool {
    unsigned long t = millis();
    while (digitalRead(pin) == HIGH) {
      app_wdt_reset(); yield();
      if (millis() - t > timeout_ms) return false;
    }
    // Ждём отпускание кнопки (с таймаутом от залипания)
    unsigned long t2 = millis();
    while (digitalRead(pin) == LOW) {
      app_wdt_reset(); yield();
      if (millis() - t2 > 10000UL) return false;  // 10с таймаут на залипание
    }
    return true;
  };

  lcd_clear_buf(lcd);
  lcd_set_cursor(lcd,0, 0); lcd_print_padded(lcd, "Ubrat gruz!     ");
  lcd_set_cursor(lcd,0, 1); lcd_print_padded(lcd, "Zhmi knopku...  ");
  unsigned long start = millis();
  while (digitalRead(BUTTON_PIN) == LOW) {
    app_wdt_reset(); yield();
    if (millis() - start > 10000) { sys.needsRedraw = true; lastActivityTime = millis(); return; }
  }
  if (!wait_press(BUTTON_PIN, 30000)) { sys.needsRedraw = true; lastActivityTime = millis(); return; }

  // Проверяем HX711 перед тарированием
  if (!scale.wait_ready_timeout(3000)) {
    lcd_clear_buf(lcd);
    lcd_set_cursor(lcd,0, 0); lcd_print_padded(lcd, "OSHIBKA HX711!  ");
    lcd_set_cursor(lcd,0, 1); lcd_print_padded(lcd, "Proverte provod!");
    { unsigned long _t0=millis(); while(millis()-_t0<2000UL){app_wdt_reset();yield();} }
    sys.needsRedraw = true; lastActivityTime = millis();
    return;
  }

  // Тарируем с scale=1 чтобы get_units вернул сырые единицы АЦП
  scale.set_scale(1.0f);
  // Пауза перед tare — датчик должен успокоиться
  { unsigned long _t=millis(); while(millis()-_t<500UL){app_wdt_reset();yield();} }
  // Диагностика: два подряд чтения — должны быть похожи но НЕ одинаковы
  long dbgA = scale.read();
  long dbgB = scale.read();
  Serial.print(F("[Calib] read1=")); Serial.print(dbgA);
  Serial.print(F(" read2=")); Serial.println(dbgB);
  if (dbgA == dbgB) {
    Serial.println(F("[Calib] WARNING: HX711 stuck! Power cycling..."));
    scale.power_down();
    delayMicroseconds(100);
    scale.power_up();
    delay(400);
    scale.set_scale(1.0f);
    if (!scale.wait_ready_timeout(3000)) {
      lcd_clear_buf(lcd);
      lcd_set_cursor(lcd,0, 0); lcd_print_padded(lcd, "HX711 zavis!    ");
      lcd_set_cursor(lcd,0, 1); lcd_print_padded(lcd, "Proverte provod!");
      { unsigned long _t0=millis(); while(millis()-_t0<2000UL){app_wdt_reset();yield();} }
      sys.needsRedraw = true; lastActivityTime = millis();
      return;
    }
  }
  // Ручной tare с yield — вместо блокирующего scale.tare(10)
  {
    long sum = 0;
    const int TARE_SAMPLES = 10;
    for (int i = 0; i < TARE_SAMPLES; i++) {
      scale.wait_ready_timeout(500);
      sum += scale.read();
      app_wdt_reset();
      yield();
    }
    scale.set_offset(sum / TARE_SAMPLES);
  }
  long zeroOffset = scale.get_offset();
  Serial.print(F("[Calib] zero offset=")); Serial.println(zeroOffset);

  lcd_clear_buf(lcd);
  lcd_set_cursor(lcd,0, 0); lcd_print_padded(lcd, "Polozh. 1 kg    ");
  lcd_set_cursor(lcd,0, 1); lcd_print_padded(lcd, "Zhmi knopku...  ");
  if (!wait_press(BUTTON_PIN, 30000)) { sys.needsRedraw = true; lastActivityTime = millis(); return; }

  // Пауза 1.5 сек — дать грузу стабилизироваться
  lcd_clear_buf(lcd);
  lcd_set_cursor(lcd,0, 0); lcd_print_padded(lcd, "Kalibrovka...   ");
  { unsigned long _t=millis(); while(millis()-_t<1500UL){app_wdt_reset();yield();} }

  if (!scale.wait_ready_timeout(3000)) {
    lcd_clear_buf(lcd);
    lcd_set_cursor(lcd,0, 0); lcd_print_padded(lcd, "OSHIBKA HX711!  ");
    lcd_set_cursor(lcd,0, 1); lcd_print_padded(lcd, "Povtorite       ");
    { unsigned long _t0=millis(); while(millis()-_t0<2000UL){app_wdt_reset();yield();} }
    sys.needsRedraw = true; lastActivityTime = millis();
    return;
  }

  // Читаем сырое значение (get_value = ADC - offset, scale=1)
  double rawD = scale.get_value(SCALE_CALIB_SAMPLES);
  float raw = (float)rawD;
  Serial.print(F("[Calib] raw ADC units=")); Serial.println(raw, 0);

  if (fabsf(raw) < 1000.0f) {
    // Первая попытка не удалась — power-cycle и повтор
    Serial.print(F("[Calib] raw too small=")); Serial.println(raw, 0);
    Serial.println(F("[Calib] Retrying after power cycle..."));
    scale.power_down();
    delayMicroseconds(100);
    scale.power_up();
    delay(400);
    scale.set_scale(1.0f);
    scale.set_offset(zeroOffset);  // восстанавливаем offset от tare
    if (scale.wait_ready_timeout(3000)) {
      rawD = scale.get_value(SCALE_CALIB_SAMPLES);
      raw = (float)rawD;
      Serial.print(F("[Calib] Retry raw=")); Serial.println(raw, 0);
    }
    if (fabsf(raw) < 1000.0f) {
      lcd_clear_buf(lcd);
      lcd_set_cursor(lcd,0, 0); lcd_print_padded(lcd, "OSHIBKA raw=0   ");
      char dbg[17]; snprintf(dbg, sizeof(dbg), "raw=%.0f", raw);
      lcd_set_cursor(lcd,0, 1); lcd_print_padded(lcd, dbg);
      Serial.print(F("[Calib] ERROR: raw too small=")); Serial.println(raw, 0);
      { unsigned long _t0=millis(); while(millis()-_t0<3000UL){app_wdt_reset();yield();} }
      sys.needsRedraw = true; lastActivityTime = millis();
      return;
    }
  }
  // Знак raw сохраняем: при «обратном» подключении тензодатчика raw<0,
  // тогда calibrationFactor получится отрицательный, и HX711 lib вернёт
  // положительный вес во время работы. Инверсию из b74c04d убрали —
  // она ломала показания при реальном reverse-подключении.

  sys.calibrationFactor = raw / (web_get_calib_weight() / 1000.0f);
  scale.set_scale(sys.calibrationFactor);
  save_calibration(sys.calibrationFactor);
  // Сохраняем новый offset — scale.tare() внутри калибровки изменил его
  sys.offset = scale.get_offset();
  save_offset(sys.offset);
  // Обновляем prevOffset чтобы "отмена тары" не откатила к до-калибровочному offset
  sys.prevOffset = sys.offset;
  sys.hasPrevOffset = false;
  save_prev_offset(sys.prevOffset);
  // Сбрасываем EMA и smoothedWeight — иначе spike-фильтр заблокирует новые показания
  sys.smoothedWeight = 0.0f;
  sys.emaInitialized = false;

  lcd_clear_buf(lcd);
  lcd_set_cursor(lcd,0, 0); lcd_print_padded(lcd, "OK! Factor:     ");
  { char _cbuf[16]; dtostrf(sys.calibrationFactor, 7, 3, _cbuf); lcd_set_cursor(lcd,0, 1); lcd_print_padded(lcd, _cbuf); }
  app_wdt_reset();
  { unsigned long _t0=millis(); while(millis()-_t0<2500UL){app_wdt_reset();yield();} }
  sys.needsRedraw = true;
  lastActivityTime = millis();
  Serial.print(F("[Calib] Factor=")); Serial.println(sys.calibrationFactor, 4);
}

void check_auto_sleep() {
  uint16_t autoSec = get_autosleep_sec();
  if (autoSec == 0) return;  // 0 = не засыпать (отладка / постоянное питание)

  // Web-продление: пользователь нажал "☕ Продлить на 10 мин" — пока не истёк дедлайн, в сон не уходим.
  // (long) cast обрабатывает переход millis() через 0 за ~49 дней. Когда продление кончилось, обнуляем
  // и сбрасываем lastActivityTime — чтобы пользователь получил один полный обычный idle-цикл.
  if (extendSleepUntilMs != 0) {
    if ((long)(extendSleepUntilMs - millis()) > 0) return;
    extendSleepUntilMs = 0;
    lastActivityTime = millis();
    return;
  }

  unsigned long autoMs = (unsigned long)autoSec * 1000UL;
  if (millis() - lastActivityTime < autoMs) return;

  Serial.print(F("[AutoSleep] "));
  Serial.print(autoSec);
  Serial.println(F(" sec idle — shutting down..."));

  // Показываем сообщение на LCD (включаем подсветку — она могла быть выключена по таймауту)
  lcd.backlight();
  lcd_clear_buf(lcd);
  lcd_set_cursor(lcd,0, 0); lcd_print_padded(lcd, "Auto sleep...   ");
  lcd_set_cursor(lcd,0, 1); lcd_print_padded(lcd, "Btn to wake up  ");
  { unsigned long _t0=millis(); while(millis()-_t0<3000UL){app_wdt_reset();yield();} }

  // Гасим подсветку LCD и очищаем DDRAM (иначе остаются "квадратики" от старых символов)
  lcd_clear_buf(lcd);
  lcd.noBacklight();
  lcd.noDisplay();  // ESP32-bonus: ещё снижает потребление PCF8574

  // ВАЖНО: WebServer закрываем ПЕРЕД деактивацией WiFi, иначе TCP-сокет _srv висит на уже
  // выключенном netif и при последующем _srv.stop()/деструкторе вызывает NULL-pointer panic
  // (PC=0x00000000, EXCCAUSE=0x14, сразу после "wifi:NAN WiFi stop") → SW reset вместо deep sleep.
  // На ESP32 ручной WiFi.disconnect()/WiFi.mode(WIFI_OFF) не вызываем — esp_deep_sleep_start()
  // корректно отключит WiFi сам. Тот же принцип — в SleepManager.cpp:84-87.
  if (webServerStarted) webserver_stop();
  webServerStarted = false;
#if defined(ESP8266)
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
#endif

  // Записываем лог перед сном
  {
    log_append(sys.datetimeStr, sys.smoothedWeight,
               sys.tempData.temperature, sys.tempData.humidity, sys.batVoltage, sys.batPercent);
  }

  // Сохраняем данные
  save_weight(sys.lastSavedWeight, sys.smoothedWeight);
  save_prev_weight(sys.prevWeight);   // сохраняем опорный вес дельты перед сном
  persist.lastWeight = sys.smoothedWeight;
  persist.lastTempC = sys.tempData.temperature;
  persist.wakeupCount++;
  sleep_save_persistent(persist);

  // HX711 → power_down: standby ~1.5 мА → ~1 мкА. Просыпается scale.power_up() в setup().
  scale.power_down();

  // Уходим в deep sleep (пробуждение по кнопке GPIO 0)
#if defined(ESP32)
  esp_task_wdt_delete(NULL);
#endif
  { uint32_t sleepDur = sys.currentTime.valid
      ? sched_next_sec(sys.currentTime.hour, sys.currentTime.minute)
      : get_sleep_sec();
    // Защита от циклов "проснулся → уснул на 0 сек → проснулся": минимум 60 сек.
    // Если попали в минуту расписания — sched_next_sec может вернуть 0 → не циклим, ждём минуту.
    if (sleepDur < 60) sleepDur = 60;
    Serial.print(F("[AutoSleep] Sleep for "));
    Serial.print(sleepDur);
    Serial.println(F(" sec"));
    sleep_enter(sleepDur);
  }
}

void show_splash_screen() {
  lcd_clear_buf(lcd);
  lcd_set_cursor(lcd,0, 0); lcd_print_padded(lcd, " Vesy Pchelovod ");
  char verLine[17];
  snprintf(verLine, sizeof(verLine), "  Versiya %s", FW_VERSION);
  lcd_set_cursor(lcd,0, 1); lcd_print_padded(lcd, verLine);
  { unsigned long _t0=millis(); while(millis()-_t0<1200UL){app_wdt_reset();yield();} }

  if (sys.sensorReady) {
    float current = scale_read_weight(scale, 5);
    if (!isnan(current)) {
      float diff = current - sys.lastSavedWeight;
      char buf[17];
      snprintf(buf, sizeof(buf), "VES: %+6.2f kg", diff);
      lcd_set_cursor(lcd,0, 1);
      lcd_print_padded(lcd, buf);
      { unsigned long _t0=millis(); while(millis()-_t0<2500UL){app_wdt_reset();yield();} }
    }
  }

  lcd_clear_buf(lcd);
  sys.needsRedraw = true;
}

