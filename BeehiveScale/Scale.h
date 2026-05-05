#ifndef SCALE_H
#define SCALE_H

#include <HX711.h>

#define SENSOR_READY_TIMEOUT_MS 1500
#define SCALE_READ_SAMPLES 5
#define SCALE_CALIB_SAMPLES 20

void scale_init(HX711 &scale, int dtPin, int sckPin);
bool check_sensor(HX711 &scale);
float scale_read_weight(HX711 &scale, int samples = SCALE_READ_SAMPLES);

#endif
