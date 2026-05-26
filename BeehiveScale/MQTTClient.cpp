#include "MQTTClient.h"
#include "Memory.h"
#include <PubSubClient.h>
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

static WiFiClient _wifiClient;
static PubSubClient _mqtt(_wifiClient);
static bool _discoverySent = false;

// Один раз публикует HA MQTT Discovery JSON для каждого sensor.
// HA автоматически создаёт sensor.beehive_weight, .._temperature, etc.
static void _publishDiscovery(const MqttSettings &cfg) {
  if (_discoverySent) return;
  char topic[128], payload[400];

  // device JSON shared между всеми sensors
  const char* deviceJson =
    "\"device\":{\"identifiers\":[\"beehive_scale\"],"
    "\"name\":\"BeehiveScale\",\"manufacturer\":\"DIY\","
    "\"model\":\"ESP32-DOIT\",\"sw_version\":\"5.0.53\"}";

  // Weight sensor
  snprintf(topic, sizeof(topic), "homeassistant/sensor/beehive_weight/config");
  snprintf(payload, sizeof(payload),
    "{\"name\":\"Beehive Weight\",\"unique_id\":\"beehive_weight\","
    "\"state_topic\":\"%s/weight\",\"unit_of_measurement\":\"kg\","
    "\"device_class\":\"weight\",\"state_class\":\"measurement\","
    "%s}", cfg.topic, deviceJson);
  _mqtt.publish(topic, payload, true);

  // Temperature sensor
  snprintf(topic, sizeof(topic), "homeassistant/sensor/beehive_temp/config");
  snprintf(payload, sizeof(payload),
    "{\"name\":\"Beehive Temperature\",\"unique_id\":\"beehive_temp\","
    "\"state_topic\":\"%s/temperature\",\"unit_of_measurement\":\"°C\","
    "\"device_class\":\"temperature\",\"state_class\":\"measurement\","
    "%s}", cfg.topic, deviceJson);
  _mqtt.publish(topic, payload, true);

  // Battery voltage
  snprintf(topic, sizeof(topic), "homeassistant/sensor/beehive_battery_v/config");
  snprintf(payload, sizeof(payload),
    "{\"name\":\"Beehive Battery V\",\"unique_id\":\"beehive_bat_v\","
    "\"state_topic\":\"%s/battery_v\",\"unit_of_measurement\":\"V\","
    "\"device_class\":\"voltage\",\"state_class\":\"measurement\","
    "%s}", cfg.topic, deviceJson);
  _mqtt.publish(topic, payload, true);

  // Battery percent
  snprintf(topic, sizeof(topic), "homeassistant/sensor/beehive_battery_pct/config");
  snprintf(payload, sizeof(payload),
    "{\"name\":\"Beehive Battery\",\"unique_id\":\"beehive_bat_pct\","
    "\"state_topic\":\"%s/battery_pct\",\"unit_of_measurement\":\"%%\","
    "\"device_class\":\"battery\",\"state_class\":\"measurement\","
    "%s}", cfg.topic, deviceJson);
  _mqtt.publish(topic, payload, true);

  // WiFi RSSI
  snprintf(topic, sizeof(topic), "homeassistant/sensor/beehive_rssi/config");
  snprintf(payload, sizeof(payload),
    "{\"name\":\"Beehive RSSI\",\"unique_id\":\"beehive_rssi\","
    "\"state_topic\":\"%s/rssi\",\"unit_of_measurement\":\"dBm\","
    "\"device_class\":\"signal_strength\",\"state_class\":\"measurement\","
    "%s}", cfg.topic, deviceJson);
  _mqtt.publish(topic, payload, true);

  _discoverySent = true;
  Serial.println(F("[MQTT] HA Discovery JSON published"));
}

bool mqtt_publish_data(float weight, float tempC, float batV, int batPct, long rssi) {
  MqttSettings cfg;
  load_mqtt(cfg);
  if (!cfg.enabled || cfg.host[0] == '\0') return false;
  if (WiFi.status() != WL_CONNECTED) return false;

  _mqtt.setServer(cfg.host, cfg.port);
  _mqtt.setBufferSize(512);  // для HA Discovery JSON ~400 байт

  // Connect (если ещё не подключён)
  if (!_mqtt.connected()) {
    String clientId = "beehive-";
    clientId += String((uint32_t)ESP.getEfuseMac(), HEX);
    bool ok;
    if (cfg.user[0] != '\0') {
      ok = _mqtt.connect(clientId.c_str(), cfg.user, cfg.pass);
    } else {
      ok = _mqtt.connect(clientId.c_str());
    }
    if (!ok) {
      Serial.print(F("[MQTT] Connect FAILED, state="));
      Serial.println(_mqtt.state());
      return false;
    }
    Serial.println(F("[MQTT] Connected"));
    _discoverySent = false;  // переподключение → пере-publish discovery
  }

  // Discovery (один раз за коннект)
  _publishDiscovery(cfg);

  // Publish values
  char topic[64], val[16];
  snprintf(topic, sizeof(topic), "%s/weight", cfg.topic);
  snprintf(val, sizeof(val), "%.2f", weight);
  _mqtt.publish(topic, val);

  if (tempC > -90) {
    snprintf(topic, sizeof(topic), "%s/temperature", cfg.topic);
    snprintf(val, sizeof(val), "%.1f", tempC);
    _mqtt.publish(topic, val);
  }

  snprintf(topic, sizeof(topic), "%s/battery_v", cfg.topic);
  snprintf(val, sizeof(val), "%.2f", batV);
  _mqtt.publish(topic, val);

  snprintf(topic, sizeof(topic), "%s/battery_pct", cfg.topic);
  snprintf(val, sizeof(val), "%d", batPct);
  _mqtt.publish(topic, val);

  snprintf(topic, sizeof(topic), "%s/rssi", cfg.topic);
  snprintf(val, sizeof(val), "%ld", rssi);
  _mqtt.publish(topic, val);

  _mqtt.loop();
  Serial.println(F("[MQTT] Data published"));
  return true;
}
