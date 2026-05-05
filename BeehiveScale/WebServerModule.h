#ifndef WEB_SERVER_MODULE_H
#define WEB_SERVER_MODULE_H

#include <Arduino.h>

#define WEB_SERVER_PORT   80
// WEB_ADMIN_USER / WEB_ADMIN_PASS перенесены в EEPROM: см. get_admin_user()/get_admin_pass()
// Дефолты при пустом EEPROM: admin / beehive (credentials_is_default() → true, UI показывает баннер)
#define WEB_REFRESH_SEC   5

struct WebData {
  float*  weight;
  float*  lastSavedWeight;
  float*  tempC;
  // humidity убран в v4.2: нет подключённого датчика влажности — не выводить dummy.
  float*  rtcTempC;
  float*  calibFactor;
  long*   offset;
  bool*   sensorReady;
  bool*   wifiOk;
  String* datetime;
  uint32_t* wakeupCount;
  float*  batVoltage;
  int*    batPercent;
  float*  prevWeight;
};

struct WebActions {
  void (*doTare)();
  void (*doSave)();
  void (*onActivity)();  // вызывается при любом веб-запросе (для подсветки LCD)
  void (*doSetCalibFactor)(float cf);  // установить калибровочный коэффициент
  void (*doSetCalibOffset)(long offset);  // установить offset
};

extern unsigned long lastActivityTime;

void webserver_init(WebData &data, WebActions &actions);
void webserver_handle();
void webserver_stop();

#endif
