#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 51
#define FW_VERSION_SUFFIX ""  // 5.0.51 — feature: в TG отчёт добавлены батарея (В + %) и WiFi RSSI (dBm). Помогает мониторить состояние весов на удалённой пасеке — видишь когда батарея садится и качество сигнала. Иконки 🔋/🪫 и 📶/📵 для визуального быстрого восприятия.

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
