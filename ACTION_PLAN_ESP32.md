# BeehiveScale — Порт на ESP32 (2026-05-05)

**Цель:** перенести прошивку с ESP8266 (NodeMCU v0.1 / Lolin V3) на ESP32-WROOM-32 DevKit, потому что ESP8266 не пробуждается из deep sleep ни через GPIO16→RST, ни через DS3231 alarm после 5+ часов попыток (1N4148, резисторы 470Ω/1кΩ, WAKE_RFCAL/RF_DISABLED, питание от батареи без USB — всё проверено).

**ESP32 deep sleep работает железно из коробки** — подтверждено WakeTestESP32 2026-05-05.

---

## START HERE — приоритеты порта

### 🔴 P0 — Минимальный boot (Фаза 1)
**Почему P0:** без этого нет основы. Цель — чтобы ESP32 загрузился, показал на LCD версию, не уронился. Без HX711, без WiFi.

- [x] Создана папка `BeehiveScale-ESP32/` (копия из `BeehiveScale-main/`)
- [x] WakeTestESP32 подтвердил работу deep sleep на новой плате (Wake #7+)
- [x] **platformio.ini** — добавить `[env:esp32dev]` рядом с `d1_mini` (двойная сборка)
- [x] **Version.h** — bump до `5.0.0-pre`
- [x] **BeehiveScale.ino** — pin mapping для ESP32 (см. таблицу ниже)
- [x] **SleepManager.cpp** — `esp_deep_sleep_start()`, `esp_sleep_enable_timer_wakeup()`, `esp_sleep_enable_ext0_wakeup()` для DS3231 SQW (P0-уровень: wake по кнопке GPIO27, peripheral_power отключён — нет P-MOSFET)
- [x] **Memory.cpp** — оставлен на EEPROM эмуляции (работает на ESP32 через NVS, мигрировать на Preferences не обязательно)
- [x] **Display.cpp** — LiquidCrystal_I2C компилируется на ESP32 OK
- [x] **Logger.cpp** — заменён ESP8266 LittleFS `Dir/openDir` на ESP32 `File/openNextFile`, `ESP.wdtFeed()` → `yield()`
- [x] **WebServerModule.cpp** — `collectHeaders()` теперь принимает массив для ESP32
- [x] **Сборка прошла:** `pio run -e esp32dev` → SUCCESS, RAM 16.2%, Flash 94.5% (1.24MB/1.31MB)
- [x] **Залить на железо:** прошивка через Arduino IDE с Partition Scheme=Huge APP, ArduinoJson 6.x
- [x] LCD показал «Vesy Pchelovod / Versiya 5.0.0-pre» (Геннадий 2026-05-05)
- [x] Auto-sleep через 3 мин подтверждён (экран погас — корректное поведение)

### 🟡 P1 — Измерения (Фаза 2)
**Почему P1:** ядро функционала.

- [ ] **Scale.cpp** (HX711) — проверить пины DT=GPIO16, SCK=GPIO17, библиотека работает на ESP32
- [ ] **Temperature.cpp** (DS18B20) — пин GPIO4, OneWire библиотека универсальна
- [ ] **Battery.cpp** — ESP32 ADC: `adc1_get_raw(ADC1_CHANNEL_6)` на GPIO34 + `esp_adc_cal` калибровка
- [ ] **Button.cpp** — пины GPIO27 (MAIN), GPIO26 (MENU). На ESP32 нет boot-strap проблемы как на D3/D4
- [ ] Калибровка HX711 на гире
- [ ] Тест: вес показывается, температура показывается, кнопки работают

### 🟢 P2 — Связь (Фаза 3)
**Почему P2:** WiFi/Telegram нужны но не критичны для базовой работы.

- [ ] **Connectivity.cpp** — `WiFi.h` (не `ESP8266WiFi.h`), `WebServer.h` (не `ESP8266WebServer.h`), `ESPmDNS.h`
- [ ] **WebServerModule.cpp** — `WebServer` API, основные endpoint совместимы
- [ ] **Logger.cpp** — `LittleFS` работает на ESP32 через библиотеку
- [ ] OTA обновления через ArduinoOTA
- [ ] Тест: Web-панель открывается, Telegram отчёты приходят

### 🔵 P3 — Deep Sleep + DS3231 (Фаза 4) ⭐ ПРИЧИНА ПЕРЕХОДА
**Почему P3:** ради этого мы и перешли. На ESP32 — это просто.

- [ ] **SleepManager.cpp** — DS3231 alarm wake через `esp_sleep_enable_ext0_wakeup(GPIO_NUM_33, 0)` (LOW level wake)
- [ ] **RTC_Module.cpp** — настройка alarm регистров DS3231 (код тот же что на ESP8266)
- [ ] Тест: ESP32 уснул → DS3231 даёт alarm в назначенное время → ESP32 просыпается → лог веса → снова сон
- [ ] Замер потребления в спячке (ESP32 в deep sleep ~5-10 мкА, ожидаем суммарно ~50 мкА с обвязкой)

### ⚪ P4 — Финализация
- [ ] Финальная прошивка в улей
- [ ] Тест автономии: 18650 на 3-7 дней
- [ ] Bump до v5.0.0 (без -pre), tag, push

---

## 📌 Pin Mapping ESP32-WROOM-32 (DOIT DevKit V1)

| Модуль | GPIO | Silkscreen | Примечание |
|--------|------|-----------|-----------|
| **HX711 DT** | 16 | D16 | свободный, не strapping |
| **HX711 SCK** | 17 | D17 | свободный |
| **LCD SDA** | 21 | D21 | стандарт I2C |
| **LCD SCL** | 22 | D22 | стандарт I2C |
| **DS3231 SDA** | 21 | D21 | общий I2C bus с LCD |
| **DS3231 SCL** | 22 | D22 | общий I2C bus с LCD |
| **DS3231 SQW** | 33 | D33 | RTC GPIO ⭐ (для EXT0 wake) |
| **DS18B20** | 4 | D4 | RTC GPIO, OneWire |
| **Button MAIN** | 27 | D27 | RTC GPIO (можно wake кнопкой) |
| **Button MENU** | 26 | D26 | RTC GPIO |
| **Battery ADC** | 34 | D34 | ADC1_CH6, input-only |

### Перепайка
Геннадий должен переподключить провода со старой ESP8266 на ESP32 по этой таблице. Это ~30 минут работы. Делать ПОСЛЕ того как Phase 1 будет собран и залит хоть раз.

---

## 📋 Полезные правки уже в коде

В рабочей копии есть незакоммиченные изменения с прошлой сессии — **частично переносим в ESP32-проект:**

| Файл | Изменение | Переносим? |
|------|-----------|-----------|
| BeehiveScale.ino | LEGACY_HX711_PINS закомментирован | ✅ (для ESP32 не актуально, но останется) |
| BeehiveScale.ino | lcd.backlight() в check_auto_sleep + 3 sec | ✅ полезный UX |
| SleepManager.cpp | cap 70 мин для ESP8266 | ❌ не нужно (ESP32 без лимита) |
| WebServerModule.cpp | мелкие правки | ✅ |
| Version.h | бамп до v4.2.12 | ❌ → v5.0.0-pre |

---

## 🔗 Ссылки

- ESP32 deep sleep API: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/sleep_modes.html
- ESP32 ADC: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/adc_oneshot.html
- ArduinoESP32 reference: https://espressif-docs.readthedocs-hosted.com/projects/arduino-esp32/en/latest/

---

## 🚫 НЕ ТРОГАТЬ

1. **`BeehiveScale-main/`** — не лезем туда. Та папка ESP8266, остаётся для отката если ESP32-порт пойдёт криво.
2. **GPIO 6-11** на ESP32 — внутренняя SPI flash, никогда не использовать.
3. **GPIO 0, 2, 12, 15** — boot-strapping pins, использовать осторожно (мы их и не трогаем).
4. **GPIO 34-39** — input only, не выводить как OUTPUT (Battery ADC=34 — это вход, OK).

---

## 📊 Лог выполнения

| Дата | Этап | Что сделано | Кто |
|---|---|---|---|
| 2026-05-05 | P0.1 | Создана папка BeehiveScale-ESP32, скопирована из main | Claude |
| 2026-05-05 | P0.2 | WakeTestESP32 подтвердил deep sleep wake на новой плате | Геннадий |
| 2026-05-05 | P0.3 | ACTION_PLAN_ESP32.md создан | Claude |
| 2026-05-05 | P0.4 | BeehiveScale.ino: pin mapping ESP32 (HX711 16/17, btn 27/26) | Claude |
| 2026-05-05 | P0.5 | SleepManager: SLEEP_WAKEUP_PIN=27, PERIPHERAL_POWER=-1 | Claude |
| 2026-05-05 | P0.6 | Logger.cpp: ESP32 LittleFS API (File.openNextFile), убран ESP.wdtFeed | Claude |
| 2026-05-05 | P0.7 | WebServerModule: collectHeaders с массивом для ESP32 | Claude |
| 2026-05-05 | P0.8 | **Сборка ESP32 SUCCESS**: RAM 16.2%, Flash 94.5% (1238233 байт) | Claude |
| 2026-05-05 | P0.9 | Battery.cpp: ADC_ATTEN_DB_11 → ADC_11db (совместимость с ESP32 Core 3.0+) | Claude |
| 2026-05-05 | P0.10 | **Прошивка залита на железо** через Arduino IDE (Huge APP partition, ArduinoJson 6.x) | Геннадий |
| 2026-05-05 | P0.11 | **LCD показал v5.0.0-pre** — P0 (Фаза 1) ЗАВЕРШЕНА | Геннадий |
| 2026-05-05 | P1.1 | HX711 откалиброван (эталон 4.43, точность ±10г) | Геннадий |
| 2026-05-05 | P1.2 | Кнопки D27/D26 работают, перевёл TARE на удержание 3 сек | Claude+Геннадий |
| 2026-05-05 | P1.3 | DS18B20 (D4) и DS3231 (D21/D22) подключены и работают | Геннадий |
| 2026-05-05 | P1.4 | Точность веса 3→2 знака на LCD и web | Claude |
| 2026-05-05 | P2.1 | LittleFS auto-format при первом boot — web заработал | Claude |
| 2026-05-05 | P2.2 | Фикс AP-fallback (WiFi.getMode()==WIFI_AP) — webserver стабилен | Claude |
| 2026-05-05 | P2.3 | Мобильная CSS-вёрстка (tabs scroll, hdr flex-wrap) | Claude |
| 2026-05-05 | P2.4 | **STA подключился к Beeline**, IP 192.168.0.58 | Геннадий |
| 2026-05-05 | P3.1 | Sleep guard 60 сек, лог [AutoSleep] Sleep for X sec | Claude |
| 2026-05-05 | P3.2 | **Расписание 15:07 сработало** — wake точно в 15:07:15 | Геннадий |
| 2026-05-05 | END | **P0+P1+P2+P3 ЗАКРЫТЫ за одну сессию.** Осталось P4 (hw корпус, питание 18650) | — |
