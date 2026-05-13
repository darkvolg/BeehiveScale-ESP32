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

#endif
