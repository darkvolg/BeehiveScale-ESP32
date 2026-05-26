#ifndef ALERTS_H
#define ALERTS_H
// ─── Telegram alerts (v5.0.20) ─────────────────────────────────────────
// Проверяет батарею, температуру, RTC — отправляет уведомления в Telegram
// при срабатывании. Анти-спам: каждый алерт шлётся ОДИН РАЗ, пока условие
// сохраняется. Сбрасывается с гистерезисом (например, low-bat алерт
// сбросится только когда напряжение поднимется на 0.1В выше порога).

#include <Arduino.h>

void alerts_init();                    // вызвать в setup() после load_alerts
void alerts_check(float batV,          // текущее напряжение
                  int   batPct,        // процент заряда
                  float tempC,         // температура DS18B20
                  bool  rtcValid);     // false если RTC ошибка

// v5.0.52: детекция роения через резкое падение веса.
// Сравнивает текущий вес с весом прошлого замера (persist.lastWeight).
// Если падение > thresholdKg за интервал < maxSec → SWARM alert в TG.
// Anti-spam: один алерт за событие, сбрасывается при стабилизации.
void alerts_check_swarm(float currentW,        // текущий вес кг
                        float prevW,           // вес прошлого замера
                        uint32_t deltaSec,     // секунд с прошлого замера
                        float thresholdKg);    // порог потери (например 1.5 кг)

#endif
