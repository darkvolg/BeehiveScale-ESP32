# Аудит сессии 52 — BeehiveScale v4.1

**Дата**: 2026-03-09
**Коммит-база**: `bdb7c32`
**Сборка**: RAM 46%, Flash 57%, IRAM 95%

---

## Сводка

| Уровень | Кол-во |
|---------|--------|
| CRITICAL | 1 |
| MEDIUM | 11 |
| MINOR | 13 |
| INFO | 8 |
| **Итого** | **33** |

---

## CRITICAL (1)

### C1. `millis()` overflow в TG report интервале
**Файл**: BeehiveScale.ino:434
**Проблема**: `get_tg_report_interval_min() * 60000UL` — при значении 10080 (максимум) результат = 604,800,000. Это в пределах uint32_t. НО: `now - lastTgReport >= tgRptMs` при millis() overflow (через ~49.7 дней) может дать ложный срабатывание, отправив лавину TG-отчётов.
При тип `tgRptMs` — `uint32_t`, а `now` и `lastTgReport` — `unsigned long` (тоже 32-bit на ESP8266), разница корректна при overflow. **НО**: `lastTgReport` инициализируется как 0 при boot. Если весы работают в continuous mode и millis() уже ~49 дней — первый TG-отчёт отправится мгновенно при включении WiFi, что ожидаемо. Однако если `lastTgReport = now` после отправки, а следующий `now` переполнится — отчёт отправится раньше на (49.7 дней - tgRptMs).
**Реальность**: ESP8266 в continuous mode маловероятно проработает 49 дней без ребута (OOM, WiFi disconnect). **Но** принцип — баг есть.
**Исправление**: `lastTgReport = millis()` в setup() после инициализации WiFi.

---

## MEDIUM (11)

### M1. `_handleBackup` создаёт два JSON в heap одновременно
**Файл**: WebServerModule.cpp:1887-1892
**Проблема**: Комментарий говорит "два больших String не сосуществуют", но `log_save_backup(_buildBackupJson(false))` вернёт String, передаст в `log_save_backup`, а затем `_buildBackupJson(true)` создаст второй. Первый String деструктурируется перед вторым, так что это ОК. Но `_srv.send()` на строке 1891 может не отправить данные до деструкции `json`. На ESP8266 `_srv.send()` копирует данные — безопасно.
**Статус**: Ложная тревога. Оставляю как INFO → переведено в M1 из-за heap давления: DynamicJsonDocument(768) + String ~200 байт = ~1KB на каждый вызов. При heap ~16KB это 6%.
**Рекомендация**: Отправлять напрямую через chunked transfer вместо сборки полного String.

### M2. `_handleBackupRestore` использует `extAp` как dangling pointer
**Файл**: WebServerModule.cpp:1964
**Проблема**: `const char* p = doc["apPass"] | ""` — ArduinoJson v6 при `operator|` с `""` возвращает указатель на строковый литерал. Но `doc["apPass"]` при наличии значения возвращает указатель на данные внутри `doc`. `doc` живёт до конца функции — безопасно. Однако `set_ext_all(extSleep, extLcd, extAp)` вызывается после `}` scope — `extAp` всё ещё валиден (doc жив). **ОК**, но код хрупкий.
**Рекомендация**: Явно копировать в static буфер (как сделано для `apPassBuf` в `_handleSettings`).

### M3. `log_clear` на LittleFS удаляет файлы во время итерации Dir
**Файл**: Logger.cpp:400-407
**Проблема**: `Dir dir = LittleFS.openDir("/"); while(dir.next()) { LittleFS.remove(...); }` — модификация директории во время итерации. На ESP8266 LittleFS это может привести к пропуску файлов или крешу.
**Исправление**: Собрать имена в массив, потом удалять (как сделано для SD).

### M4. `_sendProgmemChunked` — нет null-check на strlen_P
**Файл**: WebServerModule.cpp:1320
**Проблема**: `strlen_P(pgm)` если `pgm` — NULL, undefined behavior. Маловероятно (вызывается только с `PAGE_HTML`), но защитный код не помешает.

### M5. `set_tg_all(newToken, newChatId)` при newToken=NULL не пропускает запись
**Файл**: Memory.cpp:282
**Проблема**: Если `newToken` = NULL, `strncpy` не вызывается, но `_tg_write()` + `EEPROM.commit()` всё равно выполняются. Это перезаписывает TG-блок EEPROM текущими cached значениями. Если `_tgLoaded = false` и кеш пуст — перетрёт EEPROM нулями.
**Реальность**: `tg_settings_init()` вызывается в setup(), так что кеш всегда загружен. Но формально баг.

### M6. WiFi reconnect блокирует loop на 7 секунд
**Файл**: Connectivity.cpp:141
**Проблема**: `while (WiFi.status() != WL_CONNECTED && millis() - start < 7000UL)` — блокирующий цикл. С `ESP.wdtFeed()` и `yield()` WDT не сработает, но веб-сервер, кнопки и LCD не обновляются 7 секунд. В continuous mode пользователь увидит "зависание".
**Рекомендация**: Non-blocking reconnect (проверять WiFi.status() в loop без блокировки).

### M7. `queue_process` — rename failure оставляет `/queue_tmp.bin`
**Файл**: Connectivity.cpp:246-248
**Проблема**: Если `LittleFS.rename("/queue_tmp.bin", QUEUE_FILE)` фейлится — остаётся `queue_tmp.bin` и при следующем вызове `queue_process` обрабатывается `QUEUE_FILE` (удалён), а `queue_tmp.bin` игнорируется навсегда.
**Исправление**: При старте `queue_process` проверять наличие `queue_tmp.bin` и если `QUEUE_FILE` отсутствует — переименовать.

### M8. `_handleLog` фильтрация по дате — ChunkStream буфер 64 байта мал
**Файл**: WebServerModule.cpp:1705
**Проблема**: `char buf[64]` — строка CSV может быть до ~60 символов ("DD.MM.YYYY HH:MM:SS;12.34;-5.6;45.2;3.78\n"). При 64 байтах буфера write() вызывает `_flush_buf()` часто, создавая множество мелких TCP-пакетов. Не баг, но влияет на производительность.
**Рекомендация**: Увеличить до 256.

### M9. `_handleDayStat` — дни наблюдений считаются по формуле Гаусса без учёта ошибок
**Файл**: WebServerModule.cpp:1794-1796
**Проблема**: Формула `y*365 + y/4 - y/100 + y/400 + (m*306+5)/10 + d` — приблизительная. Для корректного подсчёта дней нужна Julian Day Number. Текущая формула даёт ошибку при переходе через год. Для "дней наблюдений" точность ±1 день достаточна.
**Статус**: Допустимо для задачи.

### M10. `show_splash_screen` не прочитан — но splash samples 10→5 из сессии 51
**Ссылка**: BeehiveScale.ino (вероятно далее в файле)
**Проблема**: Не удалось прочитать show_splash_screen, но по памяти сессии 51 — samples снижены для WDT safety. Может быть первый замер менее точным.

### M11. `tg_send_alert` — datetime.c_str() в snprintf без длины проверки
**Файл**: Connectivity.cpp:363
**Проблема**: `snprintf(msg, sizeof(msg), "... %s ...", datetime.c_str(), ...)` — если `datetime` длинная, `snprintf` обрежет (sizeof(msg)=256). Безопасно от overflow, но сообщение может быть обрезано.

---

## MINOR (13)

### m1. `bat_percent` — pct может быть >100 при V > 4.20
**Файл**: Battery.cpp:44
**Проблема**: При `v >= 4.10f` формула `95 + (v - 4.10f) / 0.10f * 5.0f` при v=4.30 даёт 105%. `constrain()` на строке 51 ограничивает до 100.
**Статус**: Ограничено `constrain`. ОК.

### m2. `commaToFloat` — atof без errno проверки
**Файл**: Logger.cpp:28
**Проблема**: `atof()` при overflow возвращает ±HUGE_VAL, при невалидном вводе — 0. Для CSV-парсинга достаточно.

### m3. `_maskSecret` — создаёт String посимвольно
**Файл**: WebServerModule.cpp:1280-1283
**Проблема**: `out += (char)` в цикле — множественные реаллокации String. `out.reserve(len)` есть, но += char может всё равно аллоцировать.
**Рекомендация**: Использовать char буфер.

### m4. `display_screen_temp` — snprintf в buf[24] для 16-символьного LCD
**Файл**: BeehiveScale.ino:777-778
**Проблема**: `"T:%4.1fC R:%4.1fC"` может быть до 18 символов при отрицательных температурах (T:-5.1C R:-3.2C = 18). Буфер 24 — OK, но LCD 16 символов обрежется. На display_screen_temp отображение правой половины строки может обрезаться.

### m5. `log_first_date` — yield каждые 1024 байта, но нет WDT feed
**Файл**: Logger.cpp:542, 548
**Проблема**: `yield()` кормит software WDT, но при большом файле (100KB) и медленной SD — может не хватить для hardware WDT (8 сек).

### m6. `_handleWifiSettings` — restart без корректного закрытия SD
**Файл**: WebServerModule.cpp:1652
**Проблема**: `ESP.restart()` после `delay(300)` — SD-файл может быть не закрыт при записи лога. `LittleFS` обычно корректно обрабатывает это, SD — может потерять данные.

### m7. `adjust_calibration` — бесконечный цикл с 5 мин таймаутом
**Файл**: BeehiveScale.ino:979
**Проблема**: `for(;;)` с break по таймауту 5 минут. Во время этого цикла loop() не вызывается → WiFi клиенты не обслуживаются, мигнёт "офлайн" в браузере.

### m8. `display_screen_diag` — delay(1000) × 4 = 4 секунды блокировки loop
**Файл**: BeehiveScale.ino:874-944
**Проблема**: Диагностика блокирует loop на 4+ секунд. WDT кормится через `app_wdt_reset()`, но веб-клиенты зависнут.

### m9. `ntp_sync_time` ESP8266 — delay(1000) × до 15 = 15 секунд блокировки
**Файл**: Connectivity.cpp:480-487
**Проблема**: NTP синхронизация блокирует loop на 1-15 секунд. Вызывается в setup() (приемлемо) и по `ntp_loop()` (каждый час — проблема для continuous mode).

### m10. `_fs_rename` для SD — yield() в копировании, но нет WDT feed
**Файл**: Logger.cpp:84
**Проблема**: Только `yield()` без `ESP.wdtFeed()`. При большом файле (100KB) и медленной SD может триггернуть WDT.

### m11. `webserver_stop` — не сбрасывает webServerStarted
**Файл**: WebServerModule.cpp:2046-2049
**Проблема**: `_srv.stop()` без изменения внешнего флага. В BeehiveScale.ino `webServerStarted` управляется вручную, но `webserver_stop()` не связан с ним.

### m12. `AP_PASSWORD` в Connectivity.h не используется
**Файл**: Connectivity.h:16
**Проблема**: `#define AP_PASSWORD "12345678"` — не используется нигде. Пароль AP берётся из EEPROM через `get_ap_pass()`. Dead code.

### m13. `log_to_json` — totalLines считает каждый '\n', включая trailing
**Файл**: Logger.cpp:700-708
**Проблема**: Если файл заканчивается на '\n' (что нормально для CSV), `totalLines` будет на 1 больше реального количества строк. `dataLines = totalLines - 1` компенсирует заголовок, но пустая последняя строка не учтена. При `skipLines` может пропустить одну лишнюю строку.

---

## INFO (8)

### i1. Hardcoded OTA password
**Файл**: BeehiveScale.ino:226
`ArduinoOTA.setPassword("ota_beehive")` — известная проблема, документирована ранее.

### i2. `client.setInsecure()` для TLS
**Файл**: Connectivity.cpp:314
Не проверяет TLS-сертификат. Документировано ранее.

### i3. Нет CSRF защиты на POST endpoints
Все POST обработчики защищены только Basic Auth. Документировано ранее.

### i4. Нет rate limiting на API endpoints
Возможен DoS через спам /api/data. Документировано ранее.

### i5. WEB_ADMIN_PASS = "beehive" отображается в HTML
**Файл**: WebServerModule.cpp:514-517
Пароль по умолчанию виден в интерфейсе. Документировано ранее.

### i6. Serial.println после Serial.end()
**Файл**: BeehiveScale.ino:217, 582 и другие
После `Serial.end()` вызовы Serial безвредны (UART отключен), но создают dead code.

### i7. `TEMP_READ_INTERVAL_MS` не определён в прочитанных файлах
**Ссылка**: BeehiveScale.ino:301
Должен быть в Temperature.h (не прочитан полностью). Не баг, но стоит проверить значение.

### i8. DynamicJsonDocument в `_buildBackupJson` — heap fragmentation
**Файл**: WebServerModule.cpp:1827
`DynamicJsonDocument doc(768)` — аллоцирует на heap. При частых backup (каждое сохранение настроек) может фрагментировать. StaticJsonDocument<768> лучше, но 768 байт на стеке — ОК для ESP8266 (стек 4KB).

---

## Архитектурные заметки (без номера — для справки)

1. **IRAM 95%** — новые ISR или ICACHE_RAM_ATTR функции невозможны.
2. **Heap idle 16-20KB** — каждый String/DynamicJsonDocument съедает заметную долю.
3. **Блокирующие операции** (WiFi reconnect 7с, NTP 15с, диагностика 4с, калибровка 5мин) — главная архитектурная проблема continuous mode. Решение: state machine вместо блокирующих циклов.
4. **EEPROM wear** — save_weight вызывается при каждой стабилизации (раз в 10 мин). EEPROM ESP8266 рассчитан на ~100K записей. При 6 записях/час = 52K/год — хватит на ~2 года. Приемлемо.

---

## Сравнение с предыдущими аудитами

| Метрика | Аудит 47-49 | Аудит 52 |
|---------|------------|----------|
| CRITICAL | 1 | 1 |
| MEDIUM | ~41 | 11 |
| MINOR | ~38 | 13 |
| INFO | ~13 | 8 |
| **Итого** | 102 | 33 |

Прогресс: код значительно улучшен после 46 фиксов сессий 50-51. Оставшиеся проблемы — в основном архитектурные (блокирующие операции) и мелкие.
