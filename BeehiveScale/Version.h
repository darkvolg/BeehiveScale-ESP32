#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 15
#define FW_VERSION_SUFFIX ""  // 5.0.15 — perf: загрузка HTML страницы в 8x быстрее (chunk 512→4096 байт, меньше TCP-пакетов и yield-ов). SD-статистика в /api/data кешируется на 10 сек (LittleFS.usedBytes() медленный, итерирует файлы) — поллинг быстрее.

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
