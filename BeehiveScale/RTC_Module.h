#ifndef RTC_MODULE_H
#define RTC_MODULE_H

#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>

struct TimeStamp {
  uint16_t year;
  uint8_t  month, day;
  uint8_t  hour, minute, second;
  bool     valid;
};

bool        rtc_init();
TimeStamp   rtc_now();
bool        rtc_set(uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi, uint8_t s);
bool        rtc_lost_power();
float       rtc_temperature();
String      rtc_format_datetime(const TimeStamp &t);
String      rtc_format_time(const TimeStamp &t);
// Buf-версии без heap-аллокаций (для горячих путей в loop()):
void        rtc_format_datetime_buf(const TimeStamp &t, char *buf, size_t len);
void        rtc_format_time_buf(const TimeStamp &t, char *buf, size_t len);

#endif
