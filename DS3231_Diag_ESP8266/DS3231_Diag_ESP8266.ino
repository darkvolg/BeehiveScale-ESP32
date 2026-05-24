/*
 * DS3231 Diagnostic для ESP8266 — standalone тест module.
 *
 * Подключение (ESP8266 NodeMCU):
 *   DS3231 SDA → D2 (GPIO4)
 *   DS3231 SCL → D1 (GPIO5)
 *   DS3231 VCC → 3V3 ESP
 *   DS3231 GND → GND ESP
 *   DS3231 SQW → D5 (GPIO14)  — для чтения уровня alarm pin
 *
 *   CR2032 вставлена в module.
 *   Никакой MOSFET, никакой power-cut — чистая проверка chip.
 *
 * Что тест проверяет:
 *   1. Module отвечает на I2C (адрес 0x68)
 *   2. Время идёт (DS3231 chip работает)
 *   3. Alarm1 fires через 10 секунд (SQW LOW + A1F=1)
 *   4. Печатает все регистры (диагностика)
 */

#include <Wire.h>
#include <RTClib.h>

#define SQW_PIN 14  // D5

RTC_DS3231 rtc;

void clearEOSC_OSF() {
  Wire.beginTransmission(0x68);
  Wire.write(0x0E);
  Wire.endTransmission(false);
  Wire.requestFrom(0x68, (uint8_t)1);
  uint8_t ctrl = Wire.read();
  Serial.print("[DIAG] CONTROL before = 0x"); Serial.println(ctrl, HEX);
  ctrl &= ~0x80;
  Wire.beginTransmission(0x68);
  Wire.write(0x0E);
  Wire.write(ctrl);
  Wire.endTransmission();

  Wire.beginTransmission(0x68);
  Wire.write(0x0F);
  Wire.endTransmission(false);
  Wire.requestFrom(0x68, (uint8_t)1);
  uint8_t status = Wire.read();
  Serial.print("[DIAG] STATUS before = 0x"); Serial.println(status, HEX);
  status &= ~0x80;
  Wire.beginTransmission(0x68);
  Wire.write(0x0F);
  Wire.write(status);
  Wire.endTransmission();
}

void dumpRegisters() {
  Serial.println("[DIAG] DS3231 registers dump:");
  for (uint8_t reg = 0x00; reg <= 0x12; reg++) {
    Wire.beginTransmission(0x68);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom(0x68, (uint8_t)1);
    if (Wire.available()) {
      uint8_t val = Wire.read();
      Serial.print("  0x"); if (reg < 16) Serial.print("0");
      Serial.print(reg, HEX); Serial.print(" = 0x");
      if (val < 16) Serial.print("0");
      Serial.println(val, HEX);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== DS3231 DIAGNOSTIC ESP8266 v1 ===\n");

  pinMode(SQW_PIN, INPUT_PULLUP);  // SQW обычно open-drain, нужен pullup

  Wire.begin(4, 5);  // SDA=GPIO4=D2, SCL=GPIO5=D1
  Wire.setClock(100000);

  if (!rtc.begin()) {
    Serial.println("[DIAG] X RTC NOT FOUND on I2C!");
    Serial.println("  Проверь SDA(D2)/SCL(D1)/VCC/GND проводку");
    while (1) delay(1000);
  }
  Serial.println("[DIAG] OK RTC found on I2C (0x68)");

  Serial.print("[DIAG] lostPower flag = ");
  Serial.println(rtc.lostPower() ? "TRUE (плохо)" : "FALSE (хорошо)");

  DateTime now = rtc.now();
  Serial.print("[DIAG] Current time = ");
  Serial.print(now.year()); Serial.print("-");
  Serial.print(now.month()); Serial.print("-");
  Serial.print(now.day()); Serial.print(" ");
  Serial.print(now.hour()); Serial.print(":");
  Serial.print(now.minute()); Serial.print(":");
  Serial.println(now.second());

  if (now.year() < 2020) {
    Serial.println("[DIAG] ! Время не установлено, ставлю фиктивное 2026-01-01 12:00:00");
    rtc.adjust(DateTime(2026, 1, 1, 12, 0, 0));
  }

  clearEOSC_OSF();
  dumpRegisters();

  // Тест 1 — Alarm1 fires через 10 секунд
  Serial.println("\n[DIAG] === TEST 1: Alarm1 fires through 10 sec ===");

  rtc.disableAlarm(1);
  rtc.disableAlarm(2);
  rtc.clearAlarm(1);
  rtc.clearAlarm(2);
  rtc.writeSqwPinMode(DS3231_OFF);  // INTCN=1

  DateTime alarmTime = rtc.now() + TimeSpan(10);
  Serial.print("  Current: "); Serial.println(rtc.now().unixtime());
  Serial.print("  Alarm:   "); Serial.println(alarmTime.unixtime());

  if (!rtc.setAlarm1(alarmTime, DS3231_A1_Hour)) {
    Serial.println("[DIAG] X setAlarm1 FAILED!");
  } else {
    Serial.println("[DIAG] OK Alarm1 set");
  }

  Serial.println("\n[DIAG] Now monitoring SQW pin and A1F flag...\n");
  dumpRegisters();
}

void loop() {
  static uint32_t lastPrint = 0;
  static bool alarmFired = false;
  uint32_t now = millis();
  if (now - lastPrint < 1000) return;
  lastPrint = now;

  DateTime t = rtc.now();
  int sqwLevel = digitalRead(SQW_PIN);

  Serial.print("[");
  Serial.print(t.hour()); Serial.print(":");
  if (t.minute() < 10) Serial.print("0");
  Serial.print(t.minute()); Serial.print(":");
  if (t.second() < 10) Serial.print("0");
  Serial.print(t.second()); Serial.print("] SQW=");
  Serial.print(sqwLevel ? "HIGH" : "LOW ");

  // A1F, A2F, OSF
  Wire.beginTransmission(0x68);
  Wire.write(0x0F);
  Wire.endTransmission(false);
  Wire.requestFrom(0x68, (uint8_t)1);
  uint8_t status = Wire.read();
  Serial.print(" | A1F=");
  Serial.print(status & 0x01);
  Serial.print(" A2F=");
  Serial.print((status >> 1) & 0x01);
  Serial.print(" OSF=");
  Serial.print((status >> 7) & 0x01);

  if (sqwLevel == LOW && !alarmFired) {
    Serial.println(" *** ALARM FIRED ***");
    alarmFired = true;
    Serial.println("[DIAG] OK OK OK MODULE WORKS!");
  } else {
    Serial.println();
  }

  // После 30 секунд — повторить тест с PerSecond
  static bool secondTestStarted = false;
  if (alarmFired && !secondTestStarted && now > 30000) {
    secondTestStarted = true;
    Serial.println("\n[DIAG] === TEST 2: Alarm1 PerSecond (HOLD test) ===");
    rtc.disableAlarm(1);
    rtc.clearAlarm(1);
    DateTime nowDt = rtc.now();
    if (!rtc.setAlarm1(nowDt, DS3231_A1_PerSecond)) {
      Serial.println("[DIAG] X PerSecond setAlarm1 FAILED");
    } else {
      Serial.println("[DIAG] OK PerSecond programmed. SQW should stay LOW now.");
    }
    dumpRegisters();
  }
}
