#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 20
#define FW_VERSION_SUFFIX ""  // 5.0.20 — feat: настраиваемые Telegram-алерты (низкая батарея в В, температурные пороги низкий/высокий, RTC error). Анти-спам через гистерезис: каждый алерт шлётся 1 раз, повторно — только после возврата в норму. Новый endpoint /api/alerts, EEPROM addr 342-356 (magic 0xAD), модуль Alerts.h/cpp. UI в Telegram → блок "Пороги алертов".

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
