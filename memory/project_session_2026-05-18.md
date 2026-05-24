---
name: project_session_2026-05-18
description: Сессия 2026-05-18 — v5.0.25 фикс WiFi после deep sleep НЕ помог, баг сохраняется. Тест с auto-sleep=0
metadata:
  type: project
---

# Session 2026-05-18 — v5.0.25 WiFi crash после deep sleep всё ещё актуален

## Что было сделано в сессии

### v5.0.25 — попытка №2 фикса wake-up WiFi crash (НЕ ПОМОГЛО ПОЛНОСТЬЮ)

**Изменения:**
- `Version.h` → 5.0.24 → 5.0.25
- `Connectivity.cpp` `wifi_connect()`:
  - Усиленный reset: `disconnect(true,true) → OFF → delay 500мс → STA → delay 200мс → begin` (было 100мс)
  - При WIFI_TIMEOUT_MS → `ESP.restart()` (до 2 попыток через RTC counter `_wifiRetryCount`)
- `BeehiveScale.ino` `setup()`:
  - Добавлен `delay(200)` после `Serial.begin()` если `esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED` (т.е. для всех wake-up causes)
  - Добавлен `#include <esp_sleep.h>`
- WDT остался `trigger_panic = true`, timeout 30с (false не делает hard reset, только warning)

### Симптомы v5.0.25 (хуже чем v5.0.24)

Лог:
```
POWERON_RESET → boot v5.0.25 → WiFi connect 0.8-3.8с → OK
[Sleep] Wakeup #2 → WiFi connect 2с → CRASH (TG1WDT_SYS_RESET rst:0x8)
auto-restart → boot → WiFi 11с TIMEOUT → ESP.restart() (attempt 1/2) → SW_CPU_RESET (rst:0xc)
boot → WiFi 1.5с → OK
Sleep 300с → Wakeup #2 → WiFi Connecting... → ПОЛНЫЙ ЛОК НА 2 ЧАСА (LCD splash, Serial мёртв, сайт не отвечает)
```

### Подробный разбор почему не помогло

**1. WDT 30с не помог** — IWDT (TG1WDT) срабатывает за <1с (600мс по умолчанию) когда WiFi-стек блокирует interrupts. Task WDT 30с уже поздно.

**2. ESP.restart() при timeout не всегда срабатывает** — иногда crash происходит ДО timeout (мгновенно при `WiFi.begin()`).

**3. v5.0.24 фикс + увеличенные delay (500+200мс) — недостаточно** — что-то в WiFi-стеке после deep sleep остаётся в "мёртвом" состоянии независимо от delay.

**4. delay(200) перед WDT init** — помог немного на POWERON, но не для DEEPSLEEP wake.

### Решение (временное) — auto-sleep=0

После 2 часов мёртвого лока user сделал hardware reset (кнопка EN на DevKit). После boot — зашёл на сайт → Настройки → **Уйти в сон после бездействия = 0** (не засыпать!) → сохранил. ESP теперь не уходит в deep sleep → нет crash на wake.

USB оставили подключённым (питание + Serial Monitor). Тестирование стабильности на сутки.

**Расход в этом режиме:** ~120 мА × 24ч = 2880 мА·ч/день. На банке 21700 4500 мА·ч = ~1.5 дня. Только для теста — не для улья.

## Архитектурный диагноз

**Проблема не в коде wifi_connect.** Проблема в самом ESP32 Arduino Core 3.0.7 — WiFi-стек после deep sleep не восстанавливается надёжно.

**Кандидаты на финальный фикс (v5.0.26):**
1. **Light Sleep вместо Deep Sleep** — лучший вариант. WiFi-стек НЕ теряется → нет crash → потребление ~0.8-2 мА (даже лучше 6 мА deep sleep).
2. **Downgrade ESP32 Arduino Core до 2.0.17** — если баг specific для 3.0.7.
3. **`esp_phy_erase_cal_data_in_nvs()` перед каждым boot** — может калибровочные данные WiFi портятся.
4. **Прямой вызов `esp_wifi_init()` / `esp_wifi_start()`** вместо Arduino `WiFi.mode()`.
5. **NVS clear при wake** — некоторые форумы советуют.

Запущены 2 фоновых агента:
- `problem-investigator` — глубокий аудит кода wifi_connect + sleep_enter
- `general-purpose` с WebSearch — поиск решений на форумах ESP32 (PC 0x400e999e, TG1WDT, Core 3.0.7)

После их отчётов — собрать план v5.0.26.

## Что НЕ работало (отвергнутые гипотезы из этой сессии)

- ❌ Увеличение delay 100→500мс между WIFI_OFF и WIFI_STA
- ❌ ESP.restart() при WIFI_TIMEOUT — не всегда успевает сработать
- ❌ delay(200) после wake перед WDT init
- ❌ trigger_panic=false для WDT (не делает hard reset, только warning — НЕ ТРОГАТЬ)

## Что НЕ НУЖНО повторять

- MT3608 — уже доказано не виноват
- XL6009 — не нужен
- Капы на VOUT — не помогли
- Заряжать банку лучше — банка норм 4.2В
- NTP — уже отключён

## Текущее состояние железа (без изменений с 2026-05-17)

- ESP32 DevKit V1, v5.0.25 прошита
- HT7333 + MT3608 + 21700 4500мАч
- HX711 + pull-up SCK 10к → 3V3
- LCD I²C 0x27, DS3231 (на 3V3), DS18B20
- Все LED выпаяны
- **Auto-sleep=0** (не засыпает) — временно для теста стабильности
- USB подключён к ноуту для Serial + питание

## Что Геннадий ждёт

- Детали из Озона: 1N5819, SS14, AO3401, Type-C female bulkhead (для солнечной зарядки + P-MOSFET LCD)

## v5.0.26 ГОТОВ К ПРОШИВКЕ (на основе отчётов 2 агентов)

### Изменения

**Connectivity.cpp** `wifi_connect()`:
- `esp_wifi_stop()` в начале → cleanup half-state из предыдущего boot
- Правильный порядок: `mode(WIFI_STA)` ДО `disconnect(true,true)` (баг 3.0.x — disconnect на uninitialized handle)
- `esp_phy_erase_cal_data_in_nvs()` если `_wifiRetryCount >= 2` (recover от corruption PHY calibration)
- `_wifiRetryCount` теперь в `RTC_DATA_ATTR` (переживает ESP.restart)
- Сброс счётчика на успешном connect

**SleepManager.cpp** `sleep_enter()` (главный фикс):
- ДОБАВЛЕНО: `WiFi.disconnect(true,false)` + `delay(100)` + `esp_wifi_stop()` + `delay(50)` ПЕРЕД `esp_deep_sleep_start()`
- Старый комментарий "esp_wifi_deinit вызывает NULL panic" — относился только к `deinit()`, не к `stop()`. Stop безопасен.

**BeehiveScale.ino** `setup()`:
- `app_wdt_init()` СРАЗУ после `Serial.begin` (ДО delay) — иначе delay не защищён WDT
- delay 200→500мс после DEEPSLEEP_RESET + `esp_task_wdt_reset()`

**Connectivity.cpp** `tg_send_alert()` + caller в BeehiveScale.ino:
- Новый параметр `refWeight`
- В TG-сообщении: причина (📈 прирост / 📉 убыль роение/кража) + дельта `+/- N.NN кг` + было/стало

### Источники
- GitHub espressif/arduino-esp32 #9658, #9329, #9913
- PR #9904 Deep Sleep Fix (Core 3.0.x регрессия)
- ESP-IDF Sleep Modes: esp_wifi_stop требуется перед sleep
- ESP-IDF RF Calibration: esp_phy_erase_cal_data_in_nvs

### Fallback если v5.0.26 не помогает
1. **Downgrade Core 2.0.17** через Arduino IDE Boards Manager (по форумам — самое надёжное)
2. **Light Sleep** — большая переделка, но WiFi-стек не теряется → нет crash

## v5.0.26 РЕЗУЛЬТАТ: НЕ ПОМОГ — Wake #2 опять виснет

Лог 12:38: `[Sleep] WiFi stopped cleanly` сработал, но wake #2 опять зависает на `[WiFi] Connecting` → ⸮ мусор → полный лок. ESP.restart() не успевает сработать (зависание мгновенное).

## v5.0.27 — финальный комбо-фикс по итогам 3 параллельных агентов

### Новое понимание ⸮ мусора

Agent 3 (web search) дал альтернативную трактовку: ⸮ = **UART baud divider mismatch** (НЕ обрезанный backtrace). После wake CPU может оказаться на 80/160 MHz вместо 240 (modem-sleep auto-entry меняет APB). UART делитель неверный → ⸮. WiFi-стек ждёт clock который не там → silent hang.

Issues: espressif/arduino-esp32 #7240, #6032, #7182.

### Все 3 root causes (комбо)

1. **CPU частота** не зафиксирована на 240 MHz после wake (Agent 3)
2. **GPIO27 (EXT0 wakeup pin)** остаётся в RTC mux после wake → конфликт RTC/PHY clock domain. НИ ОДНОГО `rtc_gpio_deinit` во всём коде (Agent 1+2 ⭐)
3. **RTC_PERIPH retention OFF** по умолчанию в IDF 5.x → ext0 latch теряется, PHY calibration cache в RTC FAST RAM корраптится (Agent 1)

### Фиксы в v5.0.27

**BeehiveScale.ino setup()** — в самом начале (до Serial!):
- `esp_sleep_get_wakeup_cause()` сохранить в переменную
- `setCpuFrequencyMhz(240)` — форс 240 МГц (фикс UART baud)
- Если wake — `rtc_gpio_deinit(GPIO_NUM_27)` + `gpio_reset_pin(27)` (освобождение RTC mux)
- Лог `[Wake] cause=N, CPU=240 MHz` для диагностики

**BeehiveScale.ino setup() WiFi block:**
- `WiFi.mode(WIFI_STA)` + `WiFi.setTxPower(WIFI_POWER_11dBm)` **ПЕРЕД** wifi_init() (brownout protection)

**SleepManager.cpp sleep_init():**
- `rtc_gpio_deinit(SLEEP_WAKEUP_PIN)` перед enable_ext0_wakeup
- `esp_sleep_pd_config(RTC_PERIPH, ON)` + RTC_SLOW_MEM + RTC_FAST_MEM (явно держим)

**SleepManager.cpp sleep_enter():**
- `Serial.flush()` + `uart_wait_tx_idle_polling(UART_NUM_0)` ДО esp_deep_sleep_start (Issue #6032)

**Connectivity.cpp:**
- УБРАН ESP.restart() retry loop — он накапливал RTC slow memory corruption между SW_CPU_RESET циклами (Agent 1 рекомендация)

### Fallback если v5.0.27 НЕ помогает

**Downgrade Arduino-ESP32 Core до 2.0.17** через Boards Manager (Agent 1: 80% работает). 2.0.17 = IDF 4.4 без этой регрессии. Не требует правки кода — перепрошить тот же v5.0.27.

Если и это не помогает → **Light Sleep** (переделка кода ~100 строк).

## v5.0.27/28 РЕЗУЛЬТАТЫ — баг ослаблен но не пофиксен

**v5.0.27**: `⸮` мусор ушёл (CPU 240 + UART flush сработали), но WiFi.mode/setTxPower перед wifi_init дал `netstack cb reg failed 12308` → AP fallback на каждом wake.

**v5.0.28**: откат setTxPower-before. POWERON работает, Wake #2 опять виснет (мусор `ssss` теперь).

## История версий Arduino-ESP32 Core тестируем:

| Core | Результат |
|---|---|
| 3.0.7 (исходная) | 100% wake #2 крашат |
| 2.0.17 | Arduino IDE OOM при компиляции, JVM heap 512MB → даже после увеличения до 2048M IDE падает на 89% |
| **3.1.3** ⭐ | **17% wake ломаются (3-5 wake подряд работают, потом 1 крашит)** — лучший вариант |
| 3.3.8 | 100% wake #2 крашит (регрессия!) |

## v5.0.29 — ФИНАЛЬНОЕ РЕШЕНИЕ: LIGHT SLEEP

**Идея:** не лечим WiFi-crash после wake, а ИЗБАВЛЯЕМСЯ от wake-from-deep-sleep вообще.

**Light Sleep**:
- CPU выключен → 0.5 мА
- RAM сохранена → нет re-init
- WiFi modem-sleep → радио spike ~5мс каждые 100мс на beacon → average 0.8-2 мА
- `esp_light_sleep_start()` ВОЗВРАЩАЕТСЯ после wake (не reboot)
- WiFi association не теряется

**Потребление в sleep**: 0.8-2 мА (даже меньше deep sleep 6 мА!)

### Изменения в v5.0.29

**SleepManager.cpp `sleep_enter()`:**
- `esp_deep_sleep_start()` → `esp_light_sleep_start()`
- Убрано `WiFi.disconnect/stop` перед sleep (не нужно — WiFi остаётся)
- Добавлено `WiFi.setSleep(WIFI_PS_MIN_MODEM)` перед sleep для экономии
- После wake — `WiFi.setSleep(false)`, `_persist.wakeupCount++`, лог `[LightSleep] Awake #N, cause=...`
- Освобождение PERIPHERAL_POWER_PIN из RTC mux после wake (если pin >= 0)

**BeehiveScale.ino:**
- Убран `persist.wakeupCount++` ПЕРЕД sleep_enter (теперь делается ВНУТРИ sleep_enter после wake)
- Убран `esp_task_wdt_delete(NULL)` перед sleep (WDT должен защищать loop после wake)
- После `sleep_enter()` — `scale.power_up()` + `lastActivityTime = millis()` для следующего цикла
- Loop теперь paradigm "boot → setup → loop forever (with light sleep pauses)" вместо "boot → setup → loop once → deep sleep → reboot"

### Автономия (расчёт)

- Active 120мА × 120с idle = 4 мА·ч
- Light sleep 1.5мА × ~280с = 0.12 мА·ч
- На цикл 5 мин (300с) = ~4.12 мА·ч
- За час = 12 цикл × 4 мА·ч ≈ wait — это слишком много на active. Если idle меньше → лучше.

Реальный расчёт при auto-sleep 30с idle + sleep 5 мин:
- Active 120мА × 30с = 1 мА·ч
- Sleep 1.5мА × 270с = 0.11 мА·ч
- На цикл ~1.1 мА·ч × 12/ч × 24 = 317 мА·ч/день → 21700 = ~14 дней

Это **хуже** теоретического deep sleep (22 дня). Но **РАБОТАЕТ**. И с auto-sleep можно идти ещё ниже.

Если уменьшить idle до 10с — будет ~7 дней автономии. Это плохо.

**Альтернатива**: оставить deep sleep идею, но использовать Light Sleep ТОЛЬКО как fallback для проблемных wake. Сложно.

**Реалистично с light sleep**: 14-30 дней автономии + солнечная панель = бесконечно работает в улье.

### Core recommendation для v5.0.29

Установить **Core 3.1.3** (наш лучший рабочий). Можно и 3.0.7 — с light sleep deep-sleep баг не задействуется.

Прошить v5.0.29 → проверить → если работает стабильно → готово к улью.

## Бонус — TG алерты теперь с причиной

Было:
```
🚨 ТРЕВОГА: улей
Время: ...
Вес: 5.46 кг
Температура: 29.6 С
```

Стало (v5.0.26):
```
🚨 ТРЕВОГА: улей
Причина: 📈 резкий прирост веса
Изменение: +0.84 кг
Время: 18.05.2026 11:32:12
Вес: 5.46 кг (было 4.62)
Температура: 29.6 С
```

## Полезные команды

```bash
# Открыть Arduino IDE
# Файл → Открыть → BeehiveScale.ino
# Скетч → Загрузка (Ctrl+U)

# Версия в Version.h
# Перепрошивать обязательно с Huge APP partition
```

## ВАЖНО для следующей сессии

1. **Прочитай этот файл ПЕРВЫМ**
2. Не предлагай MT3608 / XL6009 / капы (отвергнуто)
3. Не предлагай downgrade NTP (это другой баг, уже отключено)
4. Главная задача — баг WiFi после deep sleep
5. Light Sleep — приоритетный путь
