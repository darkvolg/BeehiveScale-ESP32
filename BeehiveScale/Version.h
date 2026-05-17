#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 23
#define FW_VERSION_SUFFIX ""  // 5.0.23 — fix: 1) ntp_loop() в main цикле тоже отключён (вызывал NTP несмотря на отключение в setup, тот же баг Core 3.0.7). 2) WiFi.setSleep(false) вернул — modem-sleep блокировал webserver запросы, страница открывалась но /api/data зависал.

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
