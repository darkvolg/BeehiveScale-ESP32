#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 53
#define FW_VERSION_SUFFIX ""  // 5.0.53 — feature: MQTT клиент для Home Assistant Discovery. Новый файл MQTTClient.cpp с PubSubClient. EEPROM addr 363-502 (MQTT host/port/user/pass/topic/enabled). UI секция в Telegram странице. После активации улей автоматически появляется в HA как устройство с 5 sensors (weight, temperature, battery V, battery %, RSSI). Topics: beehive/weight, beehive/temperature, beehive/battery_v, beehive/battery_pct, beehive/rssi. Discovery JSON публикуется один раз за коннект. Требует библиотеку PubSubClient.

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
