#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 55
#define FW_VERSION_SUFFIX ""  // 5.0.55 — fix: MQTT temperature force-read + fallback. v5.0.54 не публиковал температуру в MQTT при первом wake — DS18B20 async-read не успевал до log_append → tempC=-127 → skip. Теперь: при mqttTempC<=-90 делаем temp_force_read() (~750мс), если всё ещё invalid → fallback на persist.lastTempC. Аналогично логике TG отчёта.

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
