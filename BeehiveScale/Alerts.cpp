#include "Alerts.h"
#include "Memory.h"
#include "Connectivity.h"

// Состояние «алерт уже послан» (anti-spam). Сбрасывается с гистерезисом.
// Живёт в RAM — при reboot сбросится, и при следующей проверке алерт
// уйдёт повторно если условие сохранилось. Это правильно — после reboot
// пасечник должен знать что батарея/температура всё ещё критичны.
static bool _batLowSent    = false;
static bool _tempLowSent   = false;
static bool _tempHighSent  = false;
static bool _rtcErrSent    = false;

// Кэш настроек (для скорости — читаем EEPROM один раз)
static AlertSettings _cfg;
static bool          _inited = false;

void alerts_init() {
  load_alerts(_cfg);
  _inited = true;
  // На свежем boot не считаем что был alert — даём шанс послать его заново
  _batLowSent = _tempLowSent = _tempHighSent = _rtcErrSent = false;
}

void alerts_check(float batV, int batPct, float tempC, bool rtcValid) {
  if (!_inited) return;

  // Перезагружаем настройки если поменялись через сайт (нечасто, но дёшево)
  // Можно убрать в продакшен для оптимизации, но удобно для UX
  load_alerts(_cfg);

  char msg[256];

  // ─── 1. Низкое напряжение батареи ─────────────────────────────────────
  if (_cfg.batLowV > 0.1f && batV > 0.1f) {  // 0.1В — защита от нулевых показаний при инициализации
    if (batV < _cfg.batLowV) {
      if (!_batLowSent) {
        snprintf(msg, sizeof(msg),
                 "🔋 <b>Низкая батарея!</b>\n"
                 "Напряжение: %.2f В (%d%%)\n"
                 "Порог: %.2f В\n"
                 "Пора зарядить аккумулятор.",
                 batV, batPct, _cfg.batLowV);
        if (tg_send_message(String(msg))) {
          _batLowSent = true;
        }
      }
    } else if (batV > _cfg.batLowV + 0.1f) {
      // Гистерезис 0.1В — сбрасываем флаг когда зарядилось
      _batLowSent = false;
    }
  }

  // ─── 2. Низкая температура ────────────────────────────────────────────
  // tempC > -90 → датчик валидный (-99 = ошибка чтения)
  if (_cfg.tempLow != 0.0f && tempC > -90.0f) {
    if (tempC < _cfg.tempLow) {
      if (!_tempLowSent) {
        snprintf(msg, sizeof(msg),
                 "❄️ <b>Низкая температура!</b>\n"
                 "Сейчас: %.1f °C\n"
                 "Порог: %.1f °C\n"
                 "Проверьте улей — возможно подмерзание.",
                 tempC, _cfg.tempLow);
        if (tg_send_message(String(msg))) {
          _tempLowSent = true;
        }
      }
    } else if (tempC > _cfg.tempLow + 2.0f) {
      _tempLowSent = false;
    }
  }

  // ─── 3. Высокая температура ───────────────────────────────────────────
  if (_cfg.tempHigh != 0.0f && tempC > -90.0f) {
    if (tempC > _cfg.tempHigh) {
      if (!_tempHighSent) {
        snprintf(msg, sizeof(msg),
                 "🔥 <b>Высокая температура!</b>\n"
                 "Сейчас: %.1f °C\n"
                 "Порог: %.1f °C\n"
                 "Возможен перегрев улья.",
                 tempC, _cfg.tempHigh);
        if (tg_send_message(String(msg))) {
          _tempHighSent = true;
        }
      }
    } else if (tempC < _cfg.tempHigh - 2.0f) {
      _tempHighSent = false;
    }
  }

  // ─── 4. RTC error (CR2032 села) ───────────────────────────────────────
  if (_cfg.rtcEn) {
    if (!rtcValid) {
      if (!_rtcErrSent) {
        snprintf(msg, sizeof(msg),
                 "⏰ <b>RTC ошибка!</b>\n"
                 "Часы реального времени сбились.\n"
                 "Возможно села CR2032 на модуле DS3231 — замените батарейку.\n"
                 "Расписание замеров может работать некорректно.");
        if (tg_send_message(String(msg))) {
          _rtcErrSent = true;
        }
      }
    } else {
      _rtcErrSent = false;
    }
  }
}
