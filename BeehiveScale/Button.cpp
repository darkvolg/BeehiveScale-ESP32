#include "Button.h"

static const unsigned long DEBOUNCE_MS = 50;
// v5.0.3: пороги удержания теперь параметры read_button() — у MAIN и MENU свои значения.
//   MAIN:  mediumMs=3000 (тара), longMs=6000 (калибровка)
//   MENU:  mediumMs=0 (отключён), longMs=1500 (возврат на 1/8)

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

unsigned long button_hold_ms(const ButtonState &state) {
  if (!state.isPressed) return 0;
  unsigned long now = millis();
  if (now < state.pressStart) return 0;
  return now - state.pressStart;
}

ButtonAction read_button(int pin, ButtonState &state,
                         unsigned long mediumMs,
                         unsigned long longMs) {
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
    // v5.0.6: кнопка ещё нажата — фиксируем pressStart на момент irqTime,
    // чтобы реальное удержание считалось от физического нажатия, а не от
    // момента detection (loop мог быть заблокирован process_weight на 500+мс).
    // Это критично для MEDIUM/LONG срабатывания при долгих блокировках loop.
    if (now - irqT >= DEBOUNCE_MS) {
      state.pressStart = irqT;
      state.isPressed = true;
      state.longFired = false;
      state.lastRaw = true;
      state.lastChangeTime = irqT;
      // Сразу проверяем не дотянули ли уже до longMs
      if ((now - state.pressStart) >= longMs) {
        state.longFired = true;
        return LONG_PRESS;
      }
      return NO_ACTION;
    }
    // Иначе — обработаем через обычный debounce ниже
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
             (now - state.pressStart >= longMs)) {
    state.longFired = true;
    return LONG_PRESS;
  } else if (!stable && state.isPressed) {
    state.isPressed = false;
    if (state.longFired) {
      return NO_ACTION;  // LONG уже отстрелил — release ничего не делает
    }
    unsigned long heldMs = now - state.pressStart;
    if (mediumMs > 0 && heldMs >= mediumMs) {
      return MEDIUM_PRESS;
    }
    return SHORT_PRESS;
  }
  return NO_ACTION;
}
