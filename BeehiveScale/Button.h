#ifndef BUTTON_H
#define BUTTON_H

#include <Arduino.h>

enum ButtonAction { NO_ACTION, SHORT_PRESS, LONG_PRESS };

struct ButtonState {
  unsigned long pressStart = 0;
  unsigned long lastChangeTime = 0;
  bool lastRaw = false;
  bool isPressed = false;
  bool longFired = false;
  // Interrupt-based: ISR ставит флаг при FALLING edge
  volatile bool irqFell = false;
  volatile unsigned long irqTime = 0;
};

ButtonAction read_button(int pin, ButtonState &state);
void button_attach_interrupt(int pin, ButtonState &state);

#endif
