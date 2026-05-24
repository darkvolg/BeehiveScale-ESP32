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

// v5.0.35: DS3231 двух-alarm self-latching.
// Alarm1 в PerSecond mode = HOLD (A1F=1 постоянно → SQW LOW → MOSFET ON).
// Alarm2 = wake time (A2F=1 при match → SQW LOW → MOSFET ON для wake).
//
// rtc_enable_persecond_hold — вызывается в setup() сразу после rtc_init.
//   Programm Alarm1 PerSecond + A1IE=1 + INTCN=1 → SQW LOW → MOSFET держится сам.
// rtc_set_alarm_in_seconds — programm Alarm2 на N сек вперёд + disable Alarm1.
//   После вызова SQW HIGH → MOSFET OFF → power off.
// rtc_clear_alarm — сбрасывает A1F и A2F flags.
// rtc_enable_alarm_interrupt — устанавливает INTCN=1.
void        rtc_enable_persecond_hold();
bool        rtc_set_alarm_in_seconds(uint32_t seconds_from_now);
void        rtc_clear_alarm();
void        rtc_enable_alarm_interrupt();
String      rtc_format_datetime(const TimeStamp &t);
String      rtc_format_time(const TimeStamp &t);
// Buf-версии без heap-аллокаций (для горячих путей в loop()):
void        rtc_format_datetime_buf(const TimeStamp &t, char *buf, size_t len);
void        rtc_format_time_buf(const TimeStamp &t, char *buf, size_t len);

#endif
