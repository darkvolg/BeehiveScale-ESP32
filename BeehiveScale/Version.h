#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 66
#define FW_VERSION_SUFFIX ""  // 5.0.66 — FIX дубль записей в архиве. Раньше каждый wake давал 2 записи (08:00 schedLog + 08:02 pre-sleep log в check_auto_sleep). Теперь флаг g_logWrittenThisBoot гейтит pre-sleep лог: если schedLog/bootLog/interval уже писали — pre-sleep пропускается.

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
