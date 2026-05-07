#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 12
#define FW_VERSION_SUFFIX ""  // 5.0.12 — feat: кнопка "☕ Продлить на 10 мин" + UI fixes: шапка не съезжает на mobile (.hdr-right теперь static), Δ не переносится в карточке "Текущий вес", графики читаемые на телефоне (font Y 12→26, X 11→24, dates 9→18, padding L 60→90 / B 50→80 mini, 42→75 chart).

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
