---
name: project_current_state
description: Текущая версия BeehiveScale-ESP32 и работающие фичи на 2026-05-11
type: project
---

# Текущее состояние v5.0.17 (2026-05-11)

**Why:** база чтобы при заходе в проект понимать что уже сделано и не дублировать. Версия меняется часто — обновляй этот файл при каждом bump.
**How to apply:** при старте сессии прочитай LightRAG → если нет — этот файл → потом конкретные модули.

## Платформа
- ESP32-WROOM-32 DevKit (DOIT v1, 38 пинов)
- PlatformIO, `pio run -e esp32dev` для сборки, `-t upload` для прошивки
- Partition: `huge_app.csv` (3 МБ кода / 1 МБ SPIFFS, **без OTA**)
- Filesystem: LittleFS (SD отключена с v4.2.1, HX711 переехал на пины бывшего SPI)
- Текущий размер прошивки: 1.27 МБ (40.5%), RAM 17.5% — запас огромный

## Работающие фичи
- HX711 весы + DS18B20 температура + DS3231 RTC + ADC батареи
- Deep sleep с пробуждением по DS3231 SQW (ext0 wake)
- LiquidCrystal_I2C дисплей, две кнопки (MAIN, MENU)
- Web-интерфейс single-page (PROGMEM PAGE_HTML в WebServerModule.cpp)
- Telegram-отчёты по расписанию (3 раза в день по умолчанию: 09:00, 14:00, 21:00)
- CSV лог во LittleFS, ротация при достижении 100 КБ → /log_YYMMDD_HHMM.csv
- Калибровка через web, OTA через ArduinoOTA
- **v5.0.16** Архивная вкладка с выбором периода, лентой дней, цветными маркерами аномалий
- **v5.0.16** PWA — manifest.json + service worker + SVG icon, устанавливается на главный экран телефона
- **v5.0.17** Пресет "С прошлого визита" — EEPROM хранит timestamp прошлого открытия web

## Текущая версия
v5.0.17 (Version.h: MAJOR=5, MINOR=0, PATCH=17). Bump после каждого функционального коммита.

## Git
- main branch, push в https://github.com/darkvolg/BeehiveScale-ESP32.git
- User: darkvolg (Геннадий)
- Последние коммиты: 58aded5 (5.0.16), ebd3f34 (5.0.17)

## Что в работе у пользователя (не закоммичено)
В корне проекта остались несомместные изменения от Геннадия: hardware/pinout.md, manual.html, pages/01..07_*.html. Это его параллельная работа над документацией. Не трогать без явной просьбы.
