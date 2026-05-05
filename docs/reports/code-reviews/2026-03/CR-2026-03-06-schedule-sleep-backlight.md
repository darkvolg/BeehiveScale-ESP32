# Code Review: Schedule / Sleep / Backlight

**Date**: 2026-03-06
**Scope**: Незакоммиченные изменения — расписание замеров, boot-лог, _keepalive, retry DS18B20
**Files**: 5 | **Changes**: +179 / -14

## Summary

|              | Critical | High | Medium | Low |
|--------------|----------|------|--------|-----|
| Issues       | 0        | 1    | 1      | 1   |
| Improvements | —        | 0    | 1      | 1   |

**Verdict**: NEEDS WORK (1 P1 баг — boot-лог с невалидным datetime)

---

## Issues

### High (P1)

#### 1. Boot-лог может записаться с datetime="--" при быстром старте

- **File**: `BeehiveScale/BeehiveScale.ino:374`
- **Problem**: Условие `sys.datetimeStr.length() > 0` выполняется с первой итерации loop(), потому что начальное значение `datetimeStr = "--"` имеет длину 2. RTC читается только раз в `TEMP_READ_INTERVAL_MS` = 10 секунд. Если HX711 стабилизируется раньше чем истечёт первые 10с, `_bootLogDone` срабатывает с `datetimeStr = "--"`, и в CSV-лог попадает строка с мусорной датой.
- **Impact**: Портит лог, создаёт невалидные строки с датой "--", которые ломают парсинг CSV по дате.
- **Fix**:
  ```cpp
  // Было:
  if (!_bootLogDone && sys.sensorReady && sys.datetimeStr.length() > 0) {

  // Стать:
  if (!_bootLogDone && sys.sensorReady && sys.currentTime.valid) {
  ```
  Также убрать обновление `_lastSchedLogMin` в else-ветке bootLog: если RTC невалиден, `sys.currentTime.valid = false` → эта ветка недостижима.

---

### Medium (P2)

#### 2. `_handleSettings` вызывает `_activity()` до проверки метода

- **File**: `BeehiveScale/WebServerModule.cpp:1461`
- **Problem**: `_activity()` вызывается перед `if (_srv.method() != HTTP_POST)`. Если кто-то отправляет GET-запрос на `/api/settings`, подсветка LCD включается и таймер сна сбрасывается даже без реального действия.
- **Impact**: Незначительный — фронтенд всегда отправляет POST. Но нарушает принцип "только реальные действия сбрасывают таймер".
- **Fix**: Переставить `_activity()` после проверки метода:
  ```cpp
  static void _handleSettings() {
    if (!_auth()) return;
    if (_srv.method() != HTTP_POST) { _sendJson(false,"Только POST"); return; }
    _activity();  // ← теперь после проверки метода
    ...
  ```

---

### Low (P3)

#### 3. `delay(20)` в retry-цикле DS18B20 блокирует WiFi

- **File**: `BeehiveScale/Temperature.cpp:61`
- **Problem**: `delay(20)` — синхронный блок без `yield()`. При 2 ретраях = 40мс без обработки WiFi пакетов. На ESP8266 это ниже порога дроппинга (~100мс), но при активном MQTT/HTTP может вызвать кратковременный сбой.
- **Impact**: Очень низкий при текущем использовании. Потенциальный источник WiFi-глюков при расширении функционала.
- **Fix**: Заменить `delay(20)` на `{ unsigned long _t=millis(); while(millis()-_t<20){yield();} }` — аналогично паттерну, уже используемому в проекте.

---

## Improvements

### Medium

#### 4. Времена расписания не сортируются при сохранении

- **File**: `BeehiveScale/Memory.cpp:338`
- **Problem**: `set_sched_times()` сохраняет времена в том порядке, в котором они пришли из JSON. `sched_next_sec()` корректно работает с несортированным массивом (два прохода: мин > now, иначе мин абсолютный), но при отладке через Serial сложно читать порядок.
- **Current**: Сохраняется как получено: `[20:00, 08:00, 14:00]`
- **Recommended**: Добавить сортировку пузырьком (8 элементов — O(n²) допустим):
  ```cpp
  // После заполнения массива в set_sched_times:
  for (uint8_t i = 0; i < count-1; i++)
    for (uint8_t j = 0; j < count-1-i; j++)
      if (times[j] > times[j+1]) { uint16_t tmp=times[j]; times[j]=times[j+1]; times[j+1]=tmp; }
  ```

---

### Low

#### 5. IRAM заполнен на 95% — опасная зона

- **File**: compilation output
- **Problem**: IRAM используется на 95% (62463/65536 байт). Это не новая проблема данного PR, но важный сигнал. Добавление новых функций в IRAM (например, ISR, `ICACHE_RAM_ATTR`) вызовет ошибку линковки.
- **Impact**: Нет влияния сейчас, но ограничивает дальнейшее развитие. При следующем крупном добавлении функционала может потребоваться аудит атрибутов IRAM.
- **Recommendation**: Мониторить этот показатель. Если превысит 98% — провести аудит функций с `ICACHE_RAM_ATTR`.

---

## Positive Patterns

1. **Дедупликация boot+schedule лога** — логика `if (!schedLog) { bootLog = true; _lastSchedLogMin = cur_min; }` корректно предотвращает двойную запись когда boot происходит ровно в расписанное время. Элегантное решение.

2. **EEPROM layout без перекрытий** — WIFI_PASS занимает 168–200 (33 байта), SCHED_MAGIC начинается с 201. Граница точная, места до EEPROM_SIZE=256 достаточно (219–255 = 37 байт свободных).

3. **`_keepalive()` vs `_activity()`** — чёткое разделение polling-активности и пользовательских действий правильно решает оба бага (подсветка + auto-sleep).

---

## Validation

- **Компиляция (ESP8266 NodeMCU)**: ✅ PASS
- **Предупреждения компилятора**: ✅ нет
- **IRAM**: ⚠️ 95% (62463/65536)
- **RAM global/static**: ✅ 46% (36960/80192)
- **Flash**: ✅ 57% (603076/1048576)
