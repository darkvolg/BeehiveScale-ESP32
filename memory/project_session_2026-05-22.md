---
name: project_session_2026-05-22
description: Сессия 2026-05-22 — установка power-cut платы на новой ESP32, цикл работает но MOSFET не закрывается полностью, попытки v5.0.30-33
metadata:
  type: project
---

# Session 2026-05-22 — Hardware Power-Cut установка + проблема GPIO25 leakage

## Главное достижение

✅ **Auto-cycle работает 14+ wake подряд** без сбоев на новой ESP32 38-pin + Core 3.1.3. Старая проблема wake-from-deep-sleep WiFi crash **исчезла**. Каждый wake = чистый WiFi connect за 2с, TG ОК, DS3231 alarm срабатывает.

## Главная проблема (НЕ решена)

❌ **Hardware power-cut не закрывает MOSFET полностью**. ESP уходит в fallback deep sleep (~6 мА) вместо физического OFF (0 мА).

### Корень проблемы (диагностирован)
- **GPIO25 в deep sleep = 0V** вместо HIGH-Z
- ESP32 RTC GPIO peripheral продолжает driving LOW через D2 → Gate AO3401 застрял на 2.8V вместо 4V (с 100k pullup) или 0.1V (с 10k pullup)
- Vgs = marginal threshold → MOSFET частично проводит → 3.8V на VIN ESP

## Hardware конфигурация (текущая)

### Доплата power-cut собрана:
- AO3401 P-MOSFET (A19T, SOT-23): D=Pin3 верх, G=Pin1 низ-лево, S=Pin2 низ-право
- **10kΩ pullup** между Gate и Source (изначально 100k, потом заменили)
- D1 1N5819: катод → DS3231 SQW pin, анод → AO3401 Gate
- D2 1N5819: катод → ESP GPIO25 (D25), анод → AO3401 Gate
- SW2 push-button: между Gate и GND (manual WAKE)
- SW1 главный тумблер (внешний, между TP4056 OUT+ и нашей платой)

### Подключение:
- TP4056 OUT+ → SW1 → AO3401 Source
- AO3401 Drain → MT3608 VIN+
- MT3608 VOUT+ → ESP VIN + LCD/HX711 5V
- DS3231 VCC → AO3401 Source (always-on после SW1)
- DS3231 SQW → CTRL_RTC → D1 → Gate
- ESP GPIO25 (D25) → CTRL_ESP → D2 → Gate
- Battery divider → MT3608 VIN+ (после AO3401, отключается)

### DS3231 модуль: HW-084 (не ZS-042!)
- Распиновка: 32K, SQW, SCL, SDA, VCC, GND (один ряд)
- SQW pin работает как alarm INT когда INTCN=1
- **Стеклянный diode рядом с чипом** = trickle charge для LIR2032. Опционально снять (мы используем non-rechargeable CR2032).
- Power LED на module возможно припаян (жрёт 1-2 мА)

## Версии прошивки в сессии

### v5.0.30 — первая power-cut implementation
- RTC_Module: add `rtc_set_alarm_in_seconds`, `rtc_clear_alarm`, `rtc_enable_alarm_interrupt`
- SleepManager: программирует DS3231 alarm + `pinMode(POWER_HOLD_PIN, INPUT)` для release HOLD
- setup(): `pinMode(POWER_HOLD_PIN, OUTPUT_OPEN_DRAIN); digitalWrite(LOW)` ПЕРВЫМ
- **POWER_HOLD_PIN = GPIO25**

### v5.0.31 — NTP + ручная установка времени
- NTP sync ВКЛЮЧЁН обратно (Core 3.1.3 fix баг Core 3.0.7)
- `/api/rtc/set` POST endpoint для ручной установки времени
- `secrets.h`: SSID **"Beeline"** (с большой) вместо "beeline"

### v5.0.32 — rtc_gpio_isolate
- `rtc_gpio_isolate(GPIO_NUM_25)` перед deep sleep
- НЕ сработало — GPIO25 всё равно 0V в sleep

### v5.0.33 (текущий, ЖДЁТ ПРОШИВКИ)
- Добавлен `rtc_gpio_init()` ПЕРЕД isolate (без init RTC domain не активирован)
- GPIO25 переведён в **OUTPUT HIGH** через RTC GPIO + `gpio_hold_en` + `gpio_deep_sleep_hold_en`
- setup() очищает hold + `rtc_gpio_deinit` для возврата в digital domain

## Что проверено мультиметром

### В active (ESP работает через USB или после WAKE):
- Gate AO3401: 0.1V (HOLD active через GPIO25 LOW + D2 forward)
- Source: 4.1V
- VIN ESP: ~5V (MOSFET ON)

### В sleep (после auto-sleep, USB отключён):
- DS3231 SQW: **4.1V (HIGH)** ✅ (не виноват)
- SW2 кнопка: ∞ норма (не залипла)
- D1, D2 диоды: 0.2V forward, ∞ reverse — норма
- **ESP GPIO25: 0V** ❌ (главная проблема)
- Gate: 0.1-0.2V (тянется через D2 forward от GPIO25)
- Source: 4.1V
- MT3608 VIN+: 3.8V (MOSFET частично проводит)
- MT3608 VOUT+: 5V (boost работает)

### Расчёт утечки (со 100k pullup):
- Gate = 2.8V, drop = 1.2V, I = 120 µA через pullup
- Эквивалент утечка к GND = 23 kΩ через RTC GPIO peripheral

## Инциденты в сессии

### 1. Старая ESP32 умерла
HT7333 LDO сгорел при пайке → 4.1V на 3V3 → flash chip деградировал. ESP отвечает в bootloader но flash write fail. Заменили на новую плату.

### 2. SS14 диод был пробит
В цепи solar — заменён на новый. После замены питание восстановилось.

### 3. AMS1117 вернули вместо HT7333
HT7333 (даже новый) выдавал 4.1V на 3V3 → может была неправильная распиновка или некачественный chip. Поставили родной AMS1117-3.3 → 3.3V норма.

### 4. SSID был "beeline" (с маленькой) — не находился
Поправили в secrets.h на "Beeline" (с большой). После прошивки v5.0.31 — auto-connect к WiFi.

### 5. DS3231 lost power flag
После замены ESP + перепайки DS3231 потерял VCC временно → flag установился. После NTP sync (v5.0.31) flag сбрасывается автоматически.

## План для следующей сессии

### Шаг 1 — прошить v5.0.33
1. USB к ESP
2. Закрыть Serial Monitor
3. Ctrl+U → загрузка
4. После прошивки **отключить USB**
5. Banка + SW1 ON → нажать SW2 WAKE → boot
6. Жди 2 мин idle → ESP должна уйти в sleep

### Шаг 2 — multimeter проверки в sleep
- **GPIO25 (D25)** должно быть **~3.3V** (с v5.0.33 OUTPUT HIGH + hold)
- **Gate AO3401** должно быть **~3.5V** (через D2 forward от 3.3V GPIO)
- **MT3608 VIN+** должно быть **0V** или близко (MOSFET закрыт)
- **MT3608 VOUT+** должно быть **0V**

### Шаг 3 — если v5.0.33 НЕ помогло
Остаются варианты:
A. **Сильный pullup** — заменить 10k на 2.2k или 1k (стянет Gate к 4V даже при утечке)
B. **NPN buffer (BC547)** — паять между GPIO25 и Gate, изолировать ESP полностью от Gate
C. **Снять D2** — оставить только DS3231 control (нет HOLD через GPIO25). Тогда WAKE кнопка работает но ESP не может сам себя держать — DS3231 alarm fires на 5+ сек чтобы успеть boot до закрытия MOSFET.
D. **Принять как есть** — auto-cycle работает, ESP в fallback deep sleep 6 мА → автономия 60 дней вместо 500+. Хороший компромисс если других путей нет.

### Шаг 4 — оптимизация HW-084 module
- Удалить **trickle charge diode** (стеклянный рядом с DS3231 chip) — для long-term CR2032
- Удалить **Power LED** на module (если есть) — экономия 1-2 мА always-on

## Что НЕ требует переделки

✅ Доплата power-cut спаяна, диагностирована, работает (частично — wake OK, sleep marginal)
✅ ESP32 новая 38-pin DOIT V1 — здорова
✅ AMS1117 родной LDO — работает 3.3V
✅ SS14 новый в solar цепи
✅ Все провода периферии (HX711, LCD, DS3231, DS18B20, кнопки MAIN/MENU, battery sense)
✅ DS3231 INT/SQW → правильно подключён к D1
✅ SW1 тумблер, SW2 кнопка — работают
✅ WiFi connect "Beeline" — стабильно
✅ NTP sync на boot — работает
✅ Auto-cycle 14+ wakes без сбоя

## Текущее настройки (с сайта)

- Порог тревоги TG: 0.5 кг
- Эталонный груз: 1000 г
- Скорость отклика: 0.3
- **Deep Sleep интервал**: 300с (5 мин) для теста
- Расписание: пусто
- Auto-sleep: 120с idle
- LCD timeout: 30с

## Status

| Компонент | Статус |
|---|---|
| Auto-cycle WiFi+DS3231 wake | ✅ работает (14+ циклов без сбоя) |
| RTC время | ✅ NTP sync OK |
| TG отправка | ✅ работает |
| Hardware power-cut | ⚠️ частично — fallback deep sleep |
| GPIO25 control в sleep | ❌ застрял 0V (RTC peripheral leakage) |
| Автономия | ~60 дней (deep sleep) — приемлемо для тестов |

## Файлы доплаты

- `hardware/POWER-CUT-INSTRUCTIONS.html` — инструкция пайки
- `hardware/schematic-hires.png` — high-res схема
- `hardware/power-cut-final-v3.svg` — SVG с подписями

## Память — что брать в следующую сессию

ПРОЧИТАТЬ ПЕРВЫМ:
1. **Этот файл** (project_session_2026-05-22.md)
2. project_session_2026-05-20.md (предыдущий ход)
3. project_session_2026-05-18.md (баг wake-from-sleep WiFi — решён сменой ESP)

В коде ВАЖНО:
- v5.0.33 готов, **жди прошивки + теста GPIO25**
- POWER_HOLD_PIN = GPIO25 (D25 на плате)
- Если GPIO25 ОК в sleep → power-cut работает → готово к улью
- Если нет — план B (BC547 NPN buffer) или принять deep sleep fallback
