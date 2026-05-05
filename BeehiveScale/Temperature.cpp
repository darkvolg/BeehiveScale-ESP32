#include "Temperature.h"

#ifdef TEMP_SENSOR_DS18B20
  #include <OneWire.h>
  #include <DallasTemperature.h>
  static OneWire           _ow(TEMP_PIN);
  static DallasTemperature _ds(&_ow);
#endif

static bool _tempFound = false;
static bool _firstRead = true;

bool temp_init() {
#ifdef TEMP_SENSOR_DS18B20
  // GPIO3 (RX) после Serial.begin() остаётся в UART-mux режиме.
  // Явно переключаем pin-mux в GPIO (INPUT) — UART RX отвязывается от пина,
  // OneWire получает чистую линию. Serial TX (GPIO1) продолжает работать для логов.
  pinMode(TEMP_PIN, INPUT);
  _ds.begin();
  uint8_t count = _ds.getDeviceCount();
  Serial.print(F("[Temp] DS18B20 sensors found: "));
  Serial.println(count);
  if (count == 0) {
    _tempFound = false;
    return false;
  }
  _ds.setResolution(12);
  _ds.setWaitForConversion(false);
  _ds.requestTemperatures();
  _tempFound = true;
  _firstRead = true;
  return true;
#endif
  return false;
}

bool temp_available() {
  return _tempFound;
}

TempData temp_read() {
  TempData td;

#ifdef TEMP_SENSOR_DS18B20
  if (!_tempFound) return td;

  // Первое чтение после init: конверсия 12-bit ~750ms ещё не готова,
  // DS18B20 вернёт power-on default 85.0°C — пропускаем
  if (_firstRead) {
    _firstRead = false;
    _ds.requestTemperatures();
    return td;  // valid=false, пропуск первого чтения
  }

  // Retry до 3 раз: на ESP8266 WiFi-прерывания могут нарушить OneWire-тайминг
  // и вызвать CRC-ошибку (DEVICE_DISCONNECTED_C). Повтор через паузу помогает.
  float t = DEVICE_DISCONNECTED_C;
  for (int attempt = 0; attempt < 3; attempt++) {
    t = _ds.getTempCByIndex(0);
    if (t != DEVICE_DISCONNECTED_C) break;
    if (attempt < 2) { unsigned long tStart=millis(); while(millis()-tStart<20){yield();} }
  }
  _ds.requestTemperatures();

  if (t == DEVICE_DISCONNECTED_C || t < -55.0f || t > 125.0f || t == 85.0f) {
    td.valid = false;
    td.temperature = TEMP_ERROR_VALUE;
  } else {
    td.temperature = t;
    td.valid = true;
  }
  td.humidity = TEMP_ERROR_VALUE;
#endif

  return td;
}

TempData temp_force_read() {
  TempData td;
#ifdef TEMP_SENSOR_DS18B20
  if (!_tempFound) return td;

  // Блокирующее чтение: setWaitForConversion(true) → requestTemperatures() ждёт
  // ~750мс (для 12-bit) пока датчик закончит измерение, и только потом возвращает.
  _ds.setWaitForConversion(true);
  _ds.requestTemperatures();
  float t = DEVICE_DISCONNECTED_C;
  for (int attempt = 0; attempt < 3; attempt++) {
    t = _ds.getTempCByIndex(0);
    if (t != DEVICE_DISCONNECTED_C && t != 85.0f) break;
    if (attempt < 2) {
      _ds.requestTemperatures();  // повторный запрос с ожиданием
    }
  }
  _ds.setWaitForConversion(false);  // возврат в async режим для process_temperature()
  _firstRead = false;  // первое чтение прошло, дальше обычный async подойдёт

  if (t == DEVICE_DISCONNECTED_C || t < -55.0f || t > 125.0f || t == 85.0f) {
    td.valid = false;
    td.temperature = TEMP_ERROR_VALUE;
  } else {
    td.temperature = t;
    td.valid = true;
  }
  td.humidity = TEMP_ERROR_VALUE;
#endif
  return td;
}
