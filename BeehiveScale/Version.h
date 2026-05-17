#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 22
#define FW_VERSION_SUFFIX ""  // 5.0.22 — fix: отключён NTP sync на boot (вызывал Guru Meditation InstrFetchProhibited на ESP32 Core 3.0.7). DS3231 RTC даёт точное время без NTP. Ручная синхронизация остаётся через сайт /api/ntp если понадобится.

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
