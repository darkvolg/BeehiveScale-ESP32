#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 70
#define FW_VERSION_SUFFIX ""  // 5.0.70 — FIX: кнопка «Весь лог CSV» всегда отдавала 500 «Cannot open log». Веб-слой открывал файл через макрос LOG_FS = SPIFFS, а лог пишется в LittleFS. Выгрузка переведена на Logger, макрос убран.

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
