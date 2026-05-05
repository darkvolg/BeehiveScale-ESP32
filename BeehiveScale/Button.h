#ifndef BUTTON_H
#define BUTTON_H

#include <Arduino.h>

enum ButtonAction { NO_ACTION, SHORT_PRESS, MEDIUM_PRESS, LONG_PRESS };

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

// mediumMs=0 → MEDIUM_PRESS отключён для этой кнопки.
// Defaults рассчитаны на MAIN-кнопку (тара/калибровка). Для MENU передавать (0, 1500).
ButtonAction read_button(int pin, ButtonState &state,
                         unsigned long mediumMs = 3000,
                         unsigned long longMs   = 6000);
void button_attach_interrupt(int pin, ButtonState &state);

// Сколько миллисекунд кнопка удерживается прямо сейчас (0 если не нажата).
unsigned long button_hold_ms(const ButtonState &state);

#endif
