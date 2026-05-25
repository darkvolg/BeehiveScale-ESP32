#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 47
#define FW_VERSION_SUFFIX ""  // 5.0.47 — fix: восстановлен полный sleep_enter с DS3231 Alarm2 wake. v5.0.46 имел fix в RTC_Module.cpp (Alarm1 mode переключение) но НЕ вызывал rtc_set_alarm_in_seconds из sleep_enter (был оставлен v5.0.39 timer-only compromise). Теперь: sleep_enter правильно программирует Alarm2 wake + disable Alarm1 HOLD → MOSFET закрывается → sleep current 2 мА. Fallback esp_deep_sleep остаётся если hardware не сработал.

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
