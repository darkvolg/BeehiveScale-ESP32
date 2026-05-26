#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 57
#define FW_VERSION_SUFFIX ""  // 5.0.57 — feature: настраиваемый интервал MQTT live publish в UI. Поле 'Интервал live publish (сек)' в MQTT секции, range 10-3600, default 60. EEPROM addr 503. Юзер может выбрать 30 сек для быстрого мониторинга или 600 сек для экономии трафика.

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
