---
name: reference_csv_format
description: Формат CSV-лога BeehiveScale, datetime, кодировка
type: reference
---

# CSV формат лога BeehiveScale

## Структура файла
- Путь: `/log.csv` (LittleFS, внутренний flash)
- Заголовок: `\xEF\xBB\xBF` (UTF-8 BOM) + `datetime;weight_kg;temp_c;humidity_pct;bat_v\n`
- Разделитель полей: **`;`** (точка с запятой, для русского Excel)
- Десятичный разделитель: **`,`** (запятая, тоже для русского Excel)

## Формат полей
- `datetime` — `DD.MM.YYYY HH:MM:SS` (например `11.05.2026 21:00:00`)
- `weight_kg` — `%.2f` (например `68,52`)
- `temp_c` — `%.1f` (например `22,3`). Значение `-99,0` = ошибка датчика DS18B20.
- `humidity_pct` — `%.1f`. Сейчас всегда `0,0` (датчик влажности не подключён в v4.2+).
- `bat_v` — `%.2f` (например `4,12`)

## Ротация
- При размере >100 КБ (`LOG_MAX_SIZE`) текущий файл переименовывается в `/log_YYMMDD_HHMM.csv`, создаётся новый `/log.csv` с заголовком
- Старые архивы (`log_*.csv`) удаляются только через `/api/log/clear` (POST)

## Чтение / стриминг
- `log_stream_csv_date(out, "DD.MM.YYYY")` или `"YYYY-MM-DD"` — за конкретную дату
- `log_stream_csv_range(out, from, to)` — за диапазон (v5.0.16). from/to принимают оба формата.
- `log_stream_period_json(out, from, to)` — JSON `[{dt,w,t,b},...]` за диапазон (v5.0.16)
- `log_stream_json(out, maxRows)` — последние N записей в JSON (для графика)
- `log_day_stat("DD.MM.YYYY")` — min/max веса и температуры за день

## Лимиты
- Размер char-буфера на строку: 128 байт (Logger.cpp `char buf[128]`)
- Максимальная длина одной CSV-строки = ~80 байт, лимит с запасом
- На ESP8266 (legacy) `log_stream_json` ограничен 50 строк, на ESP32 — 200
- Все парсеры на char-буферах (не String) — защита от heap-фрагментации

## Безопасность
- Перед записью `_validate_row()`: вес в `-5..500`, температура `-50..100`, влажность `0..100`, батарея `0..6В`
- Excel formula injection: datetime начинающиеся с `=`, `+`, `-`, `@` префиксуются апострофом (`'`)
- Запрещены символы в datetime: `;`, `\r`, `\n`, `\t`, `<`, `>`, `"`, `\\`, control bytes
