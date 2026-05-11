---
name: reference_web_architecture
description: Архитектура веб-интерфейса BeehiveScale — SPA, PROGMEM, как добавлять страницы
type: reference
---

# Web-интерфейс BeehiveScale — архитектура

## Главное
- **Весь UI = одна PROGMEM HTML страница** (`PAGE_HTML[] PROGMEM` в `WebServerModule.cpp` ~70 КБ)
- SPA-навигация: все секции `<div class="section" id="sec-X">` в одном документе, переключаются через `nav('X')` JS-функцией (display:none/active)
- Несколько URL роутов = алиасы на одну и ту же страницу: `/`, `/chart`, `/wifi`, `/archive` все вызывают `_sendProgmemChunked(PAGE_HTML)`
- Передача страницы — chunked transfer чанками 4096 байт через `_sendProgmemChunked()` (быстрее в 8 раз чем 512-байтные)

## Как добавить новую страницу/вкладку
1. В `<div class="tabs">` добавить кнопку `<button class="tab" onclick="nav('NAME')">…</button>` — порядок важен!
2. В `nav()` JS обновить idx map: `{main:0, archive:1, ...}` (порядок ДОЛЖЕН совпадать с порядком кнопок)
3. В `nav()` добавить `if (id==='NAME') initFunc();` если нужна инициализация при открытии
4. Добавить `<div class="section" id="sec-NAME">…</div>` где-нибудь между другими секциями
5. (Опционально) Если хотим прямой URL `/NAME` — добавить `_handleNAME()` который вызывает `_sendProgmemChunked(PAGE_HTML)`, плюс регистрация в `webserver_init()`

## API endpoints — паттерн
- GET-эндпоинты: `_keepalive()` (не сбрасывать sleep таймер) + `_rate_limit()` для дорогих
- POST-эндпоинты: `_auth()` + `_csrf_check()` + `_activity()` (сбросить sleep)
- Ответ: `_sendJson(bool ok, String msg)` для маленьких; `_WebChunkStream` для больших
- Sanitize дат: только цифры/`-`/`.`, длина ≤10 (защита от header injection)

## `_WebChunkStream` — для стриминга больших ответов
- Вынесен на файловый уровень (v5.0.16) — переиспользуется
- Использовать: `_srv.setContentLength(CONTENT_LENGTH_UNKNOWN); _srv.send(200, "type", ""); _WebChunkStream cs(_srv); some_stream_fn(cs); cs.flush();`
- Буфер 256 байт, flush'ит каждые 512 + yield()

## CSRF / Auth
- Basic Auth: admin/beehive по дефолту (или из EEPROM)
- CSRF token: 32 hex char, ротируется при reboot, обязателен для POST в заголовке `X-CSRF-Token`
- Клиент получает токен через `/api/data` (там есть поле в JSON, но не в данной версии — токен через `_csrf_init()` initialized in `_handleData`)

## PWA endpoints (v5.0.16, без auth)
- `/manifest.json` — отдаётся `_handleManifest`, PROGMEM строка
- `/sw.js` — `_handleServiceWorker`, no-cache
- `/icon.svg` — `_handleIcon`, max-age=30 дней
