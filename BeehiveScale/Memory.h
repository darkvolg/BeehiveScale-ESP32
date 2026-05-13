#ifndef MEMORY_H
#define MEMORY_H

#include <Arduino.h>
#include <EEPROM.h>

#define EEPROM_ADDR_CALIB        0
#define EEPROM_ADDR_OFFSET       4
#define EEPROM_ADDR_WEIGHT       8
#define EEPROM_ADDR_MAGIC        12
#define EEPROM_ADDR_ALERT_DELTA  13
#define EEPROM_ADDR_CALIB_WEIGHT 17
#define EEPROM_ADDR_EMA_ALPHA    21
#define EEPROM_ADDR_MAGIC2       25
#define EEPROM_MAGIC_VALUE       0xA5
#define EEPROM_ADDR_PREV_OFFSET  26
#define EEPROM_MAGIC2_VALUE      0xA6
#define EEPROM_ADDR_PREV_WEIGHT  30
// Дополнительные настройки (addr 34-63)
#define EEPROM_ADDR_SLEEP_SEC    34   // uint32_t — интервал сна (секунды)
#define EEPROM_ADDR_LCD_BL_SEC   38   // uint16_t — таймаут подсветки (секунды, 0=всегда вкл)
#define EEPROM_ADDR_AP_PASS      40   // char[24] — пароль Wi-Fi AP
#define EEPROM_ADDR_MAGIC3       64   // после AP_PASS[24] (40..63), проверяется через MAGIC3_VALUE
#define EEPROM_MAGIC3_VALUE      0xA7
// Telegram настройки (addr 65-132)
#define EEPROM_ADDR_TG_MAGIC     65   // 1 байт magic для TG блока
#define EEPROM_MAGIC_TG_VALUE    0xB1
#define EEPROM_ADDR_TG_TOKEN     66   // char[50] — Telegram bot token
#define EEPROM_ADDR_TG_CHATID    116  // char[16] — Telegram chat_id
// WiFi STA настройки (addr 133-200)
#define EEPROM_ADDR_WIFI_MAGIC   133  // 1 байт magic
#define EEPROM_MAGIC_WIFI_VALUE  0xC1
#define EEPROM_ADDR_WIFI_MODE    134  // 1 байт: 0=AP, 1=STA
#define EEPROM_ADDR_WIFI_SSID    135  // char[33] — SSID роутера
#define EEPROM_ADDR_WIFI_PASS    168  // char[33] — Пароль роутера
// Расписание замеров (addr 201-218)
#define EEPROM_ADDR_SCHED_MAGIC  201  // 1 байт magic
#define EEPROM_MAGIC_SCHED_VALUE 0xD1
#define EEPROM_ADDR_SCHED_COUNT  202  // 1 байт (0..8)
#define EEPROM_ADDR_SCHED_TIMES  203  // 8 × uint16_t = 16 байт (минуты от полуночи)
#define EEPROM_ADDR_TG_REPORT_INT 219  // uint32_t — интервал TG-отчётов (минуты, 0=выкл)
#define EEPROM_MAGIC_TG_RPT_VALUE 0xE1
#define EEPROM_ADDR_TG_REPORT_MAGIC 223 // 1 байт magic
// ─── Credentials block (addr 224-383) ─────────────────────────────────────
// Добавлено в v4.2: admin web UI pass, OTA pass. Старый layout 0..223 не затронут.
#define EEPROM_ADDR_CRED_MAGIC   224  // 1 байт magic
#define EEPROM_MAGIC_CRED_VALUE  0xF2
#define EEPROM_ADDR_ADMIN_USER   225  // char[24]
#define EEPROM_ADDR_ADMIN_PASS   249  // char[32]
#define EEPROM_ADDR_OTA_PASS     281  // char[32]
// ─── Точка отсчёта prevWeight: дата фиксации (v5.0.2) ────────────────────
#define EEPROM_ADDR_PREV_DATE    313  // uint32_t — Unix timestamp когда вес был зафиксирован
// ─── Auto-sleep timeout (v5.0.3) ─────────────────────────────────────────
#define EEPROM_ADDR_AUTOSLEEP    317  // uint16_t — секунд бездействия до deep sleep (0=отключено)
#define EEPROM_ADDR_AUTOSLEEP_MAGIC 319  // 1 байт magic
#define EEPROM_MAGIC_AUTOSLEEP_VALUE 0xA8
// ─── Last TG report (v5.0.4) — переживает reset/прошивку, чтобы дельта "С прошлого замера" не пропадала
#define EEPROM_ADDR_REPORT_MAGIC      320  // 1 байт magic
#define EEPROM_MAGIC_REPORT_VALUE     0xA9
#define EEPROM_ADDR_LAST_REPORT_W     321  // float, 4 байта — вес на момент прошлого TG-отчёта
#define EEPROM_ADDR_HAS_LAST_REPORT   325  // bool, 1 байт
// ─── Last temperature (v5.0.5) — fallback для отчётов когда DS18B20 не успел прочитаться после wake
#define EEPROM_ADDR_TEMP_MAGIC        326  // 1 байт magic
#define EEPROM_MAGIC_TEMP_VALUE       0xAA
#define EEPROM_ADDR_LAST_TEMP_C       327  // float, 4 байта
// ─── Last web visit (v5.0.17) — для пресета "С прошлого визита" в архиве ─
#define EEPROM_ADDR_LAST_VISIT_MAGIC  331  // 1 байт
#define EEPROM_MAGIC_LAST_VISIT_VALUE 0xAB
#define EEPROM_ADDR_LAST_VISIT        332  // uint32_t, 4 байта — Unix timestamp последнего web-визита
// ─── Battery divider calibration (v5.0.19) — поправка под реальные резисторы делителя
#define EEPROM_ADDR_BAT_CALIB_MAGIC   336  // 1 байт
#define EEPROM_MAGIC_BAT_CALIB_VALUE  0xAC
#define EEPROM_ADDR_BAT_CALIB_RATIO   337  // float, 4 байта — коэф. делителя (default 2.0 для 100k/100k)
// 341..383 reserved for future fields
#define EEPROM_SIZE              384  // расширено под credentials-блок

// Веб-настройки (alertDelta, calibWeight, emaAlpha)
void  web_settings_init();
float web_get_alert_delta();
float web_get_calib_weight();
float web_get_ema_alpha();
void  load_web_settings(float &alertDelta, float &calibWeight, float &emaAlpha);
void  save_web_settings(float alertDelta, float calibWeight, float emaAlpha);

// Расширенные настройки (sleep, LCD backlight, AP password)
void     ext_settings_init();
uint32_t get_sleep_sec();
void     set_sleep_sec(uint32_t sec);
uint16_t get_lcd_bl_sec();
void     set_lcd_bl_sec(uint16_t sec);
void     get_ap_pass(char *buf, size_t maxLen);
void     set_ap_pass(const char *pass);
void     set_ext_all(uint32_t sleepSec, uint16_t lcdBlSec, const char *apPass);  // batch: все ext-поля + 1 commit

// Калибровка и вес
void load_calibration_data(float &factor, long &offset, float &weight);
void save_calibration(float factor);
void save_offset(long offset);
void save_weight(float &lastWeight, float currentWeight);
// Batch: восстановление всего калибровочного блока (factor, offset, weight, prevWeight, prevOffset)
// за 1 EEPROM.commit() — используется в /api/backup/restore для снижения wear.
// Параметры со значением NAN / INT32_MIN игнорируются (сохраняется текущее значение).
void save_calibration_block(float factor, long offset, float weight,
                            float prevWeight, long prevOffset);

// Telegram настройки (token, chat_id)
void tg_settings_init();
void get_tg_token(char *buf, size_t maxLen);
void set_tg_token(const char *token);
void get_tg_chatid(char *buf, size_t maxLen);
void set_tg_chatid(const char *chatid);
void tg_commit();   // batch: записать TG-блок + commit (после изменения token/chatid без commit)
void set_tg_all(const char *token, const char *chatid);  // batch: оба поля + 1 commit

// WiFi режим и STA credentials
void     wifi_settings_init();
uint8_t  get_wifi_mode();          // 0=AP, 1=STA
void     set_wifi_mode(uint8_t m);
void     get_wifi_ssid(char *buf, size_t maxLen);
void     set_wifi_ssid(const char *ssid);
void     get_wifi_sta_pass(char *buf, size_t maxLen);
void     set_wifi_sta_pass(const char *pass);
void     wifi_commit();   // batch: записать WiFi-блок + commit
void     set_wifi_all(uint8_t mode, const char *ssid, const char *pass);  // batch: все поля + 1 commit

// Расписание замеров (до 8 суточных времён)
void     sched_settings_init();
void     get_sched_times(uint16_t *times, uint8_t &count);
void     set_sched_times(const uint16_t *times, uint8_t count);
uint32_t sched_next_sec(uint8_t hour, uint8_t minute);  // секунд до следующего времени по расписанию

// TG интервал отчётов (0 = отключить периодические отчёты)
void     tg_report_settings_init();
uint32_t get_tg_report_interval_min();
void     set_tg_report_interval_min(uint32_t minutes);

// Credentials (admin web UI, OTA)
// При пустом EEPROM возвращают дефолты "admin"/"beehive"/"ota_beehive", credentials_is_default() → true.
void credentials_init();
bool credentials_is_default();
void get_admin_user(char *buf, size_t maxLen);
void get_admin_pass(char *buf, size_t maxLen);
void set_admin_credentials(const char *user, const char *pass);
void get_ota_pass(char *buf, size_t maxLen);
void set_ota_pass(const char *pass);

// Предыдущий offset (для отмены тары)
void save_prev_offset(long prevOffset);
long load_prev_offset();

// Опорный вес для дельты (не перезаписывается авто-фиксацией)
void  save_prev_weight(float w);
float load_prev_weight(float fallback);

// Дата фиксации опорного веса (Unix timestamp). 0 = не задано.
void     save_prev_weight_date(uint32_t unixtime);
uint32_t load_prev_weight_date();

// Auto-sleep timeout — через сколько секунд бездействия уйти в deep sleep.
// 0 = не засыпать никогда (для отладки). Дефолт 180 сек (3 минуты).
uint16_t get_autosleep_sec();
void     set_autosleep_sec(uint16_t sec);

// Last TG report — переживает reset, чтобы дельта "С прошлого замера" не пропадала
// после прошивки/перезагрузки. save_last_report() вызывать после успешного tg_send_report().
// load_last_report() возвращает true и заполняет weight, если в EEPROM есть валидная запись.
void save_last_report(float weight, bool hasReport);
bool load_last_report(float &weight);

// Last valid temperature — fallback если DS18B20 не успел прочитаться к моменту
// отправки TG-отчёта. Сохраняется после каждого валидного чтения, читается в setup().
void save_last_temp(float tempC);
bool load_last_temp(float &tempC);

// Last web visit — Unix timestamp последнего открытия web-интерфейса.
// Используется для пресета "С прошлого визита" в архивной вкладке.
// 0 = ни разу не было визитов (или EEPROM пуст).
void     save_last_visit(uint32_t unixtime);
uint32_t load_last_visit();

// Battery divider calibration ratio (v5.0.19)
void save_bat_calib(float ratio);
bool load_bat_calib(float &ratio);

#endif
