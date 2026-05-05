#ifndef BATTERY_H
#define BATTERY_H

#include <Arduino.h>

#if defined(ESP8266)
#define BAT_PIN              A0
#define BAT_ADC_MAX          1023.0f
#else
#define BAT_PIN              34
#define BAT_ADC_MAX          4095.0f
#endif
#define BAT_DIVIDER_RATIO    2.0f      // R1=R2 (100K+100K) — напряжение делится на 2
#define BAT_VMAX             4.2f
#define BAT_VMIN             3.0f
#define BAT_EMA_ALPHA        0.1f
#define BAT_READ_INTERVAL_MS 10000UL

void  bat_init();
float bat_voltage();   // Напряжение в вольтах (сглаженное)
int   bat_percent();   // Процент заряда 0-100

#endif
