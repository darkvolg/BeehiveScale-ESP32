#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

// ─── Конфигурация логгера ─────────────────────────────────────────────────
// Fallback-интервал: используется ТОЛЬКО если расписание (schedTimes) не задано.
// Когда расписание настроено через веб-UI (например "21:00") — лог пишется
// строго по расписанию + при boot, этот интервал игнорируется.
// 30 минут — комфорт для пасеки если пользователь не настроил расписание.
#define LOG_INTERVAL_MS  1800000UL   // 30 минут
#define LOG_MAX_SIZE      102400UL   // 100 КБ — ротация (переименовать → log_old.csv)
#define LOG_FILE          "/log.csv"
#define LOG_FILE_OLD      "/log_old.csv"

// SD-карта отключена с v4.2.1: HX711 переехал на D5/D6 (стандартная распиновка ESP8266+HX711),
// что освободило их от SPI. Логи теперь во внутренний flash через LittleFS — надёжнее SD
// в полевых условиях улья (нет окисления контактов, выдерживает -40..+85°C, journaling FS).
// При FS:2MB ресурс ~90 лет при текущем интервале логирования.
// Чтобы вернуть SD: раскомментировать #define USE_SD_CARD ниже, проверить что D5/D6/D7
// свободны (придётся искать другие пины для HX711).
// #define USE_SD_CARD
#define SD_CS_PIN 15

// ─── API ──────────────────────────────────────────────────────────────────
bool   log_init();
// Запросить форматирование FS при следующем log_init() (опасно — удаляет все данные!)
void   log_request_format();
// batPct: процент заряда батареи; если < 5 — запись пропускается (защита от разряда)
void   log_append(const String &datetime, float weight, float tempC,
                  float humidity, float batV, int batPct);
void   log_clear();
size_t log_size();
bool   log_exists();
// Свободное место на SD (байт); 0 если SD недоступна
uint32_t log_free_space();
// Парсит CSV и возвращает JSON-массив последних maxRows строк
String   log_to_json(int maxRows = 200);
// Стримит JSON-массив прямо в Stream (без промежуточного String в heap) — предпочтительно
// для HTTP ответов на ESP8266 с ограниченным heap. Возвращает кол-во строк.
size_t   log_stream_json(Stream &out, int maxRows = 50);
// Стримит CSV только за указанную дату (формат: "DD.MM.YYYY") прямо в поток
// Возвращает кол-во строк; если date пустая — возвращает весь файл
size_t   log_stream_csv_date(Stream &out, const String &date);
// Стримит CSV за диапазон дат (включительно). Формат from/to: "YYYY-MM-DD" или "DD.MM.YYYY".
// Пустые from/to означают "без нижней/верхней границы". Возвращает кол-во строк.
size_t   log_stream_csv_range(Stream &out, const String &from, const String &to);
// Стримит JSON-массив записей за диапазон дат: [{"dt":"...","w":..,"t":..,"b":..},...]
// Формат from/to: "YYYY-MM-DD" или "DD.MM.YYYY". Возвращает кол-во строк.
size_t   log_stream_period_json(Stream &out, const String &from, const String &to);
// Первая дата в логе (DD.MM.YYYY) — для подсчёта дней наблюдений
bool     log_first_date(char *buf, size_t bufLen);
// Суточная статистика: min/max вес и температура за сегодня
struct DayStat {
  float wMin, wMax, tMin, tMax;
  int   count;
  bool  valid;
};
DayStat  log_day_stat(const String &todayDate);
// Возвращает true если SD недоступна и используется LittleFS как резервный FS
bool     log_using_fallback();
// Возвращает true если файловая система (SD или LittleFS) смонтирована и доступна
bool     log_fs_ok();

// ─── Бэкап настроек на SD/LittleFS ─────────────────────────────────────
#define BACKUP_FILE "/backup.json"
// Сохраняет JSON-бэкап всех EEPROM-настроек на SD/LittleFS
bool     log_save_backup(const String &json);
// Читает JSON-бэкап из файла; пустая строка при ошибке (устарело — используйте stream)
String   log_read_backup();
// Стримит JSON-бэкап чанками в Stream (экономит heap: ESP8266 ~40 КБ)
// Возвращает количество отправленных байт; 0 при ошибке.
size_t   log_stream_backup(Stream &out);

#endif
