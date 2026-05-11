#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 16
#define FW_VERSION_SUFFIX ""  // 5.0.16 — feat: вкладка "Архив" с выбором периода (с/по + пресеты), лента дней с цветными маркерами аномалий, экспорт CSV за диапазон. PWA: установка как приложение на телефон (manifest+SW), оффлайн-кеш HTML. Новые API: /api/period?from&to, /api/log?from&to, /manifest.json, /sw.js, /icon.svg.

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
