# BeehiveScale — Архитектура

## Модули (файлы в BeehiveScale/)
| Файл | Назначение |
|------|------------|
| `BeehiveScale.ino` | Главный скетч: setup/loop, SystemState, экраны LCD, кнопки |
| `Scale.h/.cpp` | HX711: init, check, read_weight |
| `Display.h/.cpp` | LCD 16x2 I2C: init, lcd_print_padded |
| `Button.h/.cpp` | Debounce кнопок: SHORT_PRESS, LONG_PRESS, DOUBLE_PRESS |
| `Memory.h/.cpp` | EEPROM: калибровка, offset, вес, web-настройки, prevOffset, Telegram, WiFi |
| `RTC_Module.h/.cpp` | DS3231 RTC: время, температура |
| `Temperature.h/.cpp` | DS18B20: температура |
| `Connectivity.h/.cpp` | WiFi (AP/STA), NTP, ThingSpeak, Telegram, LittleFS очередь |
| `SleepManager.h/.cpp` | Deep sleep, RTC memory persist |
| `WebServerModule.h/.cpp` | HTTP сервер: HTML UI, REST API, настройки, графики |
| `Battery.h/.cpp` | ADC чтение Li-Ion через делитель 2:1, EMA сглаживание |
| `Logger.h/.cpp` | CSV/JSON логирование на SD-карту, LittleFS fallback |

## Ключевые паттерны
- `SystemState sys` — глобальная структура состояния
- `WebData` — указатели на поля sys для веб-сервера (не копии!)
- `WebActions` — callback'и для действий из веб-интерфейса
- LCD экраны: switch/case по `sys.menuScreen` (0–7)
- Кнопки: BUTTON_PIN(GPIO0) — тарировка/калибровка, MENU_BTN_PIN(GPIO2) — переключение экранов + двойное нажатие для отмены тары
- EEPROM: magic bytes для валидации, commit() после записи
- EMA сглаживание для веса (настраиваемый alpha) и батареи (alpha=0.1)
- Spike-фильтр: отброс показаний при скачке > 5 кг

## Пины (NodeMCU ESP8266)
| Компонент | Сигнал | GPIO | Пин NodeMCU |
|-----------|--------|------|-------------|
| HX711 | DT | GPIO16 | D0 |
| HX711 | SCK | GPIO1 | TX |
| LCD I2C | SDA | GPIO4 | D2 |
| LCD I2C | SCL | GPIO5 | D1 |
| DS3231 RTC | SDA | GPIO4 | D2 |
| DS3231 RTC | SCL | GPIO5 | D1 |
| DS18B20 | Data | GPIO3 | D9 (RX) |
| Кнопка MAIN | — | GPIO0 | D3 |
| Кнопка MENU | — | GPIO2 | D4 |
| Батарея | ADC | A0 | A0 |
| SD-карта | CS | GPIO15 | D8 |
| SD-карта | SCK | GPIO14 | D5 |
| SD-карта | MISO | GPIO12 | D6 |
| SD-карта | MOSI | GPIO13 | D7 |

### Известные программные особенности
- **GPIO1 (TX)** используется как SCK для HX711 → после `Serial.end()` Serial-вывод корраптит сигнал весов. Debug сообщения на этапе после `Serial.end()` отключены.
- **GPIO3 (RX)** используется для DS18B20 → Serial-приём недоступен при подключённом термометре.
- **GPIO0/GPIO15/GPIO16** — boot-strap пины, требуют внешних pull-резисторов (см. схему).

## Web API
| Метод | Путь | Описание | Требует CSRF |
|-------|------|----------|--------------|
| GET | `/` | HTML страница (дашборд) | — |
| GET | `/api/data` | JSON со всеми показаниями + CSRF-token | — |
| GET | `/api/log` | JSON-лог (до 100 записей) | — |
| POST | `/api/tare` | Тарировка | ✓ |
| POST | `/api/save` | Сохранить эталон | ✓ |
| POST | `/api/settings` | Настройки (alertDelta, calibWeight, emaAlpha, sleep, backlight, AP pass) | ✓ |
| POST | `/api/ntp` | Синхронизация времени | ✓ |
| POST | `/api/reboot` | Перезагрузка | ✓ |
| GET | `/api/backup` | Скачать полный бэкап настроек (JSON) | — |
| POST | `/api/backup/restore` | Восстановить настройки из JSON бэкапа | ✓ |
| POST | `/api/auth/password` | Сменить admin/OTA пароль | ✓ |
| POST | `/api/log/clear` | Очистить лог на SD | ✓ |
| POST | `/api/wifi/settings` | Wi-Fi конфиг | ✓ |

Все POST защищены Basic Auth + CSRF токеном в заголовке `X-CSRF-Token` (получается в `/api/data`).
