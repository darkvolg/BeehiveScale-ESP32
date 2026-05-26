#ifndef BEEHIVE_MQTT_H
#define BEEHIVE_MQTT_H
// ─── MQTT client для Home Assistant Discovery (v5.0.53) ────────────────────
// Публикует weight/temp/battery/rssi в MQTT topics + auto-discovery JSON
// для HA. После одного цикла HA автоматически создаёт sensors.
// Требует библиотеку PubSubClient.

#include <Arduino.h>

bool mqtt_publish_data(float weight, float tempC, float batV, int batPct, long rssi);

#endif
