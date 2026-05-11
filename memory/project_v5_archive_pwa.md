---
name: project_v5_archive_pwa
description: Реализация Архивной вкладки + PWA + Last-Visit в v5.0.16/5.0.17 (2026-05-11)
type: project
---

# Архивная вкладка + PWA (v5.0.16 + v5.0.17)

**Why:** Геннадий уезжает на пасеку с телефоном на неделю+, весы пишут лог автономно. Telegram-отчёты приходят только когда есть Wi-Fi. По приезду нужно удобно посмотреть всю неделю на телефоне.
**How to apply:** если задача связана с архивом, PWA, экспортом CSV, last-visit timestamp — детали тут. Базовая логика — стриминг JSON/CSV с фильтром по диапазону дат.

## Новые endpoints (WebServerModule.cpp)
- `GET /archive` — alias на PAGE_HTML (SPA откроет вкладку Архив через nav)
- `GET /api/period?from=YYYY-MM-DD&to=YYYY-MM-DD` — JSON-стрим записей за диапазон через `_WebChunkStream`
- `GET /api/log?from=&to=` — CSV-стрим за диапазон. Старый `?date=YYYY-MM-DD` совместим. Если задан from/to — приоритет у диапазона.
- `GET /api/last-visit` — JSON `{lastVisit: unix, serverNow: unix}`. Логика обновления: первый вызов за boot возвращает СТАРОЕ значение из EEPROM, обновляет на now если разрыв >1ч.
- `GET /manifest.json` — PWA манифест (без auth)
- `GET /sw.js` — service worker (без auth)
- `GET /icon.svg` — иконка пчелы (без auth)

## Logger функции (Logger.h/cpp)
- `log_stream_csv_range(Stream &out, const String &from, const String &to)` — стрим CSV в Stream за диапазон
- `log_stream_period_json(Stream &out, const String &from, const String &to)` — JSON стрим `[{dt,w,t,b},...]`
- Внутри: `_stream_filter_range()` единственный обход файла + `_date_to_yyyymmdd()` конвертер для лекс-сравнения (универсально работает с YYYY-MM-DD и DD.MM.YYYY)

## EEPROM (v5.0.17)
- `EEPROM_ADDR_LAST_VISIT_MAGIC = 331` (byte, magic 0xAB)
- `EEPROM_ADDR_LAST_VISIT = 332` (uint32_t, Unix epoch)
- В зарезервированном блоке 331..383 (свободно до 336)
- `save_last_visit(uint32_t)` / `load_last_visit()` в Memory.cpp
- Валидация: ts < 1546300800 (2019-01-01) = мусор (RTC не настроен)

## UI Архива (PAGE_HTML)
- 8-я вкладка после Главной: `<button onclick="nav('archive')">📅 Архив</button>`
- `nav()` idx map: `{main:0,archive:1,chart:2,wifi:3,settings:4,calib:5,tg:6,api:7}` — при добавлении новых вкладок обновить ВСЕ индексы
- Секция `<div class="section" id="sec-archive">`
- Пресеты: Сегодня (0), 3 дня (2), Неделя (6), Месяц (29), Всё (-1), **С прошлого визита** (особая, появляется только если /api/last-visit вернул >0)
- JS функции: `archInit()`, `archLoad()`, `archGroup()`, `archPreset()`, `archSinceLastVisit()`, `archDownloadRange()`
- Цвета маркеров: 🔴 падение > alert_delta (переиспользует существующую настройку!), 🟢 прирост >0.1, 🟡 ±0.1, ⚪ нет данных предыдущего дня
- Группировка: дни сверху вниз (новые первыми), внутри месячные заголовки

## PWA артефакты
- Все встроены в PROGMEM, отдаются через `_srv.send_P()` — нет необходимости в LittleFS файлах
- `/manifest.json` ~250 байт: name, short_name, display=standalone, theme_color #f5a623, icons → /icon.svg
- `/sw.js` ~700 байт: cache 'beehive-v1' + ['/', '/manifest.json', '/icon.svg'], API запросы пропускает (всегда сеть), остальное network-first с fallback на cache
- `/icon.svg` ~500 байт: пчела (амбер круг + 3 чёрные полоски + 2 белых крыла) на тёмном фоне 192x192
- В `<head>` PAGE_HTML добавлены: `<link rel="manifest">`, `<meta name="theme-color">`, `<meta apple-mobile-web-app-*>`, `<link rel="apple-touch-icon">`
- Регистрация SW: `navigator.serviceWorker.register('/sw.js')` в `window.onload`

## Известные ограничения / TODO
- График Chart.js в архиве НЕ реализован. График живёт на отдельной вкладке /chart. Можно добавить общий график за выбранный период.
- HTTPS не настроен → PWA на iOS работает частично (установка ок, push-уведомлений нет). Самоподписанный сертификат на ESP32 = warning. Можно добавить если нужно.
- Кнопка "Отправить лог в Telegram документом" обсуждалась — не реализована.
- Отдельный порог для архива (anomaly_threshold) НЕ вводился — переиспользует alert_delta. По дизайну.
