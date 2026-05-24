#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 46
#define FW_VERSION_SUFFIX ""  // 5.0.46 — fix: 14мА → 2мА в sleep. Проблема: в sleep_enter после disableAlarm(1) PerSecond mode оставался активен → A1F set каждую секунду → SQW LOW (на некоторых DS3231 clones даже при A1IE=0) → MOSFET ON. Решение: перед disableAlarm(1) перепрограммировать Alarm1 в non-PerSecond mode (DS3231_A1_Hour на +1 день вперёд) → A1F больше не set автоматически каждую секунду → после disable SQW стабильно HIGH → MOSFET закрывается → sleep current 2 мА.

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
