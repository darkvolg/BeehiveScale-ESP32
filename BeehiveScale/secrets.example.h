#ifndef BEEHIVE_SECRETS_H
#define BEEHIVE_SECRETS_H

// Шаблон секретов. Скопируй в `secrets.h` и подставь реальные значения.
// Файл `secrets.h` находится в .gitignore — в репозиторий не попадает.
//
// Если secrets.h отсутствует — прошивка соберётся, но будет использовать
// плейсхолдеры из Connectivity.h (Wi-Fi не подключится, Telegram/ThingSpeak
// не заработают). Настройки можно ввести позже через Web UI.

// ─── Wi-Fi: домашний роутер (STA режим) ──────────────────────────────────
#define WIFI_SSID        "your_home_wifi"
#define WIFI_PASSWORD    "your_wifi_password"

// ─── Telegram (опционально) ──────────────────────────────────────────────
// #define TG_BOT_TOKEN  "1234567890:ABC..."
// #define TG_CHAT_ID    "123456789"

// ─── ThingSpeak (опционально) ────────────────────────────────────────────
// #define TS_API_KEY    "ABCDEFGHIJ123456"

#endif
