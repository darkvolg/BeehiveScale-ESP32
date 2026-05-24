---
name: project_session_2026-05-20
description: Сессия 2026-05-18→20 — v5.0.30 hardware power-cut через AO3401 + DS3231 alarm. Финал борьбы с deep sleep WiFi багом.
metadata:
  type: project
---

# Session 2026-05-18→20 — Hardware Power-Cut (v5.0.30)

## Главное

После 6 версий программных фиксов deep sleep WiFi (v5.0.24→29) приняли решение **полностью отказаться от ESP32 sleep** в пользу **аппаратного отключения питания** через P-MOSFET управляемый DS3231 alarm.

## Что окончательно НЕ сработало (программно)

| Версия | Подход | Результат |
|---|---|---|
| v5.0.24 | WiFi reset перед connect | Wake#2 крашит |
| v5.0.25 | Усиленные delays + ESP.restart | Wake#2 крашит |
| v5.0.26 | esp_wifi_stop перед sleep + правильный порядок | Wake#2 крашит |
| v5.0.27/28 | CPU 240MHz + GPIO27 deinit + RTC retention + UART flush | Wake #3-4 OK, потом крах |
| v5.0.29 | Light sleep (без deep sleep) | 1 wake OK, потом RTCWDT boot loop |

Core пробовали: 3.0.7 (исходный, 100% крах), 2.0.17 (Arduino IDE OOM при компиляции), 3.1.3 (лучший, 80%), 3.3.8 (регрессия).

**Вывод 3 параллельных аудитов (Issue #2840, #9913, #7240 espressif/arduino-esp32):** manual light sleep с WiFi на Arduino-ESP32 принципиально не работает стабильно. Нужно либо `esp_pm_configure()` через ESP-IDF custom sdkconfig (большая переделка), либо HARDWARE отключение питания.

## v5.0.30 — Hardware Power-Cut

### Принцип
ESP32 физически выключается через **AO3401 P-MOSFET**. DS3231 alarm управляет MOSFET через диод. ESP сам себя удерживает включённым через **GPIO25 HOLD pin**.

### Цикл работы
1. **Idle:** 100k pullup тянет AO3401 Gate к +5V → MOSFET OFF → ESP без питания (0 мА).
2. **Wake-up:** DS3231 alarm fires → INT goes LOW → через диод D1 → Gate LOW → MOSFET ON → ESP boot.
3. **ESP в setup() ПЕРВАЯ инструкция:** `pinMode(25, OUTPUT_OPEN_DRAIN); digitalWrite(25, LOW);` → через диод D2 удерживает Gate LOW (HOLD active).
4. **ESP делает работу:** WiFi, TG, измерения, программирует DS3231 alarm на следующий момент.
5. **Sleep_enter:** `pinMode(25, INPUT)` → HOLD release → 100k pullup → Gate HIGH → MOSFET OFF → ESP теряет питание мгновенно.

Каждый boot = чистый POWERON_RESET. WiFi-стек никогда не страдает от wake-from-sleep багов.

### Hardware (доплата ~5 деталей)

| Деталь | Назначение |
|---|---|
| AO3401 P-MOSFET (SOT-23) | High-side switch |
| 100k резистор | Pullup Gate ↔ Source |
| 2× 1N5819 диоды | D1 (DS3231 INT), D2 (ESP GPIO25) — защита от 5V на ESP |
| Кнопка SW2 (push-button) | WAKE — ручной запуск |
| Тумблер SW1 (внешний) | Главный ON/OFF |

### Распиновка AO3401 (A19T, SOT-23)
- **Pin 3 (Drain)** — одиночная СВЕРХУ → к MT3608 VIN+
- **Pin 1 (Gate)** — нижняя ЛЕВАЯ → 100k pullup + D1 анод + D2 анод + SW2
- **Pin 2 (Source)** — нижняя ПРАВАЯ → к SW1 → TP4056 OUT+

### Размещение в существующей цепи
```
Solar → 1N5819 → TP4056 IN+
21700 ↔ TP4056 B+/B−
TP4056 OUT+ ──→ SW1 (тумблер) ──→ AO3401 S ──→ AO3401 D ──→ MT3608 VIN+ ──→ ESP/LCD/HX711
                                       │
                                       └──→ DS3231 VCC (от Source, через DW01 защиту)
```

### Файлы инструкций
- `hardware/POWER-CUT-INSTRUCTIONS.html` — полная инструкция (открывается в браузере, красиво форматировано)
- `hardware/POWER-CUT-INSTRUCTIONS.md` — markdown версия
- `hardware/schematic-hires.png` — high-res схема (2400×1600)
- `hardware/schematic-hires.svg` — SVG исходник
- `hardware/power-cut-final-v3.svg` — рабочая SVG с подписями

### Код v5.0.30

**Version.h** → 5.0.30

**RTC_Module.cpp/.h** — добавлены функции:
- `rtc_set_alarm_in_seconds(uint32_t)` — программирует DS3231 Alarm1 на N секунд от текущего времени
- `rtc_clear_alarm()` — clearAlarm(1) + clearAlarm(2)
- `rtc_enable_alarm_interrupt()` — `writeSqwPinMode(DS3231_OFF)` (включает INT mode вместо SQW)

**SleepManager.h** — добавлен `POWER_HOLD_PIN = 25` для ESP32.

**SleepManager.cpp `sleep_enter()`** полностью переделан:
1. Программирует DS3231 alarm
2. Clear alarm flag
3. Enable INTCN
4. WiFi.disconnect + esp_wifi_stop
5. Serial.flush + uart_wait_tx_idle_polling
6. `pinMode(POWER_HOLD_PIN, INPUT)` — отпускает Gate → MOSFET OFF → power off
7. Fallback: `esp_deep_sleep_start()` если железо не сработало

**BeehiveScale.ino setup()** — ПЕРВЫЕ ИНСТРУКЦИИ:
```cpp
pinMode(POWER_HOLD_PIN, OUTPUT_OPEN_DRAIN);
digitalWrite(POWER_HOLD_PIN, LOW);  // HOLD active — ESP держит сам себя
```
Это ДО Serial.begin, ДО WDT, ДО всего остального. Иначе после WAKE кнопки SW2 (отпустить) или alarm clear MOSFET закроется до того как ESP успеет встать.

### Боевая конфигурация
- DS3231 VCC от AO3401 Source (через SW1, после DW01 защиты). Когда SW1 OFF — DS3231 на CR2032 backup.
- GPIO33 (старый DS3231 INT) — отпаян, свободен.
- Новый провод от DS3231 INT/SQW → на доплате через D1 → AO3401 Gate.
- Новый провод от ESP GPIO25 → на доплате через D2 → AO3401 Gate.

### Автономия (расписание 8 wake/день)
- Sleep ток: ~150 µA (DS3231 only, MT3608 OFF тоже)
- Active: 0.5 мА·ч × 8 = 4 мА·ч/день
- Sleep: 24ч × 0.15мА = 3.6 мА·ч/день
- **Всего: ~8 мА·ч/день**
- 21700 4500 мА·ч / 8 = **~560 дней** автономии
- С солнечной 200мА → бесконечно в улье

### Тестирование

1. **Multimeter проверка платы** (4 теста):
   - Нет коротких VBAT/GND
   - 100k между G и S
   - Замкнуть CTRL_RTC к GND → VBAT_IN↔VBAT_OUT = 0Ω (MOSFET открывается)
   - Нажать SW2 → VBAT_IN↔VBAT_OUT = 0Ω

2. **Первый запуск**: SW1 ON → ничего не происходит → нажать SW2 → ESP boot → программирует alarm → выключается через ~15с.

3. **DS3231 alarm**: через 5 минут после первого выключения → ESP сам boot → следующий цикл.

### Что НЕ трогается
- MT3608, ESP DOIT V1, LCD, HX711, DS18B20, TP4056 — БЕЗ изменений
- Кнопки MAIN (GPIO27), MENU (GPIO26) — работают как раньше
- Wi-Fi код в Connectivity.cpp — без принципиальных изменений

## Status
- Hardware: user паяет доплату (AO3401 + 100k + 2× 1N5819 + SW2)
- Software: v5.0.30 готов к прошивке
- DS3231 INT отпаян от GPIO33, ждёт подключения на доплату

## Next session

1. User завершит пайку платы
2. Multimeter тесты пройдены
3. Прошивка v5.0.30
4. Тест WAKE кнопкой
5. Тест автоматического цикла через DS3231 alarm
6. Финальная сборка в IP65 корпус, в улей

---

## ⚠️ ИНЦИДЕНТ 2026-05-21 — старая ESP32 сгорела при пайке доплаты

### Что произошло
Геннадий допаивал power-cut доплату (AO3401 + 100k + диоды + SW2 кнопка) — последний провод от GPIO25. После завершения пайки:
- На VIN (5V pin): 4.6V — норма
- **На 3V3 pin: 4.1V (вместо 3.3V)** — критическое превышение
- 4.1V > max 3.6V для ESP32 → flash chip деградировал

### Диагностика
- VIN ↔ 3V3 = 0.5 МΩ → нет внутреннего короткого ESP32, чип жив
- 3V3 ↔ GND = 23 kΩ → нагрузки normal
- HT7333 LDO **пробит** при пайке доплаты (перегрев / ESD)
- Новый HT7333 впаян → всё равно 4.1V (странно, может уже flash убит)
- Геннадий вернул AMS1117 (родной для DOIT V1) → 3V3 OK (3.3V)
- НО прошивка не идёт: `Failed to communicate with the flash chip / Packet content transfer stopped`

### Результат
Старая ESP32 38-pin DOIT V1 = **труп**. Flash chip деградировал от 4.1V abuse.
Bootloader отвечает (chip ID видит), но flash write fail.

### Тест системы
Геннадий подключил ВТОРУЮ плату DOIT V1 (с USB-C, 30-pin) → **прошилась успешно**.
Значит USB + Arduino IDE + Windows работают. Проблема только в сожжённой 38-pin плате.

### Решение
Геннадий заказал новую **38-pin DOIT V1** (та же модель что старая). Завтра доставка.
Решил **НЕ перепаивать всё** на 30-pin плату — слишком много возни.
Оставляет железо как есть до прихода новой 38-pin.

### Что НЕ ТРОГАТЬ
- Доплата с AO3401 + диодами + резистором + SW2 кнопкой — оставлена готовая (паять не надо)
- Все провода периферии (HX711, LCD, DS3231, кнопки) — остались как были
- SS14 диод на солнечной заменён на новый (старый был пробит)
- HT7333 заменён на AMS1117-3.3 (родной, надёжнее)

## Plan для следующей сессии (когда придёт новая плата)

### 1. Подготовка
- Распаковать новую 38-pin ESP32 DOIT V1
- Multimeter проверка ПРЕД установкой:
  - VIN ↔ 3V3 = высокое (нет короткого)
  - 3V3 ↔ GND = нет короткого
  - Должна быть **чистая** плата

### 2. Перенос проводов со старой на новую (один-к-одному)
| GPIO | Что подключить |
|---|---|
| GPIO 4 | DS18B20 (температура) |
| GPIO 16 | HX711 DT |
| GPIO 17 | HX711 SCK |
| GPIO 21 | I2C SDA → DS3231 + LCD |
| GPIO 22 | I2C SCL → DS3231 + LCD |
| GPIO 25 | **HOLD от доплаты (новый, для power-cut)** |
| GPIO 26 | MENU кнопка |
| GPIO 27 | MAIN кнопка |
| GPIO 34 | Battery sense |
| VIN | 5V питание (от MT3608 VOUT через AO3401 на доплате) |
| GND | Общая земля |

### 3. Прошивка v5.0.30
1. Подключить только USB (без доплаты, без банки)
2. Arduino IDE → ESP32 Dev Module, Huge APP, Upload Speed 115200
3. Скетч → Загрузка (Ctrl+U)
4. **Должно прошиться без проблем** (новая чистая плата)
5. Boot: `[BeehiveScale v5.0.30] boot`

### 4. Тест ESP отдельно
- Подключить USB
- Boot OK
- WiFi подключается
- LCD работает (если периферия подключена)
- Кнопки работают

### 5. Подключение доплаты power-cut
- SW1 OFF (тумблер главный)
- Подключить VBAT_IN доплаты → TP4056 OUT+ через SW1
- Подключить VBAT_OUT → MT3608 VIN+
- Подключить CTRL_RTC → DS3231 INT/SQW
- Подключить CTRL_ESP → ESP GPIO25
- Подключить VCC_RTC → DS3231 VCC (DS3231 VCC отпаять от ESP 3V3!)
- Multimeter тесты доплаты:
  - VBAT_IN ↔ VBAT_OUT = ∞ (MOSFET закрыт)
  - Нажать SW2 → VBAT_IN ↔ VBAT_OUT = 0Ω
  - Замкнуть CTRL_RTC ↔ GND → VBAT_IN ↔ VBAT_OUT = 0Ω

### 6. Финальный тест
- SW1 ON
- Нажать SW2 → ESP boot → программирует DS3231 alarm → выключается через ~15с
- Через 5 мин → DS3231 alarm → ESP сам boot
- Если 24h auto-cycle работает → готово к улью

## Меры предосторожности на будущее (после инцидента)

1. **Заземление паяльника** обязательно — браслет ESD или клемма к корпусу
2. **НЕ подавать питание ДО завершения всей пайки** — сначала всё запаял, multimeter тест, потом USB
3. **Multimeter ПОСЛЕ каждой пайки** — поймать пробой LDO до того как ESP сгорит:
   - VIN должно быть 4.7-5.0V
   - **3V3 строго 3.2-3.4V** — если выше 3.6V → выключить НЕМЕДЛЕННО
4. **TVS diode на 3V3 rail** (SMAJ3.6CA) — срезает overvoltage спайки (опционально)
5. Паять подальше от LDO — минимум 5мм от корпуса HT7333/AMS1117

## Status

- v5.0.30 код **готов** (RTC alarm + GPIO25 HOLD + power-cut логика)
- Доплата спаяна и протестирована multimeter — рабочая
- Старая ESP32 = выкинуть (flash chip убит)
- Ждём доставку новой 38-pin ESP32 (завтра, 2026-05-22)
- Тогда: перенести провода → прошить → тест → улей
