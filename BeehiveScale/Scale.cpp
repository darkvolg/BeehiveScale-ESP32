#include "Scale.h"
#include <math.h>

void scale_init(HX711 &scale, int dtPin, int sckPin) {
  scale.begin(dtPin, sckPin);
  // GPIO14 (D5) поддерживает INPUT_PULLUP — это критично для стабильности DOUT.
  // На предыдущей распиновке (GPIO16) pull-up был невозможен, отсюда плавающий вес.
  pinMode(dtPin, INPUT_PULLUP);
}

// Power-cycle HX711: SCK HIGH >60us = power down, затем LOW = power up.
static void scale_power_cycle(HX711 &scale) {
  scale.power_down();
  delayMicroseconds(100);
  scale.power_up();
  delay(400);  // HX711 нужно ~400мс на стабилизацию после включения
}

bool check_sensor(HX711 &scale) {
  bool ready = scale.wait_ready_timeout(1500);
  if (!ready) {
    scale_power_cycle(scale);
    ready = scale.wait_ready_timeout(1500);
  }
  return ready;
}

float scale_read_weight(HX711 &scale, int samples) {
  if (!scale.wait_ready_timeout(1000)) {
    return NAN;
  }
  return scale.get_units(samples);
}
