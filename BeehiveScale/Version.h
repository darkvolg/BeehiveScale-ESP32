#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 49
#define FW_VERSION_SUFFIX ""  // 5.0.49 — UX: countdown подсказка LCD при удержании MAIN. Раньше на 3й сек показывалось "Otpust = TARA!" → пользователь думал что таре уже сработало и отпускал → случайная тара вместо калибровки. Теперь 3-6 сек: "TARA / KAL Xs" с обратным счётом до калибровки. После 6 сек: "Otpust = KALIBR". Помогает понять что можно ещё подержать для калибровки.

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
