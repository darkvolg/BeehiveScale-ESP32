#include "RTC_Module.h"

static RTC_DS3231 _rtc;
static bool       _rtcOk = false;

bool rtc_init() {
  _rtcOk = _rtc.begin();
  if (!_rtcOk) {
    Serial.println(F("[RTC] DS3231 not found!"));
    return false;
  }
  if (_rtc.lostPower()) {
    // НИКОГДА не писать компильную дату — она может быть в будущем/прошлом
    // относительно реального времени, а Logger и TG-report полагаются
    // на корректный timestamp. Лучше оставить RTC в lostPower()-состоянии:
    // rtc_now() вернёт valid=false, loop() использует uptime как fallback
    // и после успешного NTP sync (STA режим) RTC будет скорректирован.
    Serial.println(F("[RTC] Power lost — waiting for NTP sync"));
  }
  Serial.println(F("[RTC] OK"));
  return true;
}

TimeStamp rtc_now() {
  TimeStamp ts = {};
  ts.valid = _rtcOk;
  if (!_rtcOk) return ts;

  DateTime dt = _rtc.now();
  if (dt.year() < 2020 || dt.year() > 2099) {
    ts.valid = false;
    return ts;
  }
  ts.year   = dt.year();
  ts.month  = dt.month();
  ts.day    = dt.day();
  ts.hour   = dt.hour();
  ts.minute = dt.minute();
  ts.second = dt.second();
  return ts;
}

bool rtc_set(uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi, uint8_t s) {
  if (!_rtcOk) return false;
  if (y < 2020 || y > 2099) return false;
  if (mo < 1 || mo > 12) return false;
  // Days per month validation
  static const uint8_t daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  uint8_t maxDay = daysInMonth[mo - 1];
  if (mo == 2 && ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0))) maxDay = 29;
  if (d < 1 || d > maxDay) return false;
  if (h > 23 || mi > 59 || s > 59) return false;
  _rtc.adjust(DateTime(y, mo, d, h, mi, s));
  return true;
}

bool rtc_lost_power() {
  return _rtcOk ? _rtc.lostPower() : true;
}

float rtc_temperature() {
  if (!_rtcOk) return NAN;
  return _rtc.getTemperature();
}

void rtc_format_datetime_buf(const TimeStamp &t, char *buf, size_t len) {
  if (!buf || len < 20) { if (buf && len) buf[0] = '\0'; return; }
  if (!t.valid) {
    strncpy(buf, "??.??.???? ??:??:??", len);
    buf[len - 1] = '\0';
    return;
  }
  snprintf(buf, len, "%02u.%02u.%04u %02u:%02u:%02u",
           t.day, t.month, t.year, t.hour, t.minute, t.second);
}

void rtc_format_time_buf(const TimeStamp &t, char *buf, size_t len) {
  if (!buf || len < 9) { if (buf && len) buf[0] = '\0'; return; }
  if (!t.valid) {
    strncpy(buf, "??:??:??", len);
    buf[len - 1] = '\0';
    return;
  }
  snprintf(buf, len, "%02u:%02u:%02u", t.hour, t.minute, t.second);
}

String rtc_format_datetime(const TimeStamp &t) {
  char buf[20];
  rtc_format_datetime_buf(t, buf, sizeof(buf));
  return String(buf);
}

String rtc_format_time(const TimeStamp &t) {
  char buf[9];
  rtc_format_time_buf(t, buf, sizeof(buf));
  return String(buf);
}
