#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 60
#define FW_VERSION_SUFFIX ""  // 5.0.60 — fix: двойная запись в архив (schedLog HH:00 temp=0 + bootLog HH:02). Баг: schedLog срабатывал по минуте но не ставил _bootLogDone → bootLog создавал вторую запись позже. Плюс первая запись с temp=0 (DS18B20 async не успел). Фикс: (1) schedLog → _bootLogDone=true (нет дубликата), (2) force temp read перед log_append если tempData.valid=false. Теперь ОДНА запись с корректной температурой.

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
