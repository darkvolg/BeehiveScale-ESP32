#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 50
#define FW_VERSION_SUFFIX ""  // 5.0.50 — revert: откат подсказки калибровки на v5.0.47 поведение. Countdown "KALIBR cherez Xs" не понравилось пользователю. Возврат к двухуровневой подсказке: 3-6 сек "Otpust = TARA!", 6+ сек "Otpust = KALIBR". Галочка TG-on-interval из v5.0.48 сохранена.

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
