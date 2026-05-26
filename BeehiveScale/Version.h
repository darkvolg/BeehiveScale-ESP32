#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 56
#define FW_VERSION_SUFFIX ""  // 5.0.56 — feature: периодический MQTT publish каждые 60 сек когда ESP active. Раньше MQTT обновлялся только в log_append (раз в 30 мин) → юзер видел старые значения в HA при калибровке/тарировании. Теперь real-time обновления когда ESP не в sleep. В deep sleep — без изменений (ESP off, не publish).

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
