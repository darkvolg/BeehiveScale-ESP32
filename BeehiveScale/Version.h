#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 68
#define FW_VERSION_SUFFIX ""  // 5.0.68 — FIX: дубли TG-отчётов при boot-loop (анти-дубль через EEPROM, механизм v5.0.44 наконец задействован) + диагностика: причина последнего сброса (POWERON/PANIC/BROWNOUT/WDT) в /api/data и в карточке «Статус системы».

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
