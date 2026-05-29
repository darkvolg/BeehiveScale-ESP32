#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 61
#define FW_VERSION_SUFFIX ""  // 5.0.61 — feature: батарея (В) в архиве по дням. Каждая запись теперь показывает 'HH:MM · W кг · T°C · 🔋X.XXВ'. Данные уже писались в CSV (bat_v колонка) — добавлено только отображение в JS архивной страницы. Помогает видеть динамику разряда/заряда батареи по дням без захода в HA.

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
