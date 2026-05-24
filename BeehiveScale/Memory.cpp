#include "Memory.h"
#include <math.h>

// Статические переменные для хранения настроек
static float _alertDelta = 0.5f;
static float _calibWeight = 1000.0f;
static float _emaAlpha = 0.3f;  // дефолт: 0.3 = быстрый отклик ~10 сек, всё ещё фильтрует пчёл
static bool _settingsLoaded = false;

static bool is_eeprom_valid() {
  byte magic = 0;
  EEPROM.get(EEPROM_ADDR_MAGIC, magic);
  return (magic == EEPROM_MAGIC_VALUE);
}

static void mark_eeprom_valid() {
  byte magic = EEPROM_MAGIC_VALUE;
  EEPROM.put(EEPROM_ADDR_MAGIC, magic);
}

// Инициализация настроек веб-сервера
void web_settings_init() {
  if (_settingsLoaded) return;

  byte magic2 = 0;
  EEPROM.get(EEPROM_ADDR_MAGIC2, magic2);

  if (magic2 != EEPROM_MAGIC2_VALUE) {
    _alertDelta = 0.5f;
    _calibWeight = 1000.0f;
    _emaAlpha = 0.3f;
  } else {
    EEPROM.get(EEPROM_ADDR_ALERT_DELTA, _alertDelta);
    EEPROM.get(EEPROM_ADDR_CALIB_WEIGHT, _calibWeight);
    EEPROM.get(EEPROM_ADDR_EMA_ALPHA, _emaAlpha);

    if (isnan(_alertDelta) || _alertDelta < 0.1f || _alertDelta > 10.0f) _alertDelta = 0.5f;
    if (isnan(_calibWeight) || _calibWeight < 100.0f || _calibWeight > 50000.0f) _calibWeight = 1000.0f;
    if (isnan(_emaAlpha) || _emaAlpha < 0.05f || _emaAlpha > 0.9f) _emaAlpha = 0.3f;
  }
  _settingsLoaded = true;
}

float web_get_alert_delta() {
  if (!_settingsLoaded) web_settings_init();
  return _alertDelta;
}

float web_get_calib_weight() {
  if (!_settingsLoaded) web_settings_init();
  return _calibWeight;
}

float web_get_ema_alpha() {
  if (!_settingsLoaded) web_settings_init();
  return _emaAlpha;
}

void load_calibration_data(float &factor, long &offset, float &weight) {
  if (!is_eeprom_valid()) {
    factor = 2280.0f;
    offset = 0;
    weight = 0.0f;
    EEPROM.put(EEPROM_ADDR_CALIB, factor);
    EEPROM.put(EEPROM_ADDR_OFFSET, offset);
    EEPROM.put(EEPROM_ADDR_WEIGHT, weight);
    mark_eeprom_valid();
    EEPROM.commit();
    return;
  }

  EEPROM.get(EEPROM_ADDR_CALIB, factor);
  EEPROM.get(EEPROM_ADDR_OFFSET, offset);
  EEPROM.get(EEPROM_ADDR_WEIGHT, weight);

  if (isnan(factor) || factor < 100.0f || factor > 100000.0f) factor = 2280.0f;
  if (offset < -16777216L || offset > 16777216L) offset = 0;
  if (isnan(weight) || weight < 0.0f) weight = 0.0f;
}

void save_calibration(float factor) {
  EEPROM.put(EEPROM_ADDR_CALIB, factor);
  mark_eeprom_valid();
  EEPROM.commit();
}

void save_offset(long offset) {
  EEPROM.put(EEPROM_ADDR_OFFSET, offset);
  mark_eeprom_valid();
  EEPROM.commit();
}

void save_weight(float &lastWeight, float currentWeight) {
  lastWeight = currentWeight;
  EEPROM.put(EEPROM_ADDR_WEIGHT, lastWeight);
  EEPROM.commit();
}

void save_calibration_block(float factor, long offset, float weight,
                            float prevWeight, long prevOffset) {
  if (!isnan(factor) && factor >= 100.0f && factor <= 100000.0f) {
    EEPROM.put(EEPROM_ADDR_CALIB, factor);
  }
  if (offset > -16777216L && offset < 16777216L) {
    EEPROM.put(EEPROM_ADDR_OFFSET, offset);
  }
  if (!isnan(weight) && weight >= 0.0f && weight <= 500.0f) {
    EEPROM.put(EEPROM_ADDR_WEIGHT, weight);
  }
  if (!isnan(prevWeight) && prevWeight >= 0.0f && prevWeight <= 500.0f) {
    EEPROM.put(EEPROM_ADDR_PREV_WEIGHT, prevWeight);
  }
  if (prevOffset > -16777216L && prevOffset < 16777216L) {
    EEPROM.put(EEPROM_ADDR_PREV_OFFSET, prevOffset);
  }
  byte magic = EEPROM_MAGIC_VALUE;
  EEPROM.put(EEPROM_ADDR_MAGIC, magic);
  EEPROM.commit();
}

void load_web_settings(float &alertDelta, float &calibWeight, float &emaAlpha) {
  if (!_settingsLoaded) web_settings_init();
  alertDelta = _alertDelta;
  calibWeight = _calibWeight;
  emaAlpha = _emaAlpha;
}

void save_prev_offset(long prevOffset) {
  EEPROM.put(EEPROM_ADDR_PREV_OFFSET, prevOffset);
  EEPROM.commit();
}

long load_prev_offset() {
  long val = 0;
  EEPROM.get(EEPROM_ADDR_PREV_OFFSET, val);
  if (val < -16777216L || val > 16777216L) val = 0;
  return val;
}

void save_prev_weight(float w) {
  EEPROM.put(EEPROM_ADDR_PREV_WEIGHT, w);
  EEPROM.commit();
}

float load_prev_weight(float fallback) {
  float w = 0.0f;
  EEPROM.get(EEPROM_ADDR_PREV_WEIGHT, w);
  // Отбраковываем мусор: улей не может весить < 0.1 кг
  if (isnan(w) || isinf(w) || w < 0.1f || w > 500.0f) return fallback;
  return w;
}

void save_prev_weight_date(uint32_t unixtime) {
  EEPROM.put(EEPROM_ADDR_PREV_DATE, unixtime);
  EEPROM.commit();
}

uint32_t load_prev_weight_date() {
  uint32_t ts = 0;
  EEPROM.get(EEPROM_ADDR_PREV_DATE, ts);
  // 1546300800 = 2019-01-01, до этого даты невалидны (RTC не настроен)
  if (ts < 1546300800UL) return 0;
  return ts;
}

uint16_t get_autosleep_sec() {
  byte magic = 0;
  EEPROM.get(EEPROM_ADDR_AUTOSLEEP_MAGIC, magic);
  if (magic != EEPROM_MAGIC_AUTOSLEEP_VALUE) return 180;  // дефолт 3 минуты
  uint16_t sec = 180;
  EEPROM.get(EEPROM_ADDR_AUTOSLEEP, sec);
  if (sec > 86400) return 180;  // защита от мусора (>24ч = странно)
  return sec;
}

void set_autosleep_sec(uint16_t sec) {
  if (sec > 86400) sec = 86400;
  byte magic = EEPROM_MAGIC_AUTOSLEEP_VALUE;
  EEPROM.put(EEPROM_ADDR_AUTOSLEEP, sec);
  EEPROM.put(EEPROM_ADDR_AUTOSLEEP_MAGIC, magic);
  EEPROM.commit();
}

void save_web_settings(float alertDelta, float calibWeight, float emaAlpha) {
  EEPROM.put(EEPROM_ADDR_ALERT_DELTA, alertDelta);
  EEPROM.put(EEPROM_ADDR_CALIB_WEIGHT, calibWeight);
  EEPROM.put(EEPROM_ADDR_EMA_ALPHA, emaAlpha);
  byte magic2 = EEPROM_MAGIC2_VALUE;
  EEPROM.put(EEPROM_ADDR_MAGIC2, magic2);
  EEPROM.commit();

  _alertDelta = alertDelta;
  _calibWeight = calibWeight;
  _emaAlpha = emaAlpha;
  _settingsLoaded = true;
}

// ─── Расширенные настройки (sleep, LCD BL, AP pass) ──────────────────────
static uint32_t _sleepSec    = 900UL;
static uint16_t _lcdBlSec    = 30;
static char     _apPass[24]  = "12345678";
static bool     _extLoaded   = false;

void ext_settings_init() {
  if (_extLoaded) return;
  // magic3 по адресу 64 (после AP_PASS[24]: 40..63)
  byte magic3 = 0;
  EEPROM.get(EEPROM_ADDR_MAGIC3, magic3);
  if (magic3 != EEPROM_MAGIC3_VALUE) {
    _sleepSec = 900UL;
    _lcdBlSec = 30;
    strncpy(_apPass, "12345678", sizeof(_apPass));
  } else {
    EEPROM.get(EEPROM_ADDR_SLEEP_SEC, _sleepSec);
    EEPROM.get(EEPROM_ADDR_LCD_BL_SEC, _lcdBlSec);
    EEPROM.get(EEPROM_ADDR_AP_PASS, _apPass);
    _apPass[23] = '\0';  // гарантируем нуль-терминатор

    if (_sleepSec < 30UL || _sleepSec > 86400UL) _sleepSec = 900UL;
    if (_lcdBlSec > 3600) _lcdBlSec = 30;
    if (_apPass[0] == '\0' || strlen(_apPass) < 8) strncpy(_apPass, "12345678", sizeof(_apPass));
  }
  _extLoaded = true;
}

static void _ext_save() {
  EEPROM.put(EEPROM_ADDR_SLEEP_SEC, _sleepSec);
  EEPROM.put(EEPROM_ADDR_LCD_BL_SEC, _lcdBlSec);
  EEPROM.put(EEPROM_ADDR_AP_PASS, _apPass);
  byte magic3 = EEPROM_MAGIC3_VALUE;
  EEPROM.put(EEPROM_ADDR_MAGIC3, magic3);
  EEPROM.commit();
}

uint32_t get_sleep_sec() { if (!_extLoaded) ext_settings_init(); return _sleepSec; }
void set_sleep_sec(uint32_t sec) {
  if (sec < 30UL || sec > 86400UL) return;
  _sleepSec = sec; _ext_save();
}

uint16_t get_lcd_bl_sec() { if (!_extLoaded) ext_settings_init(); return _lcdBlSec; }
void set_lcd_bl_sec(uint16_t sec) {
  if (sec > 3600) return;
  _lcdBlSec = sec; _ext_save();
}

void get_ap_pass(char *buf, size_t maxLen) {
  if (!_extLoaded) ext_settings_init();
  strncpy(buf, _apPass, maxLen - 1);
  buf[maxLen - 1] = '\0';
}

void set_ap_pass(const char *pass) {
  if (!pass || strlen(pass) < 8 || strlen(pass) > 23) return;
  strncpy(_apPass, pass, sizeof(_apPass) - 1);
  _apPass[sizeof(_apPass) - 1] = '\0';
  _ext_save();
}

void set_ext_all(uint32_t sleepSec, uint16_t lcdBlSec, const char *apPass) {
  if (!_extLoaded) ext_settings_init();
  if (sleepSec >= 30UL && sleepSec <= 86400UL) _sleepSec = sleepSec;
  if (lcdBlSec <= 3600) _lcdBlSec = lcdBlSec;
  if (apPass && strlen(apPass) >= 8 && strlen(apPass) <= 23) {
    strncpy(_apPass, apPass, sizeof(_apPass) - 1);
    _apPass[sizeof(_apPass) - 1] = '\0';
  }
  _ext_save();
}

// ─── Telegram настройки ───────────────────────────────────────────────────
static char _tgToken[50]  = "";
static char _tgChatId[16] = "";
static bool _tgLoaded     = false;

void tg_settings_init() {
  if (_tgLoaded) return;
  byte magic = 0;
  EEPROM.get(EEPROM_ADDR_TG_MAGIC, magic);
  if (magic != EEPROM_MAGIC_TG_VALUE) {
    _tgToken[0]  = '\0';
    _tgChatId[0] = '\0';
  } else {
    EEPROM.get(EEPROM_ADDR_TG_TOKEN,  _tgToken);
    EEPROM.get(EEPROM_ADDR_TG_CHATID, _tgChatId);
    _tgToken[49]  = '\0';
    _tgChatId[15] = '\0';
  }
  _tgLoaded = true;
}

void get_tg_token(char *buf, size_t maxLen) {
  if (!_tgLoaded) tg_settings_init();
  strncpy(buf, _tgToken, maxLen - 1);
  buf[maxLen - 1] = '\0';
}

// Записывает TG-блок в EEPROM-буфер БЕЗ commit (для batch-сохранения)
static void _tg_write() {
  EEPROM.put(EEPROM_ADDR_TG_TOKEN,  _tgToken);
  EEPROM.put(EEPROM_ADDR_TG_CHATID, _tgChatId);
  byte magic = EEPROM_MAGIC_TG_VALUE;
  EEPROM.put(EEPROM_ADDR_TG_MAGIC, magic);
}

void set_tg_token(const char *token) {
  if (!token) return;
  strncpy(_tgToken, token, sizeof(_tgToken) - 1);
  _tgToken[sizeof(_tgToken) - 1] = '\0';
  _tg_write();
  EEPROM.commit();
}

void get_tg_chatid(char *buf, size_t maxLen) {
  if (!_tgLoaded) tg_settings_init();
  strncpy(buf, _tgChatId, maxLen - 1);
  buf[maxLen - 1] = '\0';
}

void set_tg_chatid(const char *chatid) {
  if (!chatid) return;
  strncpy(_tgChatId, chatid, sizeof(_tgChatId) - 1);
  _tgChatId[sizeof(_tgChatId) - 1] = '\0';
  _tg_write();
  EEPROM.commit();
}

void tg_commit() {
  _tg_write();
  EEPROM.commit();
}

void set_tg_all(const char *token, const char *chatid) {
  if (!_tgLoaded) tg_settings_init();
  if (!token && !chatid) return;  // ничего не менять если оба NULL
  if (token) { strncpy(_tgToken, token, sizeof(_tgToken) - 1); _tgToken[sizeof(_tgToken) - 1] = '\0'; }
  if (chatid) { strncpy(_tgChatId, chatid, sizeof(_tgChatId) - 1); _tgChatId[sizeof(_tgChatId) - 1] = '\0'; }
  _tg_write();
  EEPROM.commit();
}

// ─── WiFi режим и STA credentials ─────────────────────────────────────────
static uint8_t _wifiMode     = 0;           // 0=AP, 1=STA
static char    _wifiSsid[33] = "";
static char    _wifiStaPass[33] = "";
static bool    _wifiCfgLoaded = false;

void wifi_settings_init() {
  if (_wifiCfgLoaded) return;
  byte magic = 0;
  EEPROM.get(EEPROM_ADDR_WIFI_MAGIC, magic);
  if (magic != EEPROM_MAGIC_WIFI_VALUE) {
    // Первый запуск: пробуем STA. Реальные creds подставятся из WIFI_SSID/
    // WIFI_PASSWORD (секреты в secrets.h, см. Connectivity.h). Если STA не
    // подключится — wifi_init() откатит в AP автоматически.
    _wifiMode = 1;
    _wifiSsid[0] = '\0';
    _wifiStaPass[0] = '\0';
  } else {
    EEPROM.get(EEPROM_ADDR_WIFI_MODE, _wifiMode);
    EEPROM.get(EEPROM_ADDR_WIFI_SSID, _wifiSsid);
    EEPROM.get(EEPROM_ADDR_WIFI_PASS, _wifiStaPass);
    _wifiSsid[32]    = '\0';
    _wifiStaPass[32] = '\0';
    if (_wifiMode > 1) _wifiMode = 0;
  }
  _wifiCfgLoaded = true;
}

// Записывает WiFi-блок в EEPROM-буфер БЕЗ commit (для batch-сохранения)
static void _wifi_write() {
  byte magic = EEPROM_MAGIC_WIFI_VALUE;
  EEPROM.put(EEPROM_ADDR_WIFI_MAGIC, magic);
  EEPROM.put(EEPROM_ADDR_WIFI_MODE,  _wifiMode);
  EEPROM.put(EEPROM_ADDR_WIFI_SSID,  _wifiSsid);
  EEPROM.put(EEPROM_ADDR_WIFI_PASS,  _wifiStaPass);
}

uint8_t get_wifi_mode() { if (!_wifiCfgLoaded) wifi_settings_init(); return _wifiMode; }
void set_wifi_mode(uint8_t m) {
  if (m > 1) return;
  _wifiMode = m; _wifi_write(); EEPROM.commit();
}

void get_wifi_ssid(char *buf, size_t maxLen) {
  if (!_wifiCfgLoaded) wifi_settings_init();
  strncpy(buf, _wifiSsid, maxLen - 1);
  buf[maxLen - 1] = '\0';
}
void set_wifi_ssid(const char *ssid) {
  if (!ssid) return;
  strncpy(_wifiSsid, ssid, sizeof(_wifiSsid) - 1);
  _wifiSsid[sizeof(_wifiSsid) - 1] = '\0';
  _wifi_write(); EEPROM.commit();
}

void get_wifi_sta_pass(char *buf, size_t maxLen) {
  if (!_wifiCfgLoaded) wifi_settings_init();
  strncpy(buf, _wifiStaPass, maxLen - 1);
  buf[maxLen - 1] = '\0';
}
void set_wifi_sta_pass(const char *pass) {
  if (!pass) return;
  strncpy(_wifiStaPass, pass, sizeof(_wifiStaPass) - 1);
  _wifiStaPass[sizeof(_wifiStaPass) - 1] = '\0';
  _wifi_write(); EEPROM.commit();
}

void set_wifi_all(uint8_t mode, const char *ssid, const char *pass) {
  if (!_wifiCfgLoaded) wifi_settings_init();
  bool changed = false;
  if (mode <= 1 && mode != _wifiMode) { _wifiMode = mode; changed = true; }
  if (ssid && strncmp(_wifiSsid, ssid, sizeof(_wifiSsid) - 1) != 0) {
    strncpy(_wifiSsid, ssid, sizeof(_wifiSsid) - 1); _wifiSsid[sizeof(_wifiSsid) - 1] = '\0'; changed = true;
  }
  if (pass && strncmp(_wifiStaPass, pass, sizeof(_wifiStaPass) - 1) != 0) {
    strncpy(_wifiStaPass, pass, sizeof(_wifiStaPass) - 1); _wifiStaPass[sizeof(_wifiStaPass) - 1] = '\0'; changed = true;
  }
  if (!changed) return;
  _wifi_write();
  EEPROM.commit();
}

void wifi_commit() {
  _wifi_write();
  EEPROM.commit();
}

// ─── Расписание замеров ────────────────────────────────────────────────────
#define MAX_SCHED_TIMES 8
static uint16_t _schedTimes[MAX_SCHED_TIMES] = {};
static uint8_t  _schedCount = 0;
static bool     _schedLoaded = false;

void sched_settings_init() {
  if (_schedLoaded) return;
  byte magic = 0;
  EEPROM.get(EEPROM_ADDR_SCHED_MAGIC, magic);
  if (magic == EEPROM_MAGIC_SCHED_VALUE) {
    EEPROM.get(EEPROM_ADDR_SCHED_COUNT, _schedCount);
    if (_schedCount > MAX_SCHED_TIMES) _schedCount = 0;
    for (uint8_t i = 0; i < _schedCount; i++) {
      EEPROM.get(EEPROM_ADDR_SCHED_TIMES + i * 2, _schedTimes[i]);
    }
  } else {
    _schedCount = 0;
  }
  _schedLoaded = true;
}

void get_sched_times(uint16_t *times, uint8_t &count) {
  if (!_schedLoaded) sched_settings_init();
  count = _schedCount;
  for (uint8_t i = 0; i < _schedCount; i++) times[i] = _schedTimes[i];
}

void set_sched_times(const uint16_t *times, uint8_t count) {
  if (count > MAX_SCHED_TIMES) count = MAX_SCHED_TIMES;
  _schedCount = count;
  for (uint8_t i = 0; i < count; i++) _schedTimes[i] = times[i];
  byte magic = EEPROM_MAGIC_SCHED_VALUE;
  EEPROM.put(EEPROM_ADDR_SCHED_MAGIC, magic);
  EEPROM.put(EEPROM_ADDR_SCHED_COUNT, _schedCount);
  for (uint8_t i = 0; i < count; i++) {
    EEPROM.put(EEPROM_ADDR_SCHED_TIMES + i * 2, times[i]);
  }
  EEPROM.commit();
  _schedLoaded = true;
}

uint32_t sched_next_sec(uint8_t hour, uint8_t minute) {
  if (!_schedLoaded) sched_settings_init();
  if (_schedCount == 0) return get_sleep_sec();
  uint16_t now_min = (uint16_t)hour * 60 + minute;
  uint16_t best = 0;
  bool found = false;
  // Ближайшее время сегодня позже текущего
  for (uint8_t i = 0; i < _schedCount; i++) {
    if (_schedTimes[i] > now_min && (!found || _schedTimes[i] < best)) {
      best = _schedTimes[i]; found = true;
    }
  }
  if (found) return (uint32_t)(best - now_min) * 60;
  // Нет следующего сегодня — самое раннее завтра
  for (uint8_t i = 0; i < _schedCount; i++) {
    if (!found || _schedTimes[i] < best) { best = _schedTimes[i]; found = true; }
  }
  return (uint32_t)(1440 - now_min + best) * 60;
}

// ─── Интервал TG-отчётов ──────────────────────────────────────────────────
static uint32_t _tgReportIntervalMin = 360;  // 6 часов по умолчанию
static bool     _tgRptLoaded = false;

void tg_report_settings_init() {
  if (_tgRptLoaded) return;
  byte magic = 0;
  EEPROM.get(EEPROM_ADDR_TG_REPORT_MAGIC, magic);
  if (magic == EEPROM_MAGIC_TG_RPT_VALUE) {
    EEPROM.get(EEPROM_ADDR_TG_REPORT_INT, _tgReportIntervalMin);
    // Валидация: 0=откл, или 60..10080 мин. Мусор из EEPROM → дефолт.
    if (_tgReportIntervalMin != 0 && (_tgReportIntervalMin < 60 || _tgReportIntervalMin > 10080)) {
      _tgReportIntervalMin = 360;
    }
  }
  // иначе остаётся 360 мин (6ч) по умолчанию
  _tgRptLoaded = true;
}

uint32_t get_tg_report_interval_min() {
  if (!_tgRptLoaded) tg_report_settings_init();
  return _tgReportIntervalMin;
}

void set_tg_report_interval_min(uint32_t minutes) {
  // Валидация: 0=откл, или 60..10080 мин. Отклоняем невалидные значения.
  if (minutes != 0 && (minutes < 60 || minutes > 10080)) return;
  _tgReportIntervalMin = minutes;
  byte magic = EEPROM_MAGIC_TG_RPT_VALUE;
  EEPROM.put(EEPROM_ADDR_TG_REPORT_MAGIC, magic);
  EEPROM.put(EEPROM_ADDR_TG_REPORT_INT, _tgReportIntervalMin);
  EEPROM.commit();
  _tgRptLoaded = true;
}

// ─── Credentials (admin web UI + OTA) ────────────────────────────────────
static char _adminUser[24] = "admin";
static char _adminPass[32] = "beehive";
static char _otaPass[32]   = "ota_beehive";
static bool _credDefault   = true;   // true = используются дефолты (EEPROM пуст)
static bool _credLoaded    = false;

void credentials_init() {
  if (_credLoaded) return;
  byte magic = 0;
  EEPROM.get(EEPROM_ADDR_CRED_MAGIC, magic);
  if (magic != EEPROM_MAGIC_CRED_VALUE) {
    _credDefault = true;
    strncpy(_adminUser, "admin", sizeof(_adminUser));
    strncpy(_adminPass, "beehive", sizeof(_adminPass));
    strncpy(_otaPass,   "ota_beehive", sizeof(_otaPass));
  } else {
    EEPROM.get(EEPROM_ADDR_ADMIN_USER, _adminUser);
    EEPROM.get(EEPROM_ADDR_ADMIN_PASS, _adminPass);
    EEPROM.get(EEPROM_ADDR_OTA_PASS,   _otaPass);
    _adminUser[sizeof(_adminUser) - 1] = '\0';
    _adminPass[sizeof(_adminPass) - 1] = '\0';
    _otaPass[sizeof(_otaPass) - 1]     = '\0';
    if (_adminUser[0] == '\0') strncpy(_adminUser, "admin", sizeof(_adminUser));
    if (_adminPass[0] == '\0') strncpy(_adminPass, "beehive", sizeof(_adminPass));
    if (_otaPass[0]   == '\0') strncpy(_otaPass, "ota_beehive", sizeof(_otaPass));
    _credDefault = false;
  }
  _credLoaded = true;
}

bool credentials_is_default() {
  if (!_credLoaded) credentials_init();
  return _credDefault;
}

void get_admin_user(char *buf, size_t maxLen) {
  if (!_credLoaded) credentials_init();
  strncpy(buf, _adminUser, maxLen - 1);
  buf[maxLen - 1] = '\0';
}

void get_admin_pass(char *buf, size_t maxLen) {
  if (!_credLoaded) credentials_init();
  strncpy(buf, _adminPass, maxLen - 1);
  buf[maxLen - 1] = '\0';
}

void set_admin_credentials(const char *user, const char *pass) {
  if (!_credLoaded) credentials_init();
  if (user && strlen(user) >= 3 && strlen(user) <= 23) {
    strncpy(_adminUser, user, sizeof(_adminUser) - 1);
    _adminUser[sizeof(_adminUser) - 1] = '\0';
  }
  if (pass && strlen(pass) >= 6 && strlen(pass) <= 31) {
    strncpy(_adminPass, pass, sizeof(_adminPass) - 1);
    _adminPass[sizeof(_adminPass) - 1] = '\0';
  }
  byte magic = EEPROM_MAGIC_CRED_VALUE;
  EEPROM.put(EEPROM_ADDR_CRED_MAGIC, magic);
  EEPROM.put(EEPROM_ADDR_ADMIN_USER, _adminUser);
  EEPROM.put(EEPROM_ADDR_ADMIN_PASS, _adminPass);
  EEPROM.put(EEPROM_ADDR_OTA_PASS,   _otaPass);
  EEPROM.commit();
  _credDefault = false;
}

void get_ota_pass(char *buf, size_t maxLen) {
  if (!_credLoaded) credentials_init();
  strncpy(buf, _otaPass, maxLen - 1);
  buf[maxLen - 1] = '\0';
}

void set_ota_pass(const char *pass) {
  if (!_credLoaded) credentials_init();
  if (!pass || strlen(pass) < 6 || strlen(pass) > 31) return;
  strncpy(_otaPass, pass, sizeof(_otaPass) - 1);
  _otaPass[sizeof(_otaPass) - 1] = '\0';
  byte magic = EEPROM_MAGIC_CRED_VALUE;
  EEPROM.put(EEPROM_ADDR_CRED_MAGIC, magic);
  EEPROM.put(EEPROM_ADDR_ADMIN_USER, _adminUser);
  EEPROM.put(EEPROM_ADDR_ADMIN_PASS, _adminPass);
  EEPROM.put(EEPROM_ADDR_OTA_PASS,   _otaPass);
  EEPROM.commit();
  _credDefault = false;
}

// ─── Last TG report (v5.0.4) ─────────────────────────────────────────────
void save_last_report(float weight, bool hasReport) {
  byte magic = EEPROM_MAGIC_REPORT_VALUE;
  EEPROM.put(EEPROM_ADDR_REPORT_MAGIC,    magic);
  EEPROM.put(EEPROM_ADDR_LAST_REPORT_W,   weight);
  byte flag = hasReport ? 1 : 0;
  EEPROM.put(EEPROM_ADDR_HAS_LAST_REPORT, flag);
  EEPROM.commit();
}

bool load_last_report(float &weight) {
  byte magic = 0;
  EEPROM.get(EEPROM_ADDR_REPORT_MAGIC, magic);
  if (magic != EEPROM_MAGIC_REPORT_VALUE) return false;
  byte flag = 0;
  EEPROM.get(EEPROM_ADDR_HAS_LAST_REPORT, flag);
  if (!flag) return false;
  EEPROM.get(EEPROM_ADDR_LAST_REPORT_W, weight);
  if (isnan(weight) || weight < 0.0f || weight > 200.0f) return false;
  return true;
}

// ─── Last temperature (v5.0.5) ─────────────────────────────────────────
void save_last_temp(float tempC) {
  if (isnan(tempC) || tempC < -55.0f || tempC > 125.0f) return;  // не пишем мусор
  byte magic = EEPROM_MAGIC_TEMP_VALUE;
  EEPROM.put(EEPROM_ADDR_TEMP_MAGIC,  magic);
  EEPROM.put(EEPROM_ADDR_LAST_TEMP_C, tempC);
  EEPROM.commit();
}

bool load_last_temp(float &tempC) {
  byte magic = 0;
  EEPROM.get(EEPROM_ADDR_TEMP_MAGIC, magic);
  if (magic != EEPROM_MAGIC_TEMP_VALUE) return false;
  EEPROM.get(EEPROM_ADDR_LAST_TEMP_C, tempC);
  if (isnan(tempC) || tempC < -55.0f || tempC > 125.0f) return false;
  return true;
}

void save_last_visit(uint32_t unixtime) {
  if (unixtime < 1546300800UL) return;  // <2019-01-01 — RTC не настроен, мусор
  byte magic = EEPROM_MAGIC_LAST_VISIT_VALUE;
  EEPROM.put(EEPROM_ADDR_LAST_VISIT_MAGIC, magic);
  EEPROM.put(EEPROM_ADDR_LAST_VISIT,       unixtime);
  EEPROM.commit();
}

uint32_t load_last_visit() {
  byte magic = 0;
  EEPROM.get(EEPROM_ADDR_LAST_VISIT_MAGIC, magic);
  if (magic != EEPROM_MAGIC_LAST_VISIT_VALUE) return 0;
  uint32_t ts = 0;
  EEPROM.get(EEPROM_ADDR_LAST_VISIT, ts);
  if (ts < 1546300800UL) return 0;
  return ts;
}

// v5.0.44: Persistence для TG report timestamp.
// При power-cut millis() сбрасывается каждый wake → interval-проверка TG не работает.
// Сохраняем unix timestamp последнего успешного отчёта в EEPROM, сравниваем при boot.
void save_last_tg_report_unix(uint32_t unixtime) {
  if (unixtime < 1546300800UL) return;  // <2019-01-01 — мусор, RTC не настроен
  byte magic = EEPROM_MAGIC_TG_LAST_REP_VAL;
  EEPROM.put(EEPROM_ADDR_TG_LAST_REP_MAGIC, magic);
  EEPROM.put(EEPROM_ADDR_TG_LAST_REP_UNIX, unixtime);
  EEPROM.commit();
}

uint32_t load_last_tg_report_unix() {
  byte magic = 0;
  EEPROM.get(EEPROM_ADDR_TG_LAST_REP_MAGIC, magic);
  if (magic != EEPROM_MAGIC_TG_LAST_REP_VAL) return 0;
  uint32_t ts = 0;
  EEPROM.get(EEPROM_ADDR_TG_LAST_REP_UNIX, ts);
  if (ts < 1546300800UL) return 0;
  return ts;
}

// ─── Battery divider calibration ratio (v5.0.19) ─────────────────────────
// Сохраняет поправку коэффициента делителя в EEPROM. Дефолт 2.0 (R1=R2=100k),
// но реальные резисторы и ADC ESP32 имеют погрешность ±5-10%, поэтому
// пользователь корректирует через сайт.
// Валидация: разумный диапазон 1.5–3.0 (защита от мусора в EEPROM).
void save_bat_calib(float ratio) {
  if (isnan(ratio) || ratio < 1.5f || ratio > 3.0f) return;
  byte magic = EEPROM_MAGIC_BAT_CALIB_VALUE;
  EEPROM.put(EEPROM_ADDR_BAT_CALIB_MAGIC, magic);
  EEPROM.put(EEPROM_ADDR_BAT_CALIB_RATIO, ratio);
  EEPROM.commit();
}

bool load_bat_calib(float &ratio) {
  byte magic = 0;
  EEPROM.get(EEPROM_ADDR_BAT_CALIB_MAGIC, magic);
  if (magic != EEPROM_MAGIC_BAT_CALIB_VALUE) return false;
  EEPROM.get(EEPROM_ADDR_BAT_CALIB_RATIO, ratio);
  if (isnan(ratio) || ratio < 1.5f || ratio > 3.0f) return false;
  return true;
}

// ─── Telegram alerts (v5.0.20) ────────────────────────────────────────────
void save_alerts(const AlertSettings &a) {
  byte magic = EEPROM_MAGIC_ALERTS_VALUE;
  EEPROM.put(EEPROM_ADDR_ALERTS_MAGIC,    magic);
  EEPROM.put(EEPROM_ADDR_ALERT_BAT_V,     a.batLowV);
  EEPROM.put(EEPROM_ADDR_ALERT_TEMP_LOW,  a.tempLow);
  EEPROM.put(EEPROM_ADDR_ALERT_TEMP_HIGH, a.tempHigh);
  byte rtcEn = a.rtcEn ? 1 : 0;
  EEPROM.put(EEPROM_ADDR_ALERT_RTC_EN,    rtcEn);
  EEPROM.commit();
}

void load_alerts(AlertSettings &a) {
  byte magic = 0;
  EEPROM.get(EEPROM_ADDR_ALERTS_MAGIC, magic);
  if (magic != EEPROM_MAGIC_ALERTS_VALUE) {
    // Дефолты — всё выключено кроме RTC error
    a.batLowV  = 0.0f;
    a.tempLow  = 0.0f;
    a.tempHigh = 0.0f;
    a.rtcEn    = true;
    return;
  }
  EEPROM.get(EEPROM_ADDR_ALERT_BAT_V,     a.batLowV);
  EEPROM.get(EEPROM_ADDR_ALERT_TEMP_LOW,  a.tempLow);
  EEPROM.get(EEPROM_ADDR_ALERT_TEMP_HIGH, a.tempHigh);
  byte rtcEn = 0;
  EEPROM.get(EEPROM_ADDR_ALERT_RTC_EN, rtcEn);
  a.rtcEn = (rtcEn != 0);
  // Санитизация мусора
  if (isnan(a.batLowV)  || a.batLowV  < 0.0f || a.batLowV  > 5.0f)  a.batLowV  = 0.0f;
  if (isnan(a.tempLow)  || a.tempLow  < -55.0f || a.tempLow  > 85.0f) a.tempLow  = 0.0f;
  if (isnan(a.tempHigh) || a.tempHigh < -55.0f || a.tempHigh > 85.0f) a.tempHigh = 0.0f;
}
