# BeehiveScale ESP32 — Memory Index

**START HERE:** при заходе в проект сначала спроси LightRAG `lightrag_query("BeehiveScale ESP32 status recent changes tech stack", compact=true)`. MEMORY.md — только индекс файлов, не сама память.

## Project state (актуальное)
- [⭐ Session 2026-05-22 ТЕКУЩАЯ](project_session_2026-05-22.md) — **v5.0.30→33 hardware power-cut установлен**, auto-cycle 14+ wake OK, GPIO25 застрял 0V в sleep (utечка), v5.0.33 жд прошивки
- [Session 2026-05-18 → 2026-05-21](project_session_2026-05-20.md) — v5.0.30 hardware power-cut, инцидент со сгоревшей старой ESP, переход на новую плату
- [Session 2026-05-18 (старая)](project_session_2026-05-18.md) — баг wake-from-deep-sleep WiFi, v5.0.27-29 итерации
- [Session 2026-05-13 → 2026-05-17](project_session_2026-05-17.md) — большой апгрейд v5.0.18→v5.0.24, фикс wake-up WiFi crash, калибровка батареи, алерты, ждём детали
- [Текущая версия и фичи](project_current_state.md) — устаревший (v5.0.24), реальная версия v5.0.33
- [Web-архив + PWA (v5.0.16 + v5.0.17)](project_v5_archive_pwa.md) — детали реализации архивной вкладки и PWA
- [EEPROM layout](reference_eeprom_layout.md) — карта адресов 0..383, что где лежит (+ калибровка батареи 336-340, алерты 342-356)
- [⭐ Hardware Power-Cut инструкция](../hardware/POWER-CUT-INSTRUCTIONS.html) — HTML инструкция пайки доплаты AO3401 + DS3231 alarm

## Backlog / открытые задачи
- [Feature backlog](backlog_features.md) — обсуждённое но не сделанное (графики в архиве, GSM, HTTPS, Telegram sendDocument)
- [Action Plan ESP32](../ACTION_PLAN_ESP32.md) — план порта с ESP8266 на ESP32 (P0-P4)

## User & feedback
- [User profile](user_profile.md) — Геннадий, пчеловод, удалённая пасека, основное устройство — телефон
- [Feedback: compact session save](feedback_compact_session.md) — при /compact обязательно сохранить изменения + текущий план в память

## Architecture quick-reference
- [Web UI architecture](reference_web_architecture.md) — SPA single PROGMEM HTML, _WebChunkStream, как добавлять страницы
- [CSV format](reference_csv_format.md) — формат лога, datetime, BOM, separator
