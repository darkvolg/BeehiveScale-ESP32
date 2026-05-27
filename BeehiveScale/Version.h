#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 58
#define FW_VERSION_SUFFIX ""  // 5.0.58 — feature: TG retry с задержанным сообщением. При fail tg_send_report() — сохраняется в LittleFS /tg_pending.bin (вес, темп, батарея, время, retry counter). При next wake (после WiFi connect) — пробует отправить с пометкой 'Поздний отчёт от HH:MM'. Max 3 retry → дроп. Решает проблему когда Cloudflare/Telegram временно недоступен. Новый файл TgPending.cpp/.h.

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
