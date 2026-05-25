/*
 * DS3231 Diagnostic — standalone тест модуля без всей нашей схемы.
 * Проверяет:
 *   1. RTC init OK
 *   2. Время считается (DS3231 работает)
 *   3. Backup на CR2032 (после reset ESP время сохраняется)
 *   4. Alarm fires (SQW pin pulls LOW при match)
 *
 * Подключение:
 *   DS3231 SDA → ESP GPIO 21
 *   DS3231 SCL → ESP GPIO 22
 *   DS3231 VCC → ESP 3.3V
 *   DS3231 GND → ESP GND
 *   DS3231 SQW → ESP GPIO 33  (для чтения уровня alarm pin)
 *
 *   CR2032 вставлена в module.
 *   Подключаем ТОЛЬКО ESP+USB — никакой MOSFET, никакой power-cut.
 */

#include <Wire.h>
#include <RTClib.h>

#define SQW_PIN 33

RTC_DS3231 rtc;

void clearEOSC_OSF() {
  // Явный clear EOSC (CONTROL 0x0E bit 7) и OSF (STATUS 0x0F bit 7).
  Wire.beginTransmission(0x68);
  Wire.write(0x0E);
  Wire.endTransmission(false);
  Wire.requestFrom(0x68, (uint8_t)1);
  uint8_t ctrl = Wire.read();
  Serial.print("[DIAG] CONTROL before = 0x"); Serial.println(ctrl, HEX);
  ctrl &= ~0x80;  // clear EOSC
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
  status &= ~0x80;  // clear OSF
  Wire.beginTransmission(0x68);
  Wire.write(0x0F);
  Wire.write(status);
  Wire.endTransmission();
}

void dumpRegisters() {
  Serial.println("[DIAG] All DS3231 registers:");
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
  Serial.println("\n=== DS3231 DIAGNOSTIC v1 ===\n");

  pinMode(SQW_PIN, INPUT_PULLUP);  // SQW open-drain → нужен внутренний pullup ESP32 (~45kΩ)

  Wire.begin();
  Wire.setClock(100000);

  if (!rtc.begin()) {
    Serial.println("[DIAG] ❌ RTC NOT FOUND on I2C!");
    Serial.println("  Проверь SDA/SCL/VCC/GND проводку");
    while (1) delay(1000);
  }
  Serial.println("[DIAG] ✅ RTC found on I2C (0x68)");

  Serial.print("[DIAG] lostPower flag = ");
  Serial.println(rtc.lostPower() ? "TRUE (плохо)" : "FALSE (хорошо)");

  // Прочитать текущее время
  DateTime now = rtc.now();
  Serial.print("[DIAG] Current time = ");
  Serial.print(now.year()); Serial.print("-");
  Serial.print(now.month()); Serial.print("-");
  Serial.print(now.day()); Serial.print(" ");
  Serial.print(now.hour()); Serial.print(":");
  Serial.print(now.minute()); Serial.print(":");
  Serial.println(now.second());

  if (now.year() < 2020) {
    Serial.println("[DIAG] ⚠ Время не установлено — устанавливаю фиктивное");
    rtc.adjust(DateTime(2026, 1, 1, 12, 0, 0));
  }

  clearEOSC_OSF();
  dumpRegisters();

  // Test 1: Alarm fires
  Serial.println("\n[DIAG] === TEST 1: Alarm fires ===");
  Serial.println("  Программирую Alarm1 на +10 секунд...");

  rtc.disableAlarm(1);
  rtc.disableAlarm(2);
  rtc.clearAlarm(1);
  rtc.clearAlarm(2);
  rtc.writeSqwPinMode(DS3231_OFF);  // INTCN=1

  DateTime alarmTime = rtc.now() + TimeSpan(10);
  Serial.print("  Текущее: ");
  Serial.print(rtc.now().unixtime());
  Serial.print(", alarm на: ");
  Serial.println(alarmTime.unixtime());

  if (!rtc.setAlarm1(alarmTime, DS3231_A1_Hour)) {
    Serial.println("[DIAG] ❌ setAlarm1 FAILED!");
  } else {
    Serial.println("[DIAG] ✅ Alarm1 set");
  }

  dumpRegisters();
}

void loop() {
  static uint32_t lastPrint = 0;
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
  Serial.print(sqwLevel ? "HIGH" : "LOW");

  // Проверить A1F flag
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
  Serial.println();

  if (sqwLevel == LOW) {
    Serial.println("[DIAG] ✅✅✅ ALARM FIRED! SQW pin pulled LOW by DS3231!");
    Serial.println("  Модуль работает корректно. Продолжаю мониторинг...");
  }
}
