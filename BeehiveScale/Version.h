#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 13
#define FW_VERSION_SUFFIX ""  // 5.0.13 — feat: интерактивный курсор на графиках (mouse + touch). Тач-скраб по графику показывает вертикальную линию + точку + tooltip с весом/датой. Графики занимают всю высоту контейнера (aspect-ratio 3/2 mini, 9/5 charts вместо фикс. height) — viewBox 900x600/500.

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
