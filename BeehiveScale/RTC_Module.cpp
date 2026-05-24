#include "RTC_Module.h"

static RTC_DS3231 _rtc;
static bool       _rtcOk = false;

bool rtc_init() {
  _rtcOk = _rtc.begin();
  if (!_rtcOk) {
    Serial.println(F("[RTC] DS3231 not found!"));
    return false;
  }

  // v5.0.38: ЯВНЫЙ clear EOSC bit в CONTROL register (0x0E).
  // EOSC bit 7 = 1 → осциллятор остановлен на VBAT → DS3231 на CR2032 не считает время.
  // RTClib begin() НЕ всегда clear EOSC — некоторые версии оставляют bit как есть после reset.
  // Без clear DS3231 теряет время при power-cut несмотря на работающий backup.
  // Также явный clear OSF (STATUS register 0x0F, bit 7).
  Wire.beginTransmission(0x68);  // DS3231 I2C address
  Wire.write(0x0E);              // CONTROL register
  Wire.endTransmission(false);
  Wire.requestFrom(0x68, (uint8_t)1);
  if (Wire.available()) {
    uint8_t ctrl = Wire.read();
    // Clear EOSC (bit 7) → enable осциллятор on VBAT
    // Clear BBSQW (bit 6) → SQW disable on backup (экономия батарейки CR2032)
    // Keep INTCN (bit 2) and A1IE/A2IE bits as есть
    ctrl &= ~0x80;  // EOSC = 0 (enable осциллятор on VBAT)
    ctrl |= 0x40;   // BBSQW = 1 (enable SQW alarm даже на VBAT/CR2032 backup)
    Wire.beginTransmission(0x68);
    Wire.write(0x0E);
    Wire.write(ctrl);
    Wire.endTransmission();
    Serial.print(F("[RTC] CONTROL after EOSC clear: 0x"));
    Serial.println(ctrl, HEX);
  }

  // v5.0.40: Conditional clear OSF — только если время в DS3231 валидно.
  // Если year < 2020 → время сбито → НЕ clear OSF → ESP сделает NTP sync.
  // Если year >= 2020 → время правильное (от prev session) → clear OSF чтобы убрать stale flag.
  DateTime nowDt = _rtc.now();
  bool timeValid = (nowDt.year() >= 2020 && nowDt.year() <= 2099);
  if (timeValid) {
    Wire.beginTransmission(0x68);
    Wire.write(0x0F);
    Wire.endTransmission(false);
    Wire.requestFrom(0x68, (uint8_t)1);
    if (Wire.available()) {
      uint8_t status = Wire.read();
      status &= ~0x80;  // clear OSF
      Wire.beginTransmission(0x68);
      Wire.write(0x0F);
      Wire.write(status);
      Wire.endTransmission();
    }
    Serial.print(F("[RTC] Time OK: "));
    Serial.println(nowDt.timestamp());
  } else {
    Serial.print(F("[RTC] Time invalid (year="));
    Serial.print(nowDt.year());
    Serial.println(F("), keeping OSF for NTP trigger"));
  }

  if (_rtc.lostPower()) {
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

// ─── v5.0.35: DS3231 двух-alarm self-latching power-cut ───────────────────
//
// Архитектура:
//   Alarm1 = HOLD механизм. PerSecond mode → A1F=1 каждую секунду.
//            A1IE=1 + INTCN=1 → SQW pin = LOW → удерживает AO3401 Gate LOW → MOSFET ON.
//            Используется во время работы ESP. ESP сама себя держит через DS3231.
//
//   Alarm2 = WAKE механизм. Установлен на конкретное время в будущем.
//            A2F=1 при match → SQW LOW → MOSFET ON → ESP boot.
//            Granularity = 1 минута (Alarm2 не имеет seconds register).
//
// Жизненный цикл:
//   1. ESP boot (от SW2 или Alarm2 wake)
//   2. setup() вызывает rtc_enable_persecond_hold() → Alarm1 активен → SQW LOW → HOLD
//   3. ESP работает, можно отпустить SW2 — MOSFET держится через A1F
//   4. sleep_enter() → rtc_set_alarm_in_seconds(N) → disable Alarm1, programm Alarm2 на +N сек
//   5. A1IE=0, A2IE=1, A2F=0 → SQW HIGH → MOSFET OFF → ESP теряет питание мгновенно
//   6. Через N сек → Alarm2 match → A2F=1 → SQW LOW → MOSFET ON → ESP boot → goto 2

void rtc_enable_persecond_hold() {
  if (!_rtcOk) {
    Serial.println(F("[RTC] HOLD FAILED — RTC not OK"));
    return;
  }
  // v5.0.42: КРИТИЧЕСКИЙ ПОРЯДОК — сначала programm Alarm1 PerSecond (новый HOLD),
  // ПОТОМ очищать старый Alarm2 (от prev wake). Если сделать наоборот — disableAlarm(2)
  // очистит A2IE → SQW HIGH (нет flagged alarms) → MOSFET закроется → ESP теряет
  // питание ДО того как PerSecond Alarm1 установлен → ESP мёртвая в каждом цикле wake.
  //
  // На wake A2F=1 от Alarm2 match → SQW LOW → MOSFET ON → ESP boot.
  // setup() ВЫЗЫВАЕТ нас → Alarm2 ВСЁ ЕЩЁ активен (HOLD).
  // 1. writeSqwPinMode(DS3231_OFF) — INTCN=1 (уже был, на всякий)
  // 2. setAlarm1(PerSecond) — A1IE=1 enabled, A1F поднимется в next sec tick
  // 3. delay(1100) — ждём чтобы A1F set (PerSecond match каждую сек)
  // 4. ТЕПЕРЬ A1F=1 → SQW LOW через Alarm1 (новый HOLD активен)
  // 5. disableAlarm(2) + clearAlarm(2) — отпускаем старый HOLD, SQW остаётся LOW через A1F

  _rtc.writeSqwPinMode(DS3231_OFF);  // INTCN=1
  DateTime now = _rtc.now();
  if (!_rtc.setAlarm1(now, DS3231_A1_PerSecond)) {
    Serial.println(F("[RTC] HOLD setAlarm1 FAILED"));
    return;
  }
  Serial.print(F("[RTC] HOLD setting up... waiting for A1F set"));
  // Ждём чтобы PerSecond match произошёл и A1F=1 → SQW LOW через Alarm1
  delay(1100);

  // Теперь A1F=1 (PerSecond match) → SQW гарантированно LOW → HOLD надёжен.
  // Безопасно очистить старый Alarm2 (от prev wake) — SQW остаётся LOW через A1F.
  _rtc.disableAlarm(2);
  _rtc.clearAlarm(2);

  // v5.0.43: НЕ clearAlarm(1) — A1F=1 это и есть наш HOLD!
  // Если очистить → SQW HIGH на ~1 сек до next PerSecond match → MOSFET закроется → ESP сдохнет.
  // PerSecond mode будет re-set A1F каждую секунду автоматически.

  Serial.println(F(" -> HOLD engaged"));
}

bool rtc_set_alarm_in_seconds(uint32_t seconds_from_now) {
  if (!_rtcOk) {
    Serial.println(F("[RTC] Wake-alarm set FAILED — RTC not OK"));
    return false;
  }
  DateTime now = _rtc.now();
  DateTime alarmTime = now + TimeSpan(seconds_from_now);

  // v5.0.36: КРИТИЧЕСКИЙ ПОРЯДОК — сначала Alarm2 programmed, ПОТОМ disable Alarm1 (HOLD).
  // Если сделать наоборот (disable Alarm1 → potом setAlarm2), то SQW идёт HIGH между
  // этими операциями → MOSFET закрывается за ~6µs → ESP живёт ~100мс на cap MT3608 →
  // I2C может не успеть → Alarm2 не запрограммирован → ESP не просыпается никогда.
  //
  // С Alarm1 активным (HOLD) SQW LOW гарантированно → MOSFET ON → ESP стабильна
  // пока выполняет setAlarm2. Только ПОСЛЕ programm Alarm2 → disable Alarm1.

  // Шаг 1: запрограммировать Alarm2 (HOLD ещё активен — Alarm1 PerSecond)
  _rtc.disableAlarm(2);    // снять старый Alarm2 (если был)
  _rtc.clearAlarm(2);      // A2F=0
  // v5.0.41: writeSqwPinMode(DS3231_OFF) ПЕРЕД setAlarm2 — критично!
  // Adafruit RTClib API: setAlarm2 fails silently если SQW в square-wave mode.
  // INTCN=1 должен быть установлен перед программированием alarm.
  _rtc.writeSqwPinMode(DS3231_OFF);
  // v5.0.37: DS3231_A2_Hour вместо A2_Date — match только hour:minute, день игнорируется.
  if (!_rtc.setAlarm2(alarmTime, DS3231_A2_Hour)) {
    Serial.println(F("[RTC] setAlarm2 FAILED"));
    return false;
  }
  // setAlarm2 устанавливает A2IE=1. INTCN уже =1 (от prev rtc_enable_alarm_interrupt).
  // A2F=0 (не match ещё) → не влияет на SQW. HOLD держит через Alarm1.

  Serial.print(F("[RTC] Wake alarm set: "));
  Serial.print(alarmTime.timestamp(DateTime::TIMESTAMP_DATE));
  Serial.print(F(" "));
  Serial.println(alarmTime.timestamp(DateTime::TIMESTAMP_TIME));

  // Шаг 2: ОТКЛЮЧИТЬ HOLD механизм через Alarm1.
  // v5.0.46: КРИТИЧНО — заменить Alarm1 PerSecond mode на non-PerSecond.
  // Если оставить PerSecond — A1F set каждую секунду даже после disableAlarm(1).
  // А1IE=0 не влияет на матч (только на effect SQW), но **некоторые clones DS3231**
  // продолжают tянуть SQW LOW при A1F=1 даже если A1IE=0.
  // Решение: setAlarm1 на далёкое будущее (+1 день) с mode A1_Hour → match через сутки,
  // не каждую секунду. Затем clear+disable.
  DateTime farFuture = now + TimeSpan(1, 0, 0, 0);  // +1 день
  _rtc.setAlarm1(farFuture, DS3231_A1_Hour);  // НЕ PerSecond → A1F не set до match через сутки
  _rtc.clearAlarm(1);      // A1F=0 (если был от PerSecond)
  _rtc.disableAlarm(1);    // A1IE=0 → SQW зависит только от A2F (=0) → SQW HIGH → MOSFET OFF

  return true;
}

void rtc_clear_alarm() {
  if (!_rtcOk) return;
  _rtc.clearAlarm(1);
  _rtc.clearAlarm(2);
}

void rtc_enable_alarm_interrupt() {
  if (!_rtcOk) return;
  // writeSqwPinMode(DS3231_OFF) устанавливает INTCN=1 (alarm interrupt mode).
  _rtc.writeSqwPinMode(DS3231_OFF);
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
