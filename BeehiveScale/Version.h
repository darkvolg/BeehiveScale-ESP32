#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 54
#define FW_VERSION_SUFFIX ""  // 5.0.54 — fix: MQTT publish с retain=true. v5.0.53 публиковал values без retain → broker не сохранял → HA видел 'Неизвестно' до следующего publish (через 30 мин). Теперь retain=true → broker хранит last value → HA сразу получает при подписке. При перезагрузке HA тоже видит актуальные данные.

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
