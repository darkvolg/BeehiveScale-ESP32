#include "Button.h"

static const unsigned long DEBOUNCE_MS = 50;
static const unsigned long LONG_PRESS_MS = 5000;  // 5 сек — для запуска калибровки и "домой" из меню

// ISR хранилище — максимум 2 кнопки
static ButtonState* _isrState[2] = {nullptr, nullptr};

static void IRAM_ATTR _isr0() {
  if (_isrState[0]) {
    _isrState[0]->irqFell = true;
    _isrState[0]->irqTime = millis();
  }
}
static void IRAM_ATTR _isr1() {
  if (_isrState[1]) {
    _isrState[1]->irqFell = true;
    _isrState[1]->irqTime = millis();
  }
}

void button_attach_interrupt(int pin, ButtonState &state) {
  static int isrIdx = 0;
  if (isrIdx >= 2) return;
  _isrState[isrIdx] = &state;
  attachInterrupt(digitalPinToInterrupt(pin), isrIdx == 0 ? _isr0 : _isr1, FALLING);
  isrIdx++;
}

ButtonAction read_button(int pin, ButtonState &state) {
  bool raw = (digitalRead(pin) == LOW);
  unsigned long now = millis();

  // Если ISR поймал нажатие пока loop был занят — подхватываем
  noInterrupts();
  bool fell = state.irqFell;
  unsigned long irqT = state.irqTime;
  state.irqFell = false;
  interrupts();

  if (fell && !state.isPressed) {
    // ISR зафиксировал FALLING edge — кнопка была нажата
    // Проверяем что сейчас она уже отпущена (или ещё нажата)

    if (!raw) {
      // Кнопка уже отпущена — короткое нажатие произошло между опросами
      if (now - irqT >= DEBOUNCE_MS) {
        // Достаточно времени прошло — считаем валидным
        return SHORT_PRESS;
      }
      // Слишком короткий — дребезг, игнорируем
      return NO_ACTION;
    }
    // Кнопка ещё нажата — обработаем через обычный debounce ниже
  }

  // Стандартный debounce по опросу
  if (raw != state.lastRaw) {
    state.lastRaw = raw;
    state.lastChangeTime = now;
  }

  if (now - state.lastChangeTime < DEBOUNCE_MS) {
    return NO_ACTION;
  }

  bool stable = state.lastRaw;

  if (stable && !state.isPressed) {
    state.pressStart = now;
    state.isPressed = true;
    state.longFired = false;
    state.irqFell = false; // сбросить — мы уже обрабатываем
  } else if (stable && state.isPressed && !state.longFired &&
             (now - state.pressStart >= LONG_PRESS_MS)) {
    state.longFired = true;
    return LONG_PRESS;
  } else if (!stable && state.isPressed) {
    state.isPressed = false;
    if (!state.longFired) {
      return SHORT_PRESS;
    }
  }
  return NO_ACTION;
}
