#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 62
#define FW_VERSION_SUFFIX ""  // 5.0.62 — feature: 'Привес за сутки' в TG (вечерний замер сравнивается с весом вчерашнего вечера, строка '🍯 Привес за сутки: +X.XX кг (вчера Y.YY)'). Вечерний слот = последнее время расписания, окно ±15 мин. Вес хранится в EEPROM 505-511. FIX: при ручном включении весов больше НЕ шлётся TG-отчёт — bootLog шлёт TG только если время boot в пределах ±10 мин от слота расписания (раньше любое включение = TG).

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
