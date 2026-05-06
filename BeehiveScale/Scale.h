#ifndef SCALE_H
#define SCALE_H

#include <HX711.h>

#define SENSOR_READY_TIMEOUT_MS 1500
// v5.0.6: 5→2 семпла. EMA в process_weight() уже сглаживает, 5 семплов блокировали
// loop ~500мс (10Hz HX711) — это ломало кнопочный отклик. 2 семпла = ~200мс.
// Калибровка по-прежнему 20 семплов для точности (SCALE_CALIB_SAMPLES).
#define SCALE_READ_SAMPLES 2
#define SCALE_CALIB_SAMPLES 20

void scale_init(HX711 &scale, int dtPin, int sckPin);
bool check_sensor(HX711 &scale);
float scale_read_weight(HX711 &scale, int samples = SCALE_READ_SAMPLES);

#endif
