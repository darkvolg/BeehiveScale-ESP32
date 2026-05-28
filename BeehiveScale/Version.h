#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 59
#define FW_VERSION_SUFFIX ""  // 5.0.59 — fix: TG не приходил по расписанию при power-cut. Баг: tgReportPending ставился только при schedLog (точное совпадение минуты), но при power-cut каждый wake = cold boot (bootLog=true), а schedLog мог быть false из-за timing-проскока минуты (boot+WiFi+NTP ~5сек). Теперь: при bootLog + расписание задано → tgReportPending=true (ESP проснулась из-за DS3231 alarm = scheduled wake). Решает 'TG не приходит в 8:00/14:00'.

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
