# BeehiveScale v4.1

Умные весы для пчелиного улья на ESP8266 (Lolin NodeMCU). Автономный мониторинг веса, температуры, логирование на SD-карту, веб-интерфейс и Telegram-уведомления.

## Возможности

- **HX711** — тензодатчик весов с EMA-сглаживанием и авто-фиксацией стабильных показаний
- **DS18B20** — датчик температуры
- **DS3231 RTC** — часы реального времени с батарейкой
- **LCD 1602 I2C** — дисплей с 7 экранами меню (вес, дельта, батарея, температура, дата/время, статус, калибровка)
- **SD-карта** — логирование CSV каждую минуту (datetime, weight, temp, humidity, battery)
- **Веб-интерфейс** — live-дашборд с REST API, тёмная тема
- **Telegram** — уведомления при резком изменении веса (роение, кража)
- **ThingSpeak** — отправка данных в облако
- **Deep Sleep** — автоматический сон через 3 мин неактивности для экономии батареи
- **Батарея** — мониторинг напряжения через A0 с делителем 2:1
- **Калибровка** — ручная подстройка через кнопки без перепрошивки
- **Тарировка** — обнуление с возможностью отмены (двойное нажатие MENU)

## Компоненты

| Компонент | Кол-во | Примечание |
|-----------|--------|------------|
| ESP8266 NodeMCU (Lolin) | 1 | Основной контроллер |
| HX711 + тензодатчик 50 кг | 1 | Весовой модуль |
| DS18B20 | 1 | Термометр (водонепроницаемый) |
| DS3231 RTC | 1 | Часы с батарейкой CR2032 |
| LCD 1602 + I2C адаптер | 1 | Дисплей (адрес 0x27) |
| MicroSD модуль | 1 | SPI (CS=D8) |
| Кнопки тактовые | 2 | MAIN (D3) и MENU (D4) |
| Резисторы 10 кОм | 2 | Делитель напряжения для A0 |
| Аккумулятор 18650 | 1 | 3.7V Li-Ion |

## Распиновка

| Модуль | Сигнал | GPIO | NodeMCU |
|--------|--------|------|---------|
| HX711 | DT | GPIO16 | D0 |
| HX711 | SCK | GPIO1 | TX |
| LCD I2C | SDA | GPIO4 | D2 |
| LCD I2C | SCL | GPIO5 | D1 |
| DS3231 RTC | SDA | GPIO4 | D2 |
| DS3231 RTC | SCL | GPIO5 | D1 |
| DS18B20 | Data | GPIO3 | RX (D9) |
| Кнопка MAIN | — | GPIO0 | D3 |
| Кнопка MENU | — | GPIO2 | D4 |
| Батарея | ADC | A0 | A0 |
| SD-карта | CS | GPIO15 | D8 |
| SD-карта | SCK | GPIO14 | D5 |
| SD-карта | MISO | GPIO12 | D6 |
| SD-карта | MOSI | GPIO13 | D7 |

> DS18B20 перенесён на GPIO3 (RX) — GPIO13 занят под SPI MOSI для SD-карты. Serial RX использовать нельзя пока подключён DS18B20.

## Установка

### Зависимости (Arduino Library Manager)

- `HX711` by bogde
- `LiquidCrystal I2C` by Frank de Brabander
- `OneWire` by Paul Stoffregen
- `DallasTemperature` by Miles Burton
- `RTClib` by Adafruit
- `ArduinoJson` v6 by Benoit Blanchon
- `SD` (встроенная)

### Сборка

1. Откройте `BeehiveScale/BeehiveScale.ino` в Arduino IDE
2. Выберите плату: **NodeMCU 1.0 (ESP-12E Module)**
3. Установите зависимости через Library Manager
4. Настройте `Connectivity.h`:
   - Wi-Fi SSID/пароль (или оставьте `WIFI_MODE_AP` для точки доступа)
   - Telegram Bot Token и Chat ID
   - ThingSpeak API Key
5. Загрузите на плату

## Веб-интерфейс

После загрузки подключитесь к Wi-Fi сети `BeehiveScale` (пароль: `12345678`) и откройте `http://192.168.4.1`.

Логин: `admin` / Пароль: `beehive`

### REST API

| Метод | Эндпоинт | Описание |
|-------|----------|----------|
| GET | `/api/data` | Все показания в JSON |
| POST | `/api/tare` | Тарировка весов |
| POST | `/api/save` | Сохранить текущий вес как эталон |
| POST | `/api/settings` | Изменить настройки (JSON) |
| POST | `/api/ntp` | Синхронизация времени NTP |
| GET | `/api/log` | Скачать лог CSV с SD-карты |
| POST | `/api/log/clear` | Очистить лог |
| POST | `/api/reboot` | Перезагрузить ESP |

### Пример `/api/data`

```json
{
  "weight": 25.40,
  "prev": 24.50,
  "temp": 22.5,
  "hum": 0.0,
  "rtcT": 22.1,
  "sensor": true,
  "wifi": true,
  "datetime": "2026-02-23 15:30:45",
  "uptime": "0d 00:03:12",
  "batV": 4.12,
  "batPct": 85,
  "heap": 45320
}
```

## Экраны LCD

| # | Экран | Описание |
|---|-------|----------|
| 0 | Вес | `Ves: 25.40*kg` (* = стабильно) |
| 1 | Дельта | `Delta: +0.90kg` / `Pred:24.50kg` |
| 2 | Батарея | `Bat: 4.12V  85%` |
| 3 | Температура | `T:22.5C R:22.1C` / `H: 65.2 %` |
| 4 | Дата/время | `2026-02-23` / `15:30:45` |
| 5 | Статус | `CF:2280` / `W:OK N:14` |
| 6 | Калибровка | `CF:2280.0` / `MAIN=Vojti` — вход через MAIN |
| 7 | Диагностика | `Diagnostika...` / `HX711... OK` — тест системы |

## Кнопки

| Кнопка | Нажатие | Действие |
|--------|---------|----------|
| MENU | Короткое | Следующий экран |
| MENU | Двойное (<500мс) | Отмена последней тарировки |
| MENU | Длинное (>1с) | Тарировка |
| MAIN | Короткое | Действие на текущем экране |
| MAIN | Длинное (>1с) | Войти/выйти из калибровки (экран #6) |

## EEPROM

| Адрес | Размер | Данные |
|-------|--------|--------|
| 0 | 4 | calibrationFactor (float) |
| 4 | 4 | offset (long) |
| 8 | 4 | weight (float) |
| 12 | 1 | magic (0xA5) |
| 13 | 4 | alertDelta (float) |
| 17 | 4 | calibWeight (float) |
| 21 | 4 | emaAlpha (float) |
| 25 | 1 | magic2 (0xA6) |
| 26 | 4 | prevOffset (long) |
| 30 | 4 | prevWeight (float) |

## Структура проекта

```
BeehiveScale/
  BeehiveScale.ino   — главный скетч (setup/loop, логика)
  Battery.h/.cpp     — мониторинг батареи
  Button.h/.cpp      — обработка кнопок (short/long/double press)
  Connectivity.h/.cpp — Wi-Fi, Telegram, ThingSpeak, NTP
  Display.h/.cpp     — LCD экраны
  Logger.h/.cpp      — CSV логирование на SD-карту
  Memory.h/.cpp      — EEPROM чтение/запись
  RTC_Module.h/.cpp  — DS3231 часы
  Scale.h/.cpp       — HX711 весовой модуль
  SleepManager.h/.cpp — Deep Sleep
  Temperature.h/.cpp — DS18B20
  WebServerModule.h/.cpp — веб-сервер и REST API
manual.html          — HTML инструкция
pages/               — отдельные страницы документации
```

## Лицензия

MIT
