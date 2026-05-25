#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 48
#define FW_VERSION_SUFFIX ""  // 5.0.48 — feature: галочка "Слать TG при wake без расписания" в Настройках веб-UI. EEPROM addr 361-362, default ON (backward compat). Если расписание задано — галочка игнорируется (расписание всегда шлёт TG). Если расписания нет + галочка снята → TG не шлётся в interval-mode (полезно при тестах с коротким Deep Sleep). Если галочка стоит → TG на каждом cold-boot wake.

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
