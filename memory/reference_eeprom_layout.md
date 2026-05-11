---
name: reference_eeprom_layout
description: Карта EEPROM 0..383 для BeehiveScale-ESP32 — что где лежит
type: reference
---

# EEPROM layout BeehiveScale (всего 384 байта)

Источник истины: `BeehiveScale/Memory.h`. Эта карта — для быстрого ориентирования и поиска свободных адресов.

| Addr | Size | Поле | Magic |
|------|------|------|-------|
| 0 | float | calibrationFactor | — |
| 4 | long | offset | — |
| 8 | float | weight (lastSavedWeight) | — |
| 12 | byte | MAGIC_VALUE (0xA5) | да |
| 13 | float | alertDelta | — |
| 17 | float | calibWeight | — |
| 21 | float | emaAlpha | — |
| 25 | byte | MAGIC2 (0xA6) | да |
| 26 | long | prev_offset | — |
| 30 | float | prevWeight | — |
| 34 | uint32 | sleepSec | — |
| 38 | uint16 | lcdBlSec | — |
| 40 | char[24] | apPass | — |
| 64 | byte | MAGIC3 (0xA7) | да |
| 65 | byte | TG_MAGIC (0xB1) | да |
| 66 | char[50] | tgToken | — |
| 116 | char[16] | tgChatId | — |
| 133 | byte | WIFI_MAGIC (0xC1) | да |
| 134 | byte | wifiMode (0=AP, 1=STA) | — |
| 135 | char[33] | wifiSsid | — |
| 168 | char[33] | wifiStaPass | — |
| 201 | byte | SCHED_MAGIC (0xD1) | да |
| 202 | byte | schedCount (0..8) | — |
| 203 | 8×uint16 | schedTimes (16 байт) | — |
| 219 | uint32 | tgReportIntervalMin | — |
| 223 | byte | TG_RPT_MAGIC (0xE1) | да |
| 224 | byte | CRED_MAGIC (0xF2) | да |
| 225 | char[24] | adminUser | — |
| 249 | char[32] | adminPass | — |
| 281 | char[32] | otaPass | — |
| 313 | uint32 | prev_weight_date (Unix ts) | — |
| 317 | uint16 | autoSleepSec | — |
| 319 | byte | AUTOSLEEP_MAGIC (0xA8) | да |
| 320 | byte | REPORT_MAGIC (0xA9) | да |
| 321 | float | lastReportWeight | — |
| 325 | bool | hasLastReport | — |
| 326 | byte | TEMP_MAGIC (0xAA) | да |
| 327 | float | lastTempC | — |
| **331** | **byte** | **LAST_VISIT_MAGIC (0xAB)** | **да** |
| **332** | **uint32** | **last_visit (Unix ts) — v5.0.17** | — |
| 336..383 | — | **свободно** | — |

**Total: 384 байта.** EEPROM_SIZE определён в Memory.h как 384.

## Правила для новых полей
- Добавлять в адреса 336..383
- Каждое логическое поле = свой magic byte (защита от мусора)
- Magic-значения уникальны (см. колонку выше) — следующий свободный 0xAC
- Валидация при чтении: невалидное значение → возвращаем дефолт
- Запись через `EEPROM.put()` + `EEPROM.commit()` (на ESP32 эмулируется через NVS)
