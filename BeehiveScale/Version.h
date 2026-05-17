#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 21
#define FW_VERSION_SUFFIX ""  // 5.0.21 — fix: отложенный старт WiFi (8 сек после boot) + понижение TX power до 11dBm + modem-sleep. Решает защёлку MT3608 OCP при wake-up: пик ESP32+LCD+WiFi одновременно был 600-700мА → Boost защёлкивался → синий экран, нет TG. Теперь ESP32+LCD сначала (200мА), пауза 8с (капы заряжаются), затем WiFi отдельным пиком 200мА.

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
