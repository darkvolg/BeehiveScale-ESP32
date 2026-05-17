# BeehiveScale ESP32 — Memory Index

**START HERE:** при заходе в проект сначала спроси LightRAG `lightrag_query("BeehiveScale ESP32 status recent changes tech stack", compact=true)`. MEMORY.md — только индекс файлов, не сама память.

## Project state (актуальное)
- [Текущая версия и фичи](project_current_state.md) — **v5.0.24**, что работает, железо в корпусе
- [Session 2026-05-13 → 2026-05-17](project_session_2026-05-17.md) — большой апгрейд v5.0.18→v5.0.24, фикс wake-up WiFi crash, калибровка батареи, алерты, ждём детали
- [Web-архив + PWA (v5.0.16 + v5.0.17)](project_v5_archive_pwa.md) — детали реализации архивной вкладки и PWA
- [EEPROM layout](reference_eeprom_layout.md) — карта адресов 0..383, что где лежит (+ калибровка батареи 336-340, алерты 342-356)

## Backlog / открытые задачи
- [Feature backlog](backlog_features.md) — обсуждённое но не сделанное (графики в архиве, GSM, HTTPS, Telegram sendDocument)
- [Action Plan ESP32](../ACTION_PLAN_ESP32.md) — план порта с ESP8266 на ESP32 (P0-P4)

## User & feedback
- [User profile](user_profile.md) — Геннадий, пчеловод, удалённая пасека, основное устройство — телефон
- [Feedback: compact session save](feedback_compact_session.md) — при /compact обязательно сохранить изменения + текущий план в память

## Architecture quick-reference
- [Web UI architecture](reference_web_architecture.md) — SPA single PROGMEM HTML, _WebChunkStream, как добавлять страницы
- [CSV format](reference_csv_format.md) — формат лога, datetime, BOM, separator
