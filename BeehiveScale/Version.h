#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 71
#define FW_VERSION_SUFFIX ""  // 5.0.71 — Excel-выгрузка стала книгой из трёх листов: «Сводка» (итоги + остаток дней работы от батареи), «Замеры», «По дням». Формат сменён с HTML-таблицы на SpreadsheetML 2003 — единственный, который держит несколько листов в одном файле и собирается на ESP32 обычной печатью в поток.

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
