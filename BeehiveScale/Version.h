#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 67
#define FW_VERSION_SUFFIX ""  // 5.0.67 — FEATURE: IP-адрес на LCD. Новый экран 7/9 (IP + mDNS-имя) + показ адреса 3 сек сразу после подключения к WiFi. mDNS-имя укорочено beehivescale → vesy (http://vesy.local), тем же именем представляется ArduinoOTA. Раньше IP уходил только в Serial — приходилось искать адрес в админке роутера.

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
