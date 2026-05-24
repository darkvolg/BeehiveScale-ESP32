#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 45
#define FW_VERSION_SUFFIX ""  // 5.0.45 — refactor: упрощена логика TG отправки. Расписание = TG на scheduled wake; нет расписания = TG на каждом cold-boot wake. Sleep current ~14 мА (HOLD через Alarm1 PerSecond не отпускается полностью, ESP идёт через esp_deep_sleep_start fallback). Работающая stable версия — рабочий TG, надёжный auto-wake.

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
