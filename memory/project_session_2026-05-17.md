# Session 2026-05-13 → 2026-05-17 — большой апгрейд железа + софта

## Версии прошивки сделанные в сессии

| Версия | Дата | Что |
|---|---|---|
| **v5.0.18** | 2026-05-11 | PWA-иконка обновлена (минималистичная пчела вместо геометрии) |
| **v5.0.19** | 2026-05-13 | Калибровка делителя батареи через сайт (EEPROM 336-340) |
| **v5.0.20** | 2026-05-13 | Настраиваемые Telegram-алерты: батарея/температура/RTC. Модуль Alerts.h/cpp. EEPROM 342-356 |
| **v5.0.21** | 2026-05-17 | Отложенный WiFi 8 сек после boot + TX power 11dBm + modem-sleep. Фикс MT3608 OCP защёлки |
| **v5.0.22** | 2026-05-17 | NTP sync на boot отключён (Guru Meditation InstrFetchProhibited на ESP32 Core 3.0.7) |
| **v5.0.23** | 2026-05-17 | ntp_loop() в main отключён + WiFi.setSleep(false) (modem-sleep блокировал webserver) |
| **v5.0.24** | 2026-05-17 | Жёсткий reset WiFi перед connect после deep sleep wake (WIFI_OFF→delay→WIFI_STA→disconnect→begin). PC 0x400e999e краш каждого wake-up |

## Текущее состояние железа Геннадия

**Конфигурация (в макетной сборке, корпус собран):**
- ESP32-WROOM-32 DevKit V1
- **HT7333 LDO** вместо AMS1117 (~5 мА экономии)
- **MT3608 Boost** (замена сгоревшему AS21)
- **21700 банка 4500 мА·ч**
- **Делитель R1+R2 100к** на GPIO34
- **Pull-up 10к на SCK HX711 → 3V3** (даёт -4 мА в sleep, обязательно)
- HX711 + тензодатчик, LCD 1602 I²C, DS3231 (на 3V3 не 5В!), DS18B20, кнопки
- **Все LED выпаяны** (ESP32 PowerLED+USB LED, DS3231 PowerLED+R5, TP4056 CHRG+OK)

**Замеры:**
- Active: ~120 мА
- Deep sleep: **6 мА** (после всех оптимизаций)
- Калибровка батареи через сайт: реальное 4.10В → ratio ~2.124 откалибровано (Cal. Factor HX711 = 20534)
- Автономия 21700: **~22 дня** без подзарядки

## Cколько раз перепрошивал в сессии

Геннадий прошил каждую версию v5.0.19...v5.0.24 — итого 6 раз через Arduino IDE. ESP32 DOIT V1 + Huge APP partition.

## Главная проблема сессии — каждый wake крашит WiFi

Симптом:
- POWERON_RESET — работает
- DEEPSLEEP_RESET (wake #2, #3...) — крашит на `[WiFi] Connecting to: Beeline⸮` с Guru Meditation PC 0x400e999e
- Telegram приходил только на 1 wake, остальные молчат

Лечение (v5.0.24):
- `WiFi.persistent(false); WiFi.mode(WIFI_OFF); delay(100); WiFi.mode(WIFI_STA); WiFi.disconnect(true, true); delay(100); WiFi.begin(...)`
- Без этого WiFi-регистры после deep sleep в грязном состоянии → крах

Также фиксы по дороге (всё в Connectivity.cpp / BeehiveScale.ino):
- NTP отключён (баг ESP32 Core 3.0.7 в configTime/getLocalTime → InstrFetchProhibited). DS3231 даёт время самостоятельно (±2 мин/год)
- WiFi.setSleep(false) — modem-sleep блокировал webserver
- Отложенный WiFi 8 сек после boot/wake — даёт капам зарядиться, MT3608 не уходит в OCP защёлку
- TX power снижен до 11dBm

## Новые фичи через сайт (без перепрошивки)

**Калибровка батареи (v5.0.19) — вкладка Калибровка:**
- Endpoint /api/battery/calib (GET/POST)
- POST {realVoltage: 4.10} → авто-расчёт ratio
- POST {ratio: 2.124} → ручная установка
- Сохраняется в EEPROM addr 336-340

**Telegram-алерты (v5.0.20) — вкладка Telegram:**
- Endpoint /api/alerts (GET/POST)
- batLowV (вольты, например 3.6)
- tempLow / tempHigh (°C)
- rtcEn (bool)
- Модуль Alerts.h/cpp с анти-спам через гистерезис
- alerts_check(batV, batPct, tempC, rtcValid) вызывается при каждой записи в лог

## GitHub репо — публичный сейчас

- URL: https://github.com/darkvolg/BeehiveScale-ESP32
- Логотип PCB-пчела (docs/assets/logo.png)
- Иконка PWA минималистичная (docs/assets/icon.png)
- Маркетинг cartoon (docs/assets/bee-scale-cartoon.png)
- Social preview banner 1280x640 (docs/assets/social-preview.jpg) — загружен через Settings UI
- README двуязычный: README.md (русский) + README.en.md (английский)
- 15 topics: esp32, arduino, iot, beekeeping, smart-scale, hx711, telegram-bot, pwa, beehive, apiary, load-cell, deep-sleep, diy, hardware, open-source
- Description: «Smart bee hive scale with ESP32. Weight, temperature, Wi-Fi, Telegram, charts, 26+ days battery life. Open-source DIY project.»

## Полная электрическая схема

Сделан SVG `hardware/full-schematic.svg` — все блоки, диод/конденсатор правильно (диод последовательно, кап параллельно), цветные провода, легенда, замечания по пайке. После многих итераций объяснений Геннадий понял схему.

## Солнечная панель

- TZT solar panel 5В / 200мА / 1Вт, размер 107×61мм
- Test conditions: Light intensity 38000 LUX
- Подключение по плану через USB-C female на корпусе → 1N5819 → 470µF → TP4056
- **Что Геннадий заказал и ждёт:**
  - 1N5819 (TH) + SS14 (SMD) — диоды Шоттки
  - AO3401 (P-MOSFET) + BC547 (NPN у него уже есть) — для P-MOSFET LCD off в sleep
  - Type-C female bulkhead разъём для корпуса
- Где паять: внутри корпуса между Type-C female и TP4056

Расчёт выработки:
- Летом ясный день: ~750-900 мА·ч/день в банку
- При расходе 200 мА·ч/день → прибыль ~600 мА·ч/день
- В улей с панелью можно поставить и забыть на сезон

## Что осталось до улья

1. Дождаться 1N5819 + AO3401 + Type-C female (заказано)
2. Спаять P-MOSFET high-side для LCD off (AO3401 + BC547 + 2×10к) → даст +20 дней автономии (до 44 дней)
3. Спаять солнечную зарядку (диод 1N5819 + 470µF + Type-C female)
4. Проверить v5.0.24 в реальной работе несколько дней без USB
5. Финальная сборка в IP65 корпус
6. **В улей!**

## Подозреваемые проблемы которые могут вернуться

- MT3608 нестабилен в холодную погоду (внутреннее сопротивление банки растёт). При -10°C может опять защёлка. Альтернатива: XL6009 (3А, auto-restart).
- ntp_sync_time() оставлен в коде но не вызывается. TODO: переписать на esp_sntp_init() для будущей версии.
- Heap leak возможен после многих циклов — пока не наблюдается, но нужно мониторить.

## Полезные команды для следующей сессии

```bash
# Текущая ветка
git log --oneline | head -10

# Серийный лог
# Arduino IDE → Tools → Serial Monitor 115200

# Прошить
# Arduino IDE → Sketch → Upload (Huge APP partition!)

# Калибровка батареи через сайт (без перепрошивки)
curl -X POST http://192.168.0.58/api/battery/calib \
  -H "Content-Type: application/json" \
  -H "X-CSRF-Token: ..." \
  -d '{"realVoltage": 4.10}'

# Алерты
curl http://192.168.0.58/api/alerts
```

## ОБЯЗАТЕЛЬНО для следующей сессии

1. Спросить Геннадия: работает ли v5.0.24 стабильно? Все wake-ups приходят TG?
2. Если да → продолжить с P-MOSFET LCD + солнечная зарядка
3. Если нет → серийный лог, копать дальше (возможно XL6009)
4. Cпросить: пришли ли заказанные детали (диоды, MOSFET, Type-C)
5. Не лезть менять код пока Геннадий тестит — он уже устал прошивать
