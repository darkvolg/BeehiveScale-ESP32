#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 52
#define FW_VERSION_SUFFIX ""  // 5.0.52 — feature: детекция роения по резкому падению веса. Если current_weight < prev_weight - threshold (3×alertDelta или мин 1.5 кг) и time_delta < 30 мин → отдельный TG алерт 'РОЕНИЕ'. Anti-spam: один алерт за событие, сбрасывается при стабилизации. Вызывается в loop одновременно с alerts_check (при каждом logging). Использует persist.lastWeight как предыдущий замер.

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
