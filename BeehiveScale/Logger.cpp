#include "Logger.h"
#include <math.h>

// ─── Файловая система ─────────────────────────────────────────────────────
#ifdef USE_SD_CARD
  #include <SPI.h>
  #include <SD.h>
  #include <LittleFS.h>
  static bool _sdOk      = false;
  static bool _fallback  = false;  // true = SD недоступна, используем LittleFS
#else
  #include <LittleFS.h>
  static bool _flashOk   = false;
  static bool _fallback  = false;
#endif

// UTF-8 BOM (\xEF\xBB\xBF) + разделитель ";" для корректного открытия в Excel
// (русская локаль Excel использует ";" как разделитель столбцов)
static const char CSV_HEADER[] = "\xEF\xBB\xBF" "datetime;weight_kg;temp_c;humidity_pct;bat_v\n";

// ─── Хелпер: запятая→точка для парсинга CSV с десятичной запятой ─────────
// Без heap-аллокаций: работает на стековом буфере
static float commaToFloat(const char *src, size_t len) {
  char buf[16];
  if (len == 0 || len >= sizeof(buf)) return 0.0f;
  for (size_t i = 0; i < len; i++) buf[i] = (src[i] == ',') ? '.' : src[i];
  buf[len] = '\0';
  return (float)atof(buf);
}
// Обёртка для String (обратная совместимость)
static float commaToFloat(const String &s) {
  return commaToFloat(s.c_str(), s.length());
}
// Замена запятой на точку in-place (без heap-аллокации) для вставки в JSON
// Записывает результат в buf (maxLen включая '\0'), возвращает buf
static char* commaToPoint(const char *src, size_t srcLen, char *buf, size_t maxLen) {
  size_t len = srcLen;
  if (len >= maxLen) len = maxLen - 1;
  for (size_t i = 0; i < len; i++) buf[i] = (src[i] == ',') ? '.' : src[i];
  buf[len] = '\0';
  return buf;
}
// Обёртка для String (обратная совместимость)
static char* commaToPoint(const String &s, char *buf, size_t maxLen) {
  return commaToPoint(s.c_str(), s.length(), buf, maxLen);
}

// ─── Внутренние хелперы (абстракция над SD / LittleFS) ──────────────────

static bool _fs_exists(const char *path) {
#ifdef USE_SD_CARD
  if (!_fallback) return SD.exists(path);
  return LittleFS.exists(path);
#else
  return LittleFS.exists(path);
#endif
}

static bool _fs_remove(const char *path) {
#ifdef USE_SD_CARD
  if (!_fallback) return SD.remove(path);
  return LittleFS.remove(path);
#else
  return LittleFS.remove(path);
#endif
}

static bool _fs_rename(const char *from, const char *to) {
#ifdef USE_SD_CARD
  if (!_fallback) {
    // ESP8266 SD нет rename — эмулируем копированием
    File src = SD.open(from, FILE_READ);
    if (!src) return false;
    File dst = SD.open(to, FILE_WRITE);
    if (!dst) { src.close(); return false; }
    uint8_t buf[256];
    size_t totalRead = 0, totalWritten = 0;
    while (src.available()) {
      int n = src.read(buf, sizeof(buf));
      if (n > 0) {
        totalRead += n;
        totalWritten += dst.write(buf, n);
      }
      yield();
      ESP.wdtFeed();
    }
    src.close(); dst.close();
    if (totalRead != totalWritten) {
      SD.remove(to);  // удалить неполную копию, сохранить оригинал
      return false;
    }
    SD.remove(from);
    return true;
  }
  return LittleFS.rename(from, to);
#else
  return LittleFS.rename(from, to);
#endif
}

static File _fs_open_read(const char *path) {
#ifdef USE_SD_CARD
  if (!_fallback) return SD.open(path, FILE_READ);
  return LittleFS.open(path, "r");
#else
  return LittleFS.open(path, "r");
#endif
}

static File _fs_open_write(const char *path) {
#ifdef USE_SD_CARD
  if (!_fallback) {
    SD.remove(path);  // ESP8266 SD: FILE_WRITE = append, need explicit remove for truncate
    return SD.open(path, FILE_WRITE);
  }
  return LittleFS.open(path, "w");
#else
  return LittleFS.open(path, "w");
#endif
}

static File _fs_open_append(const char *path) {
#ifdef USE_SD_CARD
  if (!_fallback) {
  #if defined(ESP32)
    return SD.open(path, FILE_APPEND);
  #else
    // ESP8266 SD: FILE_WRITE создаёт/перезаписывает файл.
    // Для дозаписи нужно открыть и перемотать в конец (seek).
    File f = SD.open(path, FILE_WRITE);
    if (f) f.seek(f.size());
    return f;
  #endif
  }
  return LittleFS.open(path, "a");
#else
  return LittleFS.open(path, "a");
#endif
}

static bool _fs_ok() {
#ifdef USE_SD_CARD
  return _sdOk || _fallback;
#else
  return _flashOk;
#endif
}

// ─── Инициализация ────────────────────────────────────────────────────────

// Внутренний флаг — вызывал ли пользователь явный запрос форматирования FS
// (через будущий /api/fs/format). Автоматический format() при begin() fail
// удаляет все данные без предупреждения — это опасное поведение.
static bool _userRequestedFormat = false;

void log_request_format() { _userRequestedFormat = true; }

bool log_init() {
#ifdef USE_SD_CARD
  _sdOk = SD.begin(SD_CS_PIN);
  if (!_sdOk) {
    Serial.println(F("[Log] SD FAILED — trying LittleFS fallback"));
    _fallback = LittleFS.begin();
    if (!_fallback) {
      // Ретраим begin() ещё раз — transient ошибки (шум по CS) могут пройти.
      delay(100);
      _fallback = LittleFS.begin();
    }
    if (!_fallback) {
      if (_userRequestedFormat) {
        Serial.println(F("[Log] User requested format, formatting LittleFS..."));
        LittleFS.format();
        _fallback = LittleFS.begin();
        _userRequestedFormat = false;
      } else {
        Serial.println(F("[Log] LittleFS begin FAILED — skipping auto-format."));
        Serial.println(F("[Log] Call log_request_format() explicitly to reformat."));
      }
    }
    if (!_fallback) {
      Serial.println(F("[Log] LittleFS fallback FAILED too"));
      return false;
    }
    Serial.println(F("[Log] Using LittleFS as fallback"));
  } else {
    _fallback = false;
    Serial.println(F("[Log] SD OK"));
  }
#else
  // ESP32 умеет форматировать при mount fail через LittleFS.begin(true).
  // На ESP8266 такой опции нет — там оставляем старую логику.
#if defined(ESP32)
  _flashOk = LittleFS.begin(true);  // true = auto-format on mount fail
  if (!_flashOk) {
    delay(100);
    _flashOk = LittleFS.begin(true);
  }
  if (!_flashOk) {
    Serial.println(F("[Log] LittleFS FAILED even with auto-format"));
    return false;
  }
#else
  _flashOk = LittleFS.begin();
  if (!_flashOk) {
    delay(100);
    _flashOk = LittleFS.begin();
  }
  if (!_flashOk) {
    if (_userRequestedFormat) {
      Serial.println(F("[Log] User requested format, formatting..."));
      LittleFS.format();
      _flashOk = LittleFS.begin();
      _userRequestedFormat = false;
    } else {
      Serial.println(F("[Log] LittleFS FAILED — no auto-format"));
      return false;
    }
  }
#endif
  if (!_flashOk) {
    Serial.println(F("[Log] LittleFS FAILED"));
    return false;
  }
  Serial.println(F("[Log] LittleFS OK"));
#endif

  // Создать файл с заголовком если не существует
  if (!_fs_exists(LOG_FILE)) {
    File f = _fs_open_write(LOG_FILE);
    if (f) { f.print(CSV_HEADER); f.close(); }
  } else {
    // Проверяем заголовок: должен содержать "datetime" и ";" (новый формат).
    // Читаем через char-буфер (без readStringUntil — защита от OOM при отсутствии '\n').
    File f = _fs_open_read(LOG_FILE);
    if (f) {
      char hdr[80];
      int hpos = 0;
      while (f.available() && hpos < (int)sizeof(hdr) - 1) {
        int c = f.read();
        if (c == '\n' || c == '\r' || c < 0) break;
        hdr[hpos++] = (char)c;
      }
      hdr[hpos] = '\0';
      f.close();
      bool hdrOk = (strstr(hdr, "datetime") != NULL) && (strchr(hdr, ';') != NULL);
      if (!hdrOk) {
        Serial.println(F("[Log] Header outdated/invalid — recreating log"));
        File fw = _fs_open_write(LOG_FILE);
        if (fw) { fw.print(CSV_HEADER); fw.close(); }
      }
    }
  }

  // Проверяем что append реально работает
  File testF = _fs_open_append(LOG_FILE);
  if (!testF) {
    Serial.println(F("[Log] WARNING: append test FAILED!"));
#ifdef USE_SD_CARD
    if (!_fallback) {
      Serial.println(F("[Log] SD append broken — switching to LittleFS"));
      _sdOk = false;
      _fallback = LittleFS.begin();
      if (!_fallback) {
        delay(100);
        _fallback = LittleFS.begin();  // retry без автоформата
      }
      if (_fallback) {
        if (!LittleFS.exists(LOG_FILE)) {
          File fw = LittleFS.open(LOG_FILE, "w");
          if (fw) { fw.print(CSV_HEADER); fw.close(); }
        }
      }
    }
#endif
  } else {
    testF.close();
  }

  Serial.print(F("[Log] Ready"));
  if (_fallback) Serial.print(F(" [fallback:LittleFS]"));
  Serial.print(F(", size="));
  Serial.println(log_size());
  return true;
}

// ─── Пункт 6: Валидация данных перед записью ─────────────────────────────

static bool _validate_row(const String &datetime, float weight, float tempC,
                           float humidity, float batV) {
  if (datetime.length() == 0)         return false;  // совсем пустая дата
  if (datetime.length() > 25)         return false;  // подозрительно длинная дата
  // Санитизация datetime: отсекаем строки с опасными символами (CSV / XSS / разделители)
  for (unsigned int i = 0; i < datetime.length(); i++) {
    char c = datetime[i];
    if (c == ';' || c == '\r' || c == '\n' || c == '\t' ||
        c == '<' || c == '>' || c == '"' || c == '\\' || (unsigned char)c < 0x20) {
      return false;
    }
  }
  if (isnan(weight) || isinf(weight)) return false;
  if (weight < -5.0f || weight > 500.0f) return false;  // физически невозможный вес
  // Значения ≤ -90 — это сентинел ошибки датчика (-99 = TEMP_ERROR_VALUE).
  // Пропускаем валидацию диапазона для них; реальные физические значения проверяем.
  if (!isnan(tempC) && !isinf(tempC) && tempC > -90.0f) {
    if (tempC < -50.0f || tempC > 100.0f) return false;
  }
  if (!isnan(humidity) && !isinf(humidity) && humidity > -90.0f) {
    if (humidity < 0.0f || humidity > 100.0f) return false;
  }
  if (!isnan(batV) && !isinf(batV)) {
    if (batV < 0.0f || batV > 6.0f) return false;  // батарея не может быть > 6В
  }
  return true;
}

// Защита от Excel / OpenOffice formula injection (CVE-2014-3524 style):
// если datetime начинается с =, +, -, @ — добавляем ведущую апостроф-экранировку.
static void _write_datetime_safe(File &f, const String &datetime) {
  if (datetime.length() > 0) {
    char first = datetime[0];
    if (first == '=' || first == '+' || first == '-' ||
        first == '@' || first == '\t' || first == '\r') {
      f.print('\'');
    }
  }
  f.print(datetime);
}

// ─── Форматирование и запись одной CSV-строки ─────────────────────────────
static void _write_csv_row(File &f, const String &datetime, float weight,
                           float tempC, float humidity, float batV) {
  if (isnan(tempC)    || isinf(tempC)    || tempC    <= -90.0f) tempC    = 0.0f;
  if (isnan(humidity) || isinf(humidity) || humidity <= -90.0f) humidity = 0.0f;
  if (batV < 0.1f) batV = 0.0f;

  char wBuf[12], tBuf[12], hBuf[12], bBuf[12];
  snprintf(wBuf, sizeof(wBuf), "%.2f", weight);
  snprintf(tBuf, sizeof(tBuf), "%.1f", tempC);
  snprintf(hBuf, sizeof(hBuf), "%.1f", humidity);
  snprintf(bBuf, sizeof(bBuf), "%.2f", batV);
  for (char *p = wBuf; *p; p++) if (*p == '.') *p = ',';
  for (char *p = tBuf; *p; p++) if (*p == '.') *p = ',';
  for (char *p = hBuf; *p; p++) if (*p == '.') *p = ',';
  for (char *p = bBuf; *p; p++) if (*p == '.') *p = ',';

  _write_datetime_safe(f, datetime); f.print(';');
  f.print(wBuf);     f.print(';');
  f.print(tBuf);     f.print(';');
  f.print(hBuf);     f.print(';');
  f.print(bBuf);     f.print('\n');
}

// ─── Запись строки ────────────────────────────────────────────────────────

void log_append(const String &datetime, float weight, float tempC,
                float humidity, float batV, int batPct) {
  if (!_fs_ok()) return;

  // Защита от записи при критически низком заряде батареи.
  // batV < 1.0V — батарея не подключена (питание от USB) → не пропускать.
  if (batPct < 5 && batV > 1.0f) {
    Serial.println(F("[Log] Skip: low battery"));
    return;
  }

  // Пункт 6: Валидация данных
  if (!_validate_row(datetime, weight, tempC, humidity, batV)) {
    Serial.println(F("[Log] Skip: invalid data"));
    return;
  }

  // Ротация: если файл > LOG_MAX_SIZE — архивировать с датой
  if (log_size() >= LOG_MAX_SIZE) {
    // Счётчик последовательных неудачных ротаций (пункт 21):
    // после 3-х неудач форсируем truncate — иначе файл растёт бесконечно.
    static uint8_t _rotateFailCount = 0;

    // Проверяем свободное место на SD/LittleFS — нельзя rotate если места нет
    uint32_t freeBytes = log_free_space();
    bool spaceOk = (freeBytes == 0) || (freeBytes > LOG_MAX_SIZE / 2);

    // Формируем имя архива: /log_YYMMDD_HHMM.csv
    // datetime передаётся в формате "DD.MM.YYYY HH:MM:SS"
    char arcName[32];
    if (datetime.length() >= 16) {
      // "DD.MM.YYYY HH:MM" → "YYMMDD_HHMM"
      snprintf(arcName, sizeof(arcName), "/log_%c%c%c%c%c%c_%c%c%c%c.csv",
        datetime[8], datetime[9],  // YY (последние 2 цифры года)
        datetime[3], datetime[4],  // MM
        datetime[0], datetime[1],  // DD
        datetime[11], datetime[12], // HH
        datetime[14], datetime[15]  // MM
      );
    } else {
      // Фоллбэк — перезаписать log_old.csv
      strncpy(arcName, LOG_FILE_OLD, sizeof(arcName));
    }

    bool rotateOk = false;
    if (spaceOk) {
      if (_fs_exists(arcName)) _fs_remove(arcName);
      if (_fs_rename(LOG_FILE, arcName)) {
        File fn = _fs_open_write(LOG_FILE);
        if (!fn) {
          _fs_rename(arcName, LOG_FILE);
          Serial.println(F("[Log] New file creation FAILED, restored from archive"));
        } else {
          fn.print(CSV_HEADER); fn.close();
          rotateOk = true;
          Serial.print(F("[Log] Rotated → "));
          Serial.println(arcName);
        }
      } else {
        Serial.println(F("[Log] Rename FAILED"));
      }
    } else {
      Serial.println(F("[Log] No space for archive, skip rotation"));
    }

    if (rotateOk) {
      _rotateFailCount = 0;
    } else {
      _rotateFailCount++;
      if (_rotateFailCount >= 3) {
        // Форсированный truncate: пересоздаём лог с заголовком, старые данные теряются.
        // Это защита от неограниченного роста файла — лучше потерять последние
        // записи чем довести SD до 100% заполнения.
        Serial.println(F("[Log] Rotate failed 3x — forced truncate"));
        _fs_remove(LOG_FILE);
        File fw = _fs_open_write(LOG_FILE);
        if (fw) { fw.print(CSV_HEADER); fw.close(); }
        _rotateFailCount = 0;
      }
    }
  }

  File f = _fs_open_append(LOG_FILE);
  if (!f) {
    Serial.println(F("[Log] Open FAILED for append"));
#ifdef USE_SD_CARD
    // Пункт 7: если запись на SD провалилась — переключаемся на LittleFS
    if (!_fallback) {
      Serial.println(F("[Log] Switching to LittleFS fallback"));
      _sdOk = false;
      _fallback = LittleFS.begin();
      if (_fallback) {
        File ff = LittleFS.open(LOG_FILE, "a");
        if (!ff) {
          File fw = LittleFS.open(LOG_FILE, "w");
          if (fw) { fw.print(CSV_HEADER); fw.close(); }
          ff = LittleFS.open(LOG_FILE, "a");
        }
        if (ff) {
          _write_csv_row(ff, datetime, weight, tempC, humidity, batV);
          ff.close();
        }
      }
    }
#endif
    return;
  }

  _write_csv_row(f, datetime, weight, tempC, humidity, batV);
  f.close();
}

void log_clear() {
  if (!_fs_ok()) return;
  if (_fs_exists(LOG_FILE))     _fs_remove(LOG_FILE);
  if (_fs_exists(LOG_FILE_OLD)) _fs_remove(LOG_FILE_OLD);
  // Удаляем ротированные архивы /log_YYMMDD_HHMM.csv
#ifdef USE_SD_CARD
  if (!_fallback) {
    // Collect filenames first, then delete (can't modify dir during iteration)
    char toDelete[10][32];
    int delCount = 0;
    File root = SD.open("/");
    if (root) {
      File entry;
      while ((entry = root.openNextFile()) && delCount < 10) {
        const char* n = entry.name();
        const char* base = (n && n[0] == '/') ? n+1 : n;
        if (base && strncmp(base, "log_", 4) == 0) {
          int len = strlen(base);
          if (len > 4 && strcmp(base + len - 4, ".csv") == 0) {
            snprintf(toDelete[delCount], 32, "/%s", base);
            delCount++;
          }
        }
        entry.close();
      }
      root.close();
    }
    for (int i = 0; i < delCount; i++) {
      SD.remove(toDelete[i]);
    }
  } else
#endif
  {
    // Собираем имена файлов перед удалением (итерация + удаление одновременно небезопасна)
    String delFiles[8];
    int delCnt = 0;
#if defined(ESP32)
    File root = LittleFS.open("/");
    if (root && root.isDirectory()) {
      File entry;
      while ((entry = root.openNextFile()) && delCnt < 8) {
        String fn = entry.name();
        // На ESP32 LittleFS имена могут быть с ведущим '/' или без — нормализуем
        const char *base = fn.c_str();
        if (base[0] == '/') base++;
        String b(base);
        if (b.startsWith("log_") && b.endsWith(".csv")) {
          delFiles[delCnt++] = String("/") + b;
        }
        entry.close();
      }
      root.close();
    }
#else
    Dir dir = LittleFS.openDir("/");
    while (dir.next() && delCnt < 8) {
      String fn = dir.fileName();
      if (fn.startsWith("log_") && fn.endsWith(".csv")) {
        delFiles[delCnt++] = "/" + fn;
      }
    }
#endif
    for (int i = 0; i < delCnt; i++) {
      LittleFS.remove(delFiles[i]);
    }
  }
  File f = _fs_open_write(LOG_FILE);
  if (f) { f.print(CSV_HEADER); f.close(); }
  Serial.println(F("[Log] Cleared"));
}

size_t log_size() {
  if (!_fs_ok()) return 0;
  if (!_fs_exists(LOG_FILE)) return 0;
  File f = _fs_open_read(LOG_FILE);
  if (!f) return 0;
  size_t sz = f.size();
  f.close();
  return sz;
}

bool log_exists() {
  if (!_fs_ok()) return false;
  return _fs_exists(LOG_FILE);
}

uint32_t log_free_space() {
#ifdef USE_SD_CARD
  if (_fallback) {
#if defined(ESP8266)
    FSInfo info;
    if (LittleFS.info(info)) return (uint32_t)(info.totalBytes - info.usedBytes);
#elif defined(ESP32)
    return (uint32_t)(LittleFS.totalBytes() - LittleFS.usedBytes());
#endif
    return 0;
  }
  return 0;  // ESP8266 SD library does not expose free space
#else
  if (!_flashOk) return 0;
#if defined(ESP8266)
  FSInfo info;
  if (LittleFS.info(info)) return (uint32_t)(info.totalBytes - info.usedBytes);
  return 0;
#elif defined(ESP32)
  return (uint32_t)(LittleFS.totalBytes() - LittleFS.usedBytes());
#else
  return 0;
#endif
#endif
}

// Возвращает true если активен резервный LittleFS (SD недоступна)
bool log_using_fallback() {
  return _fallback;
}

bool log_fs_ok() {
  return _fs_ok();
}

// ─── Фича 11: стрим CSV за указанную дату ────────────────────────────────
// date — строка вида "DD.MM.YYYY"; если пустая — отдаём весь файл
size_t log_stream_csv_date(Stream &out, const String &date) {
  if (!_fs_ok() || !_fs_exists(LOG_FILE)) return 0;
  File f = _fs_open_read(LOG_FILE);
  if (!f) return 0;

  // Нормализуем date к "DD.MM.YYYY" один раз (без повторных String-аллокаций)
  char cmpDate[12] = {0};
  bool hasFilter = (date.length() == 10);
  if (hasFilter) {
    if (date.charAt(4) == '-') {
      // "YYYY-MM-DD" → "DD.MM.YYYY"
      snprintf(cmpDate, sizeof(cmpDate), "%.2s.%.2s.%.4s",
               date.c_str()+8, date.c_str()+5, date.c_str());
    } else {
      memcpy(cmpDate, date.c_str(), 10);
      cmpDate[10] = '\0';
    }
  }

  // Всегда печатаем заголовок
  out.print(CSV_HEADER);
  size_t count = 0;

  // Читаем построчно через char-буфер (без readStringUntil / String)
  char buf[128];
  int pos = 0;
  bool headerSkipped = false;

  while (f.available()) {
    int c = f.read();
    if (c < 0) break;
    if (c == '\n' || c == '\r') {
      if (pos == 0) continue;  // пустая строка
      buf[pos] = '\0';
      if (!headerSkipped) {
        headerSkipped = true;
        pos = 0;
        continue;
      }
      // Skip duplicate headers from log rotation
      if (pos > 8 && memcmp(buf, "datetime", 8) == 0) { pos = 0; continue; }
      if (pos > 11 && memcmp(buf, "\xEF\xBB\xBF" "datetime", 11) == 0) { pos = 0; continue; }
      // Фильтр по дате: первые 10 символов строки
      if (hasFilter && (pos < 10 || memcmp(buf, cmpDate, 10) != 0)) {
        pos = 0;
        continue;
      }
      out.write((const uint8_t*)buf, pos);
      out.print('\n');
      count++;
      pos = 0;
    } else {
      if (pos < (int)sizeof(buf) - 1) buf[pos++] = (char)c;
    }
  }
  // Последняя строка без '\n'
  if (pos > 0) {
    buf[pos] = '\0';
    if (!headerSkipped) { /* only header, no data */ }
    else if (!hasFilter || (pos >= 10 && memcmp(buf, cmpDate, 10) == 0)) {
      out.write((const uint8_t*)buf, pos);
      out.print('\n');
      count++;
    }
  }
  f.close();
  return count;
}

// ─── Нормализация даты к компактному виду "YYYYMMDD" для лекс-сравнения ──
// Входы: "YYYY-MM-DD", "DD.MM.YYYY", или пусто. Возвращает true если успешно.
static bool _date_to_yyyymmdd(const char *src, int srcLen, char out[9]) {
  if (srcLen < 10 || !src) return false;
  if (src[4] == '-') {
    // YYYY-MM-DD
    memcpy(out, src, 4);     // YYYY
    memcpy(out + 4, src + 5, 2);  // MM
    memcpy(out + 6, src + 8, 2);  // DD
  } else if (src[2] == '.') {
    // DD.MM.YYYY
    memcpy(out, src + 6, 4); // YYYY
    memcpy(out + 4, src + 3, 2);  // MM
    memcpy(out + 6, src, 2);      // DD
  } else {
    return false;
  }
  out[8] = '\0';
  // Проверяем что все 8 символов — цифры
  for (int i = 0; i < 8; i++) if (out[i] < '0' || out[i] > '9') return false;
  return true;
}

// Внутренний хелпер: ходит по строкам лога, для каждой записи в диапазоне
// вызывает onRow(buf, pos) и onPreRow() перед первой записью.
// Возвращает кол-во обработанных строк.
typedef void (*RowEmitFn)(const char *buf, int pos, void *ctx);
static size_t _stream_filter_range(const String &from, const String &to,
                                   RowEmitFn onRow, void *ctx,
                                   bool *anyEmitted = nullptr) {
  if (!_fs_ok() || !_fs_exists(LOG_FILE)) return 0;
  File f = _fs_open_read(LOG_FILE);
  if (!f) return 0;

  char fromCmp[9] = {0}, toCmp[9] = {0};
  bool hasFrom = _date_to_yyyymmdd(from.c_str(), from.length(), fromCmp);
  bool hasTo   = _date_to_yyyymmdd(to.c_str(),   to.length(),   toCmp);

  char buf[128];
  int pos = 0;
  bool headerSkipped = false;
  size_t count = 0;
  int byteCount = 0;

  auto processLine = [&]() {
    if (pos == 0) return;
    buf[pos] = '\0';
    if (!headerSkipped) { headerSkipped = true; pos = 0; return; }
    if (pos > 8 && memcmp(buf, "datetime", 8) == 0) { pos = 0; return; }
    if (pos > 11 && memcmp(buf, "\xEF\xBB\xBF" "datetime", 11) == 0) { pos = 0; return; }
    if (pos < 10) { pos = 0; return; }
    // Первые 10 байт строки лога = "DD.MM.YYYY" — конвертируем в YYYYMMDD для сравнения
    char rowCmp[9];
    if (!_date_to_yyyymmdd(buf, 10, rowCmp)) { pos = 0; return; }
    if (hasFrom && memcmp(rowCmp, fromCmp, 8) < 0) { pos = 0; return; }
    if (hasTo   && memcmp(rowCmp, toCmp,   8) > 0) { pos = 0; return; }
    onRow(buf, pos, ctx);
    count++;
    pos = 0;
  };

  while (f.available()) {
    int c = f.read();
    if (c < 0) break;
    if ((++byteCount & 1023) == 0) yield();
    if (c == '\n' || c == '\r') {
      processLine();
    } else {
      if (pos < (int)sizeof(buf) - 1) buf[pos++] = (char)c;
    }
  }
  // Последняя строка без \n
  if (pos > 0) processLine();
  f.close();
  if (anyEmitted) *anyEmitted = (count > 0);
  return count;
}

// ─── Стрим CSV за диапазон дат ───────────────────────────────────────────
struct _CsvCtx { Stream *out; };
static void _emit_csv_row(const char *buf, int pos, void *ctx) {
  _CsvCtx *c = (_CsvCtx*)ctx;
  c->out->write((const uint8_t*)buf, pos);
  c->out->print('\n');
}

size_t log_stream_csv_range(Stream &out, const String &from, const String &to) {
  out.print(CSV_HEADER);
  _CsvCtx ctx; ctx.out = &out;
  return _stream_filter_range(from, to, _emit_csv_row, &ctx);
}

// ─── Стрим JSON за диапазон дат: [{"dt":"...","w":..,"t":..,"b":..},...] ─
struct _JsonCtx { Stream *out; bool first; };
static void _emit_json_row(const char *buf, int pos, void *ctx) {
  _JsonCtx *c = (_JsonCtx*)ctx;
  // Поля CSV: datetime;weight;temp;humidity;bat → s1..s4
  int s1=-1,s2=-1,s3=-1,s4=-1;
  for (int i = 0; i < pos; i++) {
    if (buf[i] == ';') {
      if      (s1 < 0) s1 = i;
      else if (s2 < 0) s2 = i;
      else if (s3 < 0) s3 = i;
      else if (s4 < 0) { s4 = i; break; }
    }
  }
  if (s1 < 0 || s2 < 0 || s3 < 0 || s4 < 0) return;
  int wLen = s2 - s1 - 1;
  int tLen = s3 - s2 - 1;
  int bLen = pos - s4 - 1;

  // commaToPoint и valid-чек (вес 0..500)
  char cb[16];
  // Парсим вес для валидации
  {
    char wb[16];
    int n = wLen >= (int)sizeof(wb) ? (int)sizeof(wb)-1 : wLen;
    for (int i = 0; i < n; i++) wb[i] = (buf[s1+1+i] == ',') ? '.' : buf[s1+1+i];
    wb[n] = '\0';
    float w = atof(wb);
    if (isnan(w) || isinf(w) || w < -5.0f || w > 500.0f) return;
  }

  if (!c->first) c->out->print(',');
  c->first = false;
  c->out->print(F("{\"dt\":\""));
  // dt = первые s1 символов
  c->out->write((const uint8_t*)buf, s1);
  c->out->print(F("\",\"w\":"));
  // commaToPoint inline
  {
    int n = wLen >= (int)sizeof(cb) ? (int)sizeof(cb)-1 : wLen;
    for (int i = 0; i < n; i++) cb[i] = (buf[s1+1+i] == ',') ? '.' : buf[s1+1+i];
    cb[n] = '\0';
    c->out->print(cb);
  }
  c->out->print(F(",\"t\":"));
  if (tLen <= 0) c->out->print(F("-99"));
  else {
    int n = tLen >= (int)sizeof(cb) ? (int)sizeof(cb)-1 : tLen;
    for (int i = 0; i < n; i++) cb[i] = (buf[s2+1+i] == ',') ? '.' : buf[s2+1+i];
    cb[n] = '\0';
    c->out->print(cb);
  }
  c->out->print(F(",\"b\":"));
  if (bLen <= 0) c->out->print('0');
  else {
    int n = bLen >= (int)sizeof(cb) ? (int)sizeof(cb)-1 : bLen;
    for (int i = 0; i < n; i++) cb[i] = (buf[s4+1+i] == ',') ? '.' : buf[s4+1+i];
    cb[n] = '\0';
    c->out->print(cb);
  }
  c->out->print('}');
}

size_t log_stream_period_json(Stream &out, const String &from, const String &to) {
  out.print('[');
  _JsonCtx ctx; ctx.out = &out; ctx.first = true;
  size_t n = _stream_filter_range(from, to, _emit_json_row, &ctx);
  out.print(']');
  return n;
}

// ─── Первая дата в логе (DD.MM.YYYY) для подсчёта дней наблюдений ────────
// Возвращает true и заполняет buf (минимум 11 символов) датой первой записи
bool log_first_date(char *buf, size_t bufLen) {
  if (bufLen < 11 || !_fs_ok() || !_fs_exists(LOG_FILE)) return false;
  File f = _fs_open_read(LOG_FILE);
  if (!f) return false;
  // пропустить заголовок
  int byteCount = 0;
  while (f.available()) { int c = f.read(); if ((++byteCount & 1023) == 0) { yield(); } if (c == '\n' || c < 0) break; }
  char ln[48];
  while (f.available()) {
    int pos = 0;
    while (f.available()) {
      int c = f.read();
      if ((++byteCount & 1023) == 0) { yield(); }
      if (c == '\n' || c == '\r' || c < 0) break;
      if (pos < (int)sizeof(ln) - 1) ln[pos++] = (char)c;
    }
    ln[pos] = '\0';
    // trim trailing spaces
    while (pos > 0 && ln[pos - 1] == ' ') ln[--pos] = '\0';
    if (pos < 10) continue;
    if (strstr(ln, "datetime") || strstr(ln, "weight_kg")) continue;
    // Первые 10 символов = "DD.MM.YYYY"
    memcpy(buf, ln, 10);
    buf[10] = '\0';
    f.close();
    return true;
  }
  f.close();
  return false;
}

// ─── Фича 12: суточная статистика min/max вес и температура ──────────────
// Парсинг CSV на char-буфере (без String аллокаций в цикле — защита от heap-фрагментации)
DayStat log_day_stat(const String &todayDate) {
  DayStat s;
  s.wMin = 1e9f; s.wMax = -1e9f;
  s.tMin = 1e9f; s.tMax = -1e9f;
  s.count = 0; s.valid = false;

  if (!_fs_ok() || !_fs_exists(LOG_FILE)) return s;
  File f = _fs_open_read(LOG_FILE);
  if (!f) return s;

  // Нормализуем дату к "DD.MM.YYYY" один раз
  char cmpDate[12] = {0};
  bool hasFilter = (todayDate.length() >= 10);
  if (hasFilter) {
    if (todayDate.charAt(4) == '-') {
      snprintf(cmpDate, sizeof(cmpDate), "%.2s.%.2s.%.4s",
               todayDate.c_str()+8, todayDate.c_str()+5, todayDate.c_str());
    } else {
      memcpy(cmpDate, todayDate.c_str(), 10);
      cmpDate[10] = '\0';
    }
  }

  // Побайтовое чтение — без readStringUntil / String в цикле
  char buf[128];
  int pos = 0;
  bool headerSkipped = false;
  int byteCount = 0;

  while (f.available()) {
    int c = f.read();
    if (c < 0) break;
    if ((++byteCount & 1023) == 0) yield();
    if (c == '\n' || c == '\r') {
      if (pos == 0) continue;
      buf[pos] = '\0';
      if (!headerSkipped) { headerSkipped = true; pos = 0; continue; }

      // Пропускаем повторные заголовки
      if (pos > 8 && memcmp(buf, "datetime", 8) == 0) { pos = 0; continue; }

      // Фильтр по дате: первые 10 символов
      if (hasFilter && (pos < 10 || memcmp(buf, cmpDate, 10) != 0)) { pos = 0; continue; }

      // Парсим поля: datetime;weight;temp;...
      // Находим разделители ';'
      int c1 = -1, c2 = -1, c3 = -1;
      for (int i = 0; i < pos; i++) {
        if (buf[i] == ';') {
          if (c1 < 0) c1 = i;
          else if (c2 < 0) c2 = i;
          else if (c3 < 0) { c3 = i; break; }
        }
      }
      if (c1 < 0 || c2 < 0 || c3 < 0) { pos = 0; continue; }

      float w = commaToFloat(buf + c1 + 1, c2 - c1 - 1);
      int tLen = c3 - c2 - 1;
      float t = (tLen <= 0) ? -99.0f : commaToFloat(buf + c2 + 1, tLen);

      if (isnan(w) || w < -5.0f || w > 500.0f) { pos = 0; continue; }

      if (w < s.wMin) s.wMin = w;
      if (w > s.wMax) s.wMax = w;
      if (!isnan(t) && t > -90.0f) {
        if (t < s.tMin) s.tMin = t;
        if (t > s.tMax) s.tMax = t;
      }
      s.count++;
      pos = 0;
    } else {
      if (pos < (int)sizeof(buf) - 1) buf[pos++] = (char)c;
    }
  }
  // Process last line without trailing newline
  if (pos > 0 && headerSkipped) {
    buf[pos] = '\0';
    // Skip repeated headers
    if (!(pos > 8 && memcmp(buf, "datetime", 8) == 0)) {
      if (!hasFilter || (pos >= 10 && memcmp(buf, cmpDate, 10) == 0)) {
        int c1 = -1, c2 = -1, c3 = -1;
        for (int i = 0; i < pos; i++) {
          if (buf[i] == ';') {
            if (c1 < 0) c1 = i;
            else if (c2 < 0) c2 = i;
            else if (c3 < 0) { c3 = i; break; }
          }
        }
        if (c1 >= 0 && c2 >= 0 && c3 >= 0) {
          float w = commaToFloat(buf + c1 + 1, c2 - c1 - 1);
          int tLen = c3 - c2 - 1;
          float t = (tLen <= 0) ? -99.0f : commaToFloat(buf + c2 + 1, tLen);
          if (!isnan(w) && w >= -5.0f && w <= 500.0f) {
            if (w < s.wMin) s.wMin = w;
            if (w > s.wMax) s.wMax = w;
            if (!isnan(t) && t > -90.0f) {
              if (t < s.tMin) s.tMin = t;
              if (t > s.tMax) s.tMax = t;
            }
            s.count++;
          }
        }
      }
    }
  }
  f.close();

  if (s.count > 0) {
    s.valid = true;
    // Reset temperature sentinels if no valid temps were found
    if (s.tMin > 1e8f) s.tMin = 0;
    if (s.tMax < -1e8f) s.tMax = 0;
  } else { s.wMin=0; s.wMax=0; s.tMin=0; s.tMax=0; }
  return s;
}

// ─── Парсит CSV-лог и возвращает JSON-массив для графика/экспорта ────────
// Формат CSV: datetime;weight_kg;temp_c;humidity_pct;bat_v
// Парсинг на char-буфере (без String аллокаций в цикле — защита от heap-фрагментации)
// Стрим-версия: пишет JSON-массив прямо в Stream (без heap-аккумуляции).
// Формат идентичен log_to_json(). На ESP8266 экономит ~4КБ heap.
size_t log_stream_json(Stream &out, int maxRows) {
#if defined(ESP8266)
  if (maxRows > 50) maxRows = 50;
#else
  if (maxRows > 200) maxRows = 200;
#endif
  if (!_fs_ok() || !_fs_exists(LOG_FILE)) { out.print("[]"); return 0; }
  File f = _fs_open_read(LOG_FILE);
  if (!f) { out.print("[]"); return 0; }

  int totalLines = 0;
  while (f.available()) {
    int c = f.read();
    if (c < 0) break;
    if (c == '\n') {
      totalLines++;
      if ((totalLines & 63) == 0) yield();
    }
  }
  f.seek(0);

  int dataLines = totalLines - 1;
  int skipLines = (dataLines > maxRows) ? (dataLines - maxRows) : 0;

  out.print('[');
  bool first = true;
  int lineIdx = 0;
  char buf[128];
  int pos = 0;
  bool headerSkipped = false;
  size_t rows = 0;

  while (f.available()) {
    int ch = f.read();
    if (ch < 0) break;
    if (ch == '\n' || ch == '\r') {
      if (pos == 0) continue;
      buf[pos] = '\0';
      if (!headerSkipped) { headerSkipped = true; pos = 0; continue; }
      if (pos > 8 && memcmp(buf, "datetime", 8) == 0) { pos = 0; continue; }
      if (pos > 11 && memcmp(buf, "\xEF\xBB\xBF" "datetime", 11) == 0) { pos = 0; continue; }
      if (lineIdx++ < skipLines) { pos = 0; continue; }

      int s1 = -1, s2 = -1, s3 = -1, s4 = -1;
      for (int i = 0; i < pos; i++) {
        if (buf[i] == ';') {
          if      (s1 < 0) s1 = i;
          else if (s2 < 0) s2 = i;
          else if (s3 < 0) s3 = i;
          else if (s4 < 0) { s4 = i; break; }
        }
      }
      if (s1 < 0 || s2 < 0 || s3 < 0 || s4 < 0) { pos = 0; continue; }

      int wLen = s2 - s1 - 1;
      int tLen = s3 - s2 - 1;
      int hLen = s4 - s3 - 1;
      int bLen = pos - s4 - 1;

      float w = commaToFloat(buf + s1 + 1, wLen);
      if (isnan(w) || isinf(w) || w < -5.0f || w > 500.0f) { pos = 0; continue; }

      char cb[16];
      if (!first) out.print(',');
      out.print(F("{\"dt\":\""));
      { char saveCh = buf[s1]; buf[s1] = '\0'; out.print(buf); buf[s1] = saveCh; }
      out.print(F("\",\"w\":"));
      out.print(commaToPoint(buf + s1 + 1, wLen, cb, sizeof(cb)));
      out.print(F(",\"t\":"));
      if (tLen <= 0) out.print(F("-99")); else out.print(commaToPoint(buf + s2 + 1, tLen, cb, sizeof(cb)));
      out.print(F(",\"h\":"));
      if (hLen <= 0) out.print(F("-99")); else out.print(commaToPoint(buf + s3 + 1, hLen, cb, sizeof(cb)));
      out.print(F(",\"b\":"));
      if (bLen <= 0) out.print('0'); else out.print(commaToPoint(buf + s4 + 1, bLen, cb, sizeof(cb)));
      out.print('}');
      first = false;
      rows++;
      pos = 0;
      if ((rows & 15) == 0) {
        yield();
#if defined(ESP8266)
        ESP.wdtFeed();
#endif
      }
    } else {
      if (pos < (int)sizeof(buf) - 1) buf[pos++] = (char)ch;
    }
  }
  f.close();
  out.print(']');
  return rows;
}

String log_to_json(int maxRows) {
  // Ограничиваем максимум на ESP8266 — heap ~40 КБ, каждая строка ~80 байт JSON
#if defined(ESP8266)
  if (maxRows > 50) maxRows = 50;
#else
  if (maxRows > 200) maxRows = 200;
#endif
  if (!_fs_ok() || !_fs_exists(LOG_FILE)) return "[]";
  File f = _fs_open_read(LOG_FILE);
  if (!f) return "[]";

  // Считаем строки чтобы пропустить лишние
  int totalLines = 0;
  while (f.available()) {
    int c = f.read();
    if (c < 0) break;
    if (c == '\n') {
      totalLines++;
      if ((totalLines & 63) == 0) yield();  // WDT safe: yield каждые 64 строки
    }
  }
  f.seek(0);

  int dataLines = totalLines - 1;
  int skipLines = (dataLines > maxRows) ? (dataLines - maxRows) : 0;

  String out = "[";
  // Pre-allocate: ~80 байт JSON на строку, снижает фрагментацию heap на ESP8266
  int rows = (dataLines < 0) ? 0 : (dataLines < maxRows ? dataLines : maxRows);
  if (!out.reserve(2 + rows * 80)) { f.close(); return "[]"; }
  bool first = true;
  int lineIdx = 0;

  // Побайтовое чтение — без readStringUntil / String в цикле
  char buf[128];
  int pos = 0;
  bool headerSkipped = false;

  while (f.available()) {
    int ch = f.read();
    if (ch < 0) break;
    if (ch == '\n' || ch == '\r') {
      if (pos == 0) continue;
      buf[pos] = '\0';

      if (!headerSkipped) { headerSkipped = true; pos = 0; continue; }

      // Пропускаем повторные заголовки (до lineIdx, чтобы не влияли на skipLines)
      if (pos > 8 && memcmp(buf, "datetime", 8) == 0) { pos = 0; continue; }
      if (pos > 11 && memcmp(buf, "\xEF\xBB\xBF" "datetime", 11) == 0) { pos = 0; continue; }
      if (lineIdx++ < skipLines) { pos = 0; continue; }

      // Находим 4 разделителя ';'
      int s1 = -1, s2 = -1, s3 = -1, s4 = -1;
      for (int i = 0; i < pos; i++) {
        if (buf[i] == ';') {
          if      (s1 < 0) s1 = i;
          else if (s2 < 0) s2 = i;
          else if (s3 < 0) s3 = i;
          else if (s4 < 0) { s4 = i; break; }
        }
      }
      if (s1 < 0 || s2 < 0 || s3 < 0 || s4 < 0) { pos = 0; continue; }

      // Поля: dt=[0..s1), w=[s1+1..s2), t=[s2+1..s3), h=[s3+1..s4), b=[s4+1..pos)
      int wLen = s2 - s1 - 1;
      int tLen = s3 - s2 - 1;
      int hLen = s4 - s3 - 1;
      int bLen = pos - s4 - 1;

      // Валидация веса
      float w = commaToFloat(buf + s1 + 1, wLen);
      if (isnan(w) || isinf(w) || w < -5.0f || w > 500.0f) { pos = 0; continue; }

      // Строим JSON-объект
      char cb[16];
      if (!first) out += ',';
      out += F("{\"dt\":\"");
      // dt = buf[0..s1)
      { char saveCh = buf[s1]; buf[s1] = '\0'; out += buf; buf[s1] = saveCh; }
      out += F("\",\"w\":");
      out += commaToPoint(buf + s1 + 1, wLen, cb, sizeof(cb));
      out += F(",\"t\":");
      if (tLen <= 0) out += F("-99"); else out += commaToPoint(buf + s2 + 1, tLen, cb, sizeof(cb));
      out += F(",\"h\":");
      if (hLen <= 0) out += F("-99"); else out += commaToPoint(buf + s3 + 1, hLen, cb, sizeof(cb));
      out += F(",\"b\":");
      if (bLen <= 0) out += F("0"); else out += commaToPoint(buf + s4 + 1, bLen, cb, sizeof(cb));
      out += '}';
      first = false;
      pos = 0;
    } else {
      if (pos < (int)sizeof(buf) - 1) buf[pos++] = (char)ch;
    }
  }
  f.close();
  out += ']';
  return out;
}

// ─── Бэкап/восстановление настроек на SD/LittleFS ─────────────────────

bool log_save_backup(const String &json) {
  if (!_fs_ok()) return false;
  File f = _fs_open_write(BACKUP_FILE);
  if (!f) return false;
  f.print(json);
  f.close();
  Serial.println(F("[Log] Backup saved to SD/FS"));
  return true;
}

String log_read_backup() {
  if (!_fs_ok() || !_fs_exists(BACKUP_FILE)) return "";
  File f = _fs_open_read(BACKUP_FILE);
  if (!f) return "";
  // Защита от OOM: backup JSON не может быть больше ~2KB
  size_t sz = f.size();
  if (sz > 4096) {
    f.close();
    Serial.println(F("[Log] Backup file too large, skipping read"));
    return "";
  }
  String json = f.readString();
  f.close();
  return json;
}

size_t log_stream_backup(Stream &out) {
  if (!_fs_ok() || !_fs_exists(BACKUP_FILE)) return 0;
  File f = _fs_open_read(BACKUP_FILE);
  if (!f) return 0;
  // Стрим чанками в out — без аккумуляции в String (экономия ~4КБ heap на ESP8266).
  uint8_t buf[256];
  size_t total = 0;
  while (f.available()) {
    int n = f.read(buf, sizeof(buf));
    if (n <= 0) break;
    out.write(buf, n);
    total += (size_t)n;
    yield();
#if defined(ESP8266)
    ESP.wdtFeed();
#endif
  }
  f.close();
  return total;
}

// ─── Стрим Excel-таблицы за диапазон (v5.0.69) ───────────────────────────
// Голый CSV — колонка цифр, в которой ничего не видно. Здесь весы сами красят:
// вес жёлтой заливкой, температура сине-жёлто-красной шкалой, батарея зелёной
// полосой с красной отсечкой на пороге алерта, изменение веса зелёным/красным.
// Excel показывает предупреждение «формат не совпадает с расширением» — это
// нормально для HTML-таблицы под именем .xls, надо нажать «Да».

static const char XLS_HEAD[] PROGMEM =
  "<html xmlns:x=\"urn:schemas-microsoft-com:office:excel\"><head>"
  "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\">"
  "<style>"
  "table{border-collapse:collapse;font-family:Calibri,Arial;font-size:11pt}"
  "td,th{border:1px solid #C8C8C0;padding:3px 8px;text-align:center;mso-number-format:General}"
  "th{background:#1F3A2E;color:#F5A623;font-weight:bold}"
  ".dt{text-align:left;mso-number-format:'\@'}"
  ".w{background:#FFF3CD;font-weight:bold}"
  ".up{color:#1E7B34;font-weight:bold}.dn{color:#C0392B;font-weight:bold}"
  ".t0{background:#9DC3E6}.t1{background:#DEEAF6}.t2{background:#FFF2CC}"
  ".t3{background:#F8CBAD}.t4{background:#E06666;color:#fff}"
  ".b0{background:#FFC7CE;color:#9C0006;font-weight:bold}"
  ".b1{background:#FFEB9C}.b2{background:#E2EFDA}.b3{background:#C6E0B4}"
  "</style></head><body>"
  "<table><tr>"
  "<th>&#1044;&#1072;&#1090;&#1072; &#1080; &#1074;&#1088;&#1077;&#1084;&#1103;</th>"
  "<th>&#1042;&#1077;&#1089;, &#1082;&#1075;</th>"
  "<th>&#1048;&#1079;&#1084;., &#1082;&#1075;</th>"
  "<th>&#1058;&#1077;&#1084;&#1087;., &#176;C</th>"
  "<th>&#1041;&#1072;&#1090;&#1072;&#1088;&#1077;&#1103;, &#1042;</th>"
  "<th>&#1047;&#1072;&#1088;&#1103;&#1076;, %</th></tr>\n";

struct _XlsCtx { Stream *out; float prevW; bool hasPrev; };

// Кривая LiPo — та же что в Battery.cpp:53-59, иначе проценты в файле
// разойдутся с тем, что показывают весы на экране и в Telegram.
static int _xls_bat_pct(float v) {
  float pct;
  if      (v >= 4.10f) pct = 95 + (v - 4.10f) / (4.20f - 4.10f) * 5.0f;
  else if (v >= 3.90f) pct = 75 + (v - 3.90f) / (4.10f - 3.90f) * 20.0f;
  else if (v >= 3.75f) pct = 45 + (v - 3.75f) / (3.90f - 3.75f) * 30.0f;
  else if (v >= 3.60f) pct = 15 + (v - 3.60f) / (3.75f - 3.60f) * 30.0f;
  else if (v >= 3.40f) pct =  5 + (v - 3.40f) / (3.60f - 3.40f) * 10.0f;
  else if (v >= 3.00f) pct = (v - 3.00f) / (3.40f - 3.00f) * 5.0f;
  else pct = 0.0f;
  if (pct < 0) pct = 0; if (pct > 100) pct = 100;
  return (int)pct;
}

static void _emit_xls_row(const char *buf, int pos, void *ctxv) {
  _XlsCtx *c = (_XlsCtx*)ctxv;
  Stream *o = c->out;

  // Разбор строки лога: datetime;weight;temp;humidity;bat
  int s1=-1,s2=-1,s3=-1,s4=-1;
  for (int i = 0; i < pos; i++) {
    if (buf[i] == ';') {
      if      (s1 < 0) s1 = i;
      else if (s2 < 0) s2 = i;
      else if (s3 < 0) s3 = i;
      else if (s4 < 0) { s4 = i; break; }
    }
  }
  if (s1 < 0 || s2 < 0 || s3 < 0 || s4 < 0) return;

  auto fieldF = [&](int a, int b) -> float {
    char tmp[16]; int n = b - a - 1;
    if (n <= 0) return NAN;
    if (n >= (int)sizeof(tmp)) n = (int)sizeof(tmp) - 1;
    for (int i = 0; i < n; i++) tmp[i] = (buf[a+1+i] == ',') ? '.' : buf[a+1+i];
    tmp[n] = '\0';
    return atof(tmp);
  };
  float w = fieldF(s1, s2);
  float t = fieldF(s2, s3);
  float b = fieldF(s4, pos + 1);
  if (isnan(w) || w < -5.0f || w > 500.0f) return;

  // Дата — текстом, иначе Excel в русской локали жуёт DD.MM.YYYY по-своему
  o->print(F("<tr><td class=\"dt\">"));
  o->write((const uint8_t*)buf, s1);
  o->print(F("</td><td class=\"w\">"));
  o->write((const uint8_t*)(buf + s1 + 1), s2 - s1 - 1);   // вес как есть, с запятой
  o->print(F("</td>"));

  // Изменение к предыдущему замеру
  if (c->hasPrev) {
    float d = w - c->prevW;
    const char *cls = (d > 0.05f) ? "up" : (d < -0.05f ? "dn" : "");
    o->print(F("<td class=\"")); o->print(cls); o->print(F("\">"));
    char db[16];
    snprintf(db, sizeof(db), "%+.2f", d);
    for (char *p = db; *p; p++) if (*p == '.') *p = ',';
    o->print(db);
    o->print(F("</td>"));
  } else {
    o->print(F("<td></td>"));
  }
  c->prevW = w; c->hasPrev = true;

  // Температура — 5 ступеней от синего к красному
  const char *tc = "t2";
  if (!isnan(t)) {
    if      (t < 10.0f) tc = "t0";
    else if (t < 20.0f) tc = "t1";
    else if (t < 28.0f) tc = "t2";
    else if (t < 35.0f) tc = "t3";
    else                tc = "t4";
  }
  o->print(F("<td class=\"")); o->print(tc); o->print(F("\">"));
  o->write((const uint8_t*)(buf + s2 + 1), s3 - s2 - 1);
  o->print(F("</td>"));

  // Батарея: ниже 3.6 В (порог алерта) — красным
  int pct = isnan(b) ? 0 : _xls_bat_pct(b);
  const char *bc = (b < 3.6f) ? "b0" : (pct < 50 ? "b1" : (pct < 80 ? "b2" : "b3"));
  o->print(F("<td class=\"")); o->print(bc); o->print(F("\">"));
  o->write((const uint8_t*)(buf + s4 + 1), pos - s4 - 1);
  o->print(F("</td><td class=\"")); o->print(bc); o->print(F("\">"));
  o->print(pct);
  o->print(F("</td></tr>\n"));
}

size_t log_stream_xls_range(Stream &out, const String &from, const String &to) {
  out.print((const __FlashStringHelper*)XLS_HEAD);
  _XlsCtx ctx; ctx.out = &out; ctx.prevW = 0.0f; ctx.hasPrev = false;
  size_t n = _stream_filter_range(from, to, _emit_xls_row, &ctx);
  out.print(F("</table></body></html>"));
  return n;
}

// ═══ Выгрузка в Excel: три листа (v5.0.71) ════════════════════════════════
// v5.0.69 отдавал HTML-таблицу — она даёт ровно один лист, потому что Excel в
// HTML-формате хранит каждый лист отдельным файлом. SpreadsheetML 2003 (XML)
// умеет несколько листов в одном файле и поддерживает заливки, цвет шрифта и
// числовые форматы. Графиков этот формат не умеет — они остаются за
// tools/csv_to_xlsx.py.
//
// Файл собирается за три прохода по логу (он маленький, чтение дешёвое):
//   1) статистика для листа «Сводка» — её надо знать ДО того, как писать первый лист;
//   2) построчные «Замеры»;
//   3) «По дням» — группировка на лету: строки в логе идут по возрастанию даты,
//      поэтому достаточно отследить смену первых 10 символов (DD.MM.YYYY).

struct _StatCtx {
  uint32_t rows;
  char  dtFirst[20], dtLast[20];
  char  dayPrev[11];
  uint16_t days;
  float wFirst, wLast, wMin, wMax;
  float tMin, tMax;
  float bFirst, bLast;
  bool  first;
};

// Разбор строки лога на поля. Возвращает false если строка битая.
static bool _parse_row(const char *buf, int pos, float &w, float &t, float &b,
                       int &dtLen) {
  int s1=-1,s2=-1,s3=-1,s4=-1;
  for (int i = 0; i < pos; i++) {
    if (buf[i] == ';') {
      if      (s1 < 0) s1 = i;
      else if (s2 < 0) s2 = i;
      else if (s3 < 0) s3 = i;
      else if (s4 < 0) { s4 = i; break; }
    }
  }
  if (s1 < 0 || s2 < 0 || s3 < 0 || s4 < 0) return false;
  dtLen = s1;
  char tmp[16];
  auto grab = [&](int a, int bEnd) -> float {
    int n = bEnd - a - 1;
    if (n <= 0) return NAN;
    if (n >= (int)sizeof(tmp)) n = (int)sizeof(tmp) - 1;
    for (int i = 0; i < n; i++) tmp[i] = (buf[a+1+i] == ',') ? '.' : buf[a+1+i];
    tmp[n] = 0;
    return atof(tmp);
  };
  w = grab(s1, s2);
  t = grab(s2, s3);
  b = grab(s4, pos + 1);
  if (isnan(w) || w < -5.0f || w > 500.0f) return false;
  return true;
}

static void _stat_row(const char *buf, int pos, void *ctxv) {
  _StatCtx *s = (_StatCtx*)ctxv;
  float w, t, b; int dtLen;
  if (!_parse_row(buf, pos, w, t, b, dtLen)) return;

  if (dtLen > 19) dtLen = 19;
  if (s->first) {
    memcpy(s->dtFirst, buf, dtLen); s->dtFirst[dtLen] = 0;
    s->wFirst = w; s->bFirst = b;
    s->wMin = s->wMax = w;
    s->tMin = s->tMax = t;
    s->first = false;
  }
  memcpy(s->dtLast, buf, dtLen); s->dtLast[dtLen] = 0;
  s->wLast = w; s->bLast = b;
  if (w < s->wMin) s->wMin = w;
  if (w > s->wMax) s->wMax = w;
  if (!isnan(t)) {
    if (isnan(s->tMin) || t < s->tMin) s->tMin = t;
    if (isnan(s->tMax) || t > s->tMax) s->tMax = t;
  }
  if (memcmp(s->dayPrev, buf, 10) != 0) {
    memcpy(s->dayPrev, buf, 10); s->dayPrev[10] = 0;
    s->days++;
  }
  s->rows++;
}

// ─── Лист «Замеры» ────────────────────────────────────────────────────────
struct _RowsCtx { Stream *out; float prevW; bool hasPrev; };

static void _xml_cell_str(Stream *o, const char *p, int n, const char *style) {
  o->print(F("<Cell ss:StyleID=\"")); o->print(style);
  o->print(F("\"><Data ss:Type=\"String\">"));
  o->write((const uint8_t*)p, n);
  o->print(F("</Data></Cell>"));
}

static void _xml_cell_num(Stream *o, float v, const char *style, int dec) {
  char nb[16];
  if (isnan(v)) { o->print(F("<Cell ss:StyleID=\"")); o->print(style); o->print(F("\"/>")); return; }
  dtostrf(v, 0, dec, nb);
  o->print(F("<Cell ss:StyleID=\"")); o->print(style);
  o->print(F("\"><Data ss:Type=\"Number\">")); o->print(nb);
  o->print(F("</Data></Cell>"));
}

static const char* _temp_style(float t) {
  if (isnan(t))   return "t2";
  if (t < 10.0f)  return "t0";
  if (t < 20.0f)  return "t1";
  if (t < 28.0f)  return "t2";
  if (t < 35.0f)  return "t3";
  return "t4";
}

static const char* _bat_style(float b, int pct) {
  if (isnan(b))   return "b2";
  if (b < 3.6f)   return "b0";
  if (pct < 50)   return "b1";
  if (pct < 80)   return "b2";
  return "b3";
}

static void _emit_xml_row(const char *buf, int pos, void *ctxv) {
  _RowsCtx *c = (_RowsCtx*)ctxv;
  Stream *o = c->out;
  float w, t, b; int dtLen;
  if (!_parse_row(buf, pos, w, t, b, dtLen)) return;

  o->print(F("<Row>"));
  _xml_cell_str(o, buf, dtLen, "dt");
  _xml_cell_num(o, w, "w", 2);
  if (c->hasPrev) {
    float d = w - c->prevW;
    _xml_cell_num(o, d, (d > 0.05f) ? "up" : (d < -0.05f ? "dn" : "num"), 2);
  } else {
    o->print(F("<Cell ss:StyleID=\"num\"/>"));
  }
  c->prevW = w; c->hasPrev = true;
  _xml_cell_num(o, t, _temp_style(t), 1);
  int pct = isnan(b) ? 0 : _xls_bat_pct(b);
  _xml_cell_num(o, b, _bat_style(b, pct), 2);
  _xml_cell_num(o, (float)pct, _bat_style(b, pct), 0);
  o->print(F("</Row>"));
}

// ─── Лист «По дням» ───────────────────────────────────────────────────────
struct _DayCtx {
  Stream *out;
  char day[11];
  bool has;
  uint16_t n;
  float wLast, wMin, wMax, tMin, tMax, bLast;
  float prevDayLast;
  bool  hasPrevDay;
};

static void _day_flush(_DayCtx *d) {
  if (!d->has) return;
  Stream *o = d->out;
  o->print(F("<Row>"));
  _xml_cell_str(o, d->day, 10, "dt");
  _xml_cell_num(o, (float)d->n, "num", 0);
  _xml_cell_num(o, d->wLast, "w", 2);
  if (d->hasPrevDay) {
    float g = d->wLast - d->prevDayLast;
    _xml_cell_num(o, g, (g > 0.05f) ? "up" : (g < -0.05f ? "dn" : "num"), 2);
  } else {
    o->print(F("<Cell ss:StyleID=\"num\"/>"));
  }
  _xml_cell_num(o, d->wMin, "num", 2);
  _xml_cell_num(o, d->wMax, "num", 2);
  _xml_cell_num(o, d->tMin, "num", 1);
  _xml_cell_num(o, d->tMax, "num", 1);
  int pct = isnan(d->bLast) ? 0 : _xls_bat_pct(d->bLast);
  _xml_cell_num(o, d->bLast, _bat_style(d->bLast, pct), 2);
  o->print(F("</Row>"));
  d->prevDayLast = d->wLast;
  d->hasPrevDay = true;
}

static void _emit_day_row(const char *buf, int pos, void *ctxv) {
  _DayCtx *d = (_DayCtx*)ctxv;
  float w, t, b; int dtLen;
  if (!_parse_row(buf, pos, w, t, b, dtLen)) return;

  if (!d->has || memcmp(d->day, buf, 10) != 0) {
    _day_flush(d);
    memcpy(d->day, buf, 10); d->day[10] = 0;
    d->has = true; d->n = 0;
    d->wMin = d->wMax = w;
    d->tMin = d->tMax = t;
  }
  d->n++;
  d->wLast = w; d->bLast = b;
  if (w < d->wMin) d->wMin = w;
  if (w > d->wMax) d->wMax = w;
  if (!isnan(t)) {
    if (isnan(d->tMin) || t < d->tMin) d->tMin = t;
    if (isnan(d->tMax) || t > d->tMax) d->tMax = t;
  }
}

// ─── Шапка книги: стили ───────────────────────────────────────────────────
static void _xml_head(Stream &o) {
  o.print(F("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<?mso-application progid=\"Excel.Sheet\"?>\n"
            "<Workbook xmlns=\"urn:schemas-microsoft-com:office:spreadsheet\""
            " xmlns:o=\"urn:schemas-microsoft-com:office:office\""
            " xmlns:x=\"urn:schemas-microsoft-com:office:excel\""
            " xmlns:ss=\"urn:schemas-microsoft-com:office:spreadsheet\">\n"
            "<Styles>"
            "<Style ss:ID=\"Default\" ss:Name=\"Normal\"><Alignment ss:Vertical=\"Center\"/>"
            "<Font ss:FontName=\"Calibri\" ss:Size=\"11\"/></Style>"
            "<Style ss:ID=\"hdr\"><Font ss:Bold=\"1\" ss:Color=\"#F5A623\"/>"
            "<Interior ss:Color=\"#1F3A2E\" ss:Pattern=\"Solid\"/>"
            "<Alignment ss:Horizontal=\"Center\" ss:Vertical=\"Center\" ss:WrapText=\"1\"/></Style>"
            "<Style ss:ID=\"ttl\"><Font ss:Bold=\"1\" ss:Size=\"14\" ss:Color=\"#1F3A2E\"/></Style>"
            "<Style ss:ID=\"key\"><Font ss:Bold=\"1\" ss:Color=\"#3C4A32\"/></Style>"
            "<Style ss:ID=\"dt\"><Alignment ss:Horizontal=\"Left\"/></Style>"
            "<Style ss:ID=\"num\"><Alignment ss:Horizontal=\"Center\"/></Style>"
            "<Style ss:ID=\"w\"><Font ss:Bold=\"1\"/><Interior ss:Color=\"#FFF3CD\" ss:Pattern=\"Solid\"/>"
            "<Alignment ss:Horizontal=\"Center\"/><NumberFormat ss:Format=\"0.00\"/></Style>"
            "<Style ss:ID=\"up\"><Font ss:Bold=\"1\" ss:Color=\"#1E7B34\"/>"
            "<Alignment ss:Horizontal=\"Center\"/><NumberFormat ss:Format=\"+0.00;-0.00;0.00\"/></Style>"
            "<Style ss:ID=\"dn\"><Font ss:Bold=\"1\" ss:Color=\"#C0392B\"/>"
            "<Alignment ss:Horizontal=\"Center\"/><NumberFormat ss:Format=\"+0.00;-0.00;0.00\"/></Style>"
            "<Style ss:ID=\"t0\"><Interior ss:Color=\"#9DC3E6\" ss:Pattern=\"Solid\"/><Alignment ss:Horizontal=\"Center\"/></Style>"
            "<Style ss:ID=\"t1\"><Interior ss:Color=\"#DEEAF6\" ss:Pattern=\"Solid\"/><Alignment ss:Horizontal=\"Center\"/></Style>"
            "<Style ss:ID=\"t2\"><Interior ss:Color=\"#FFF2CC\" ss:Pattern=\"Solid\"/><Alignment ss:Horizontal=\"Center\"/></Style>"
            "<Style ss:ID=\"t3\"><Interior ss:Color=\"#F8CBAD\" ss:Pattern=\"Solid\"/><Alignment ss:Horizontal=\"Center\"/></Style>"
            "<Style ss:ID=\"t4\"><Interior ss:Color=\"#E06666\" ss:Pattern=\"Solid\"/><Font ss:Color=\"#FFFFFF\"/><Alignment ss:Horizontal=\"Center\"/></Style>"
            "<Style ss:ID=\"b0\"><Interior ss:Color=\"#FFC7CE\" ss:Pattern=\"Solid\"/><Font ss:Bold=\"1\" ss:Color=\"#9C0006\"/><Alignment ss:Horizontal=\"Center\"/></Style>"
            "<Style ss:ID=\"b1\"><Interior ss:Color=\"#FFEB9C\" ss:Pattern=\"Solid\"/><Alignment ss:Horizontal=\"Center\"/></Style>"
            "<Style ss:ID=\"b2\"><Interior ss:Color=\"#E2EFDA\" ss:Pattern=\"Solid\"/><Alignment ss:Horizontal=\"Center\"/></Style>"
            "<Style ss:ID=\"b3\"><Interior ss:Color=\"#C6E0B4\" ss:Pattern=\"Solid\"/><Alignment ss:Horizontal=\"Center\"/></Style>"
            "</Styles>\n"));
}

// Значения сводки — строки, локаль Excel к ним не применяется: dtostrf даёт
// точку, а в соседних «Замерах» числа настоящие и рисуются с запятой. Приводим
// к одному виду.
static void _ru_dec(char *s) {
  for (char *p = s; *p; p++) if (*p == '.') *p = ',';
}

static void _xml_kv(Stream &o, const __FlashStringHelper *k, const char *v) {
  o.print(F("<Row><Cell ss:StyleID=\"key\"><Data ss:Type=\"String\">"));
  o.print(k);
  o.print(F("</Data></Cell><Cell><Data ss:Type=\"String\">"));
  o.print(v);
  o.print(F("</Data></Cell></Row>"));
}

size_t log_stream_xlsxml_range(Stream &out, const String &from, const String &to) {
  // Проход 1 — статистика для «Сводки»
  _StatCtx st;
  memset(&st, 0, sizeof(st));
  st.first = true;
  st.tMin = st.tMax = NAN;
  _stream_filter_range(from, to, _stat_row, &st);

  _xml_head(out);

  // ─── Лист «Сводка» ───
  out.print(F("<Worksheet ss:Name=\"Сводка\"><Table>"
              "<Column ss:Width=\"210\"/><Column ss:Width=\"260\"/>"
              "<Row><Cell ss:StyleID=\"ttl\"><Data ss:Type=\"String\">"
              "BeehiveScale — сводка по выгрузке</Data></Cell></Row><Row/>"));
  char b1[48];
  snprintf(b1, sizeof(b1), "%s — %s", st.dtFirst, st.dtLast);
  _xml_kv(out, F("Период"), b1);
  snprintf(b1, sizeof(b1), "%u", (unsigned)st.days);
  _xml_kv(out, F("Дней с замерами"), b1);
  snprintf(b1, sizeof(b1), "%lu", (unsigned long)st.rows);
  _xml_kv(out, F("Замеров"), b1);
  out.print(F("<Row/>"));
  {
    char w1[12], w2[12];
    dtostrf(st.wFirst, 0, 2, w1); dtostrf(st.wLast, 0, 2, w2);
    snprintf(b1, sizeof(b1), "%s → %s кг", w1, w2);
    _ru_dec(b1);
    _xml_kv(out, F("Вес: начало → конец"), b1);
    dtostrf(st.wLast - st.wFirst, 0, 2, w1);
    snprintf(b1, sizeof(b1), "%s%s кг", (st.wLast >= st.wFirst ? "+" : ""), w1);
    _ru_dec(b1);
    _xml_kv(out, F("Изменение за период"), b1);
    dtostrf(st.wMin, 0, 2, w1); dtostrf(st.wMax, 0, 2, w2);
    snprintf(b1, sizeof(b1), "%s / %s кг", w1, w2);
    _ru_dec(b1);
    _xml_kv(out, F("Вес мин / макс"), b1);
    out.print(F("<Row/>"));
    dtostrf(st.tMin, 0, 1, w1); dtostrf(st.tMax, 0, 1, w2);
    snprintf(b1, sizeof(b1), "%s / %s °C", w1, w2);
    _ru_dec(b1);
    _xml_kv(out, F("Температура мин / макс"), b1);
    out.print(F("<Row/>"));
    int p1 = _xls_bat_pct(st.bFirst), p2 = _xls_bat_pct(st.bLast);
    dtostrf(st.bFirst, 0, 2, w1); dtostrf(st.bLast, 0, 2, w2);
    snprintf(b1, sizeof(b1), "%s В (%d%%) → %s В (%d%%)", w1, p1, w2, p2);
    _ru_dec(b1);
    _xml_kv(out, F("Батарея: начало → конец"), b1);
    // Расход считаем в процентах: кривая LiPo нелинейна, вольты тут врут
    uint16_t spanDays = st.days > 1 ? (st.days - 1) : 1;
    float perDay = (float)(p1 - p2) / (float)spanDays;
    if (perDay > 0.01f) {
      dtostrf(perDay, 0, 2, w1);
      snprintf(b1, sizeof(b1), "%d%% за %u дн = %s%% в сутки", p1 - p2, (unsigned)spanDays, w1);
      _ru_dec(b1);
      _xml_kv(out, F("Расход заряда"), b1);
      snprintf(b1, sizeof(b1), "≈ %d дней", (int)(p2 / perDay));
      _xml_kv(out, F("Осталось при таком темпе"), b1);
      snprintf(b1, sizeof(b1), "≈ %d дней", (int)(100.0f / perDay));
      _xml_kv(out, F("Полный цикл от 100%"), b1);
    } else {
      _xml_kv(out, F("Расход заряда"), "данных мало или батарея заряжалась");
    }
  }
  out.print(F("</Table></Worksheet>\n"));

  // ─── Лист «Замеры» ───
  out.print(F("<Worksheet ss:Name=\"Замеры\"><Table>"
              "<Column ss:Width=\"120\"/><Column ss:Width=\"60\"/><Column ss:Width=\"80\"/>"
              "<Column ss:Width=\"60\"/><Column ss:Width=\"70\"/><Column ss:Width=\"60\"/>"
              "<Row ss:Height=\"30\">"
              "<Cell ss:StyleID=\"hdr\"><Data ss:Type=\"String\">Дата и время</Data></Cell>"
              "<Cell ss:StyleID=\"hdr\"><Data ss:Type=\"String\">Вес, кг</Data></Cell>"
              "<Cell ss:StyleID=\"hdr\"><Data ss:Type=\"String\">Изм., кг</Data></Cell>"
              "<Cell ss:StyleID=\"hdr\"><Data ss:Type=\"String\">Темп., °C</Data></Cell>"
              "<Cell ss:StyleID=\"hdr\"><Data ss:Type=\"String\">Батарея, В</Data></Cell>"
              "<Cell ss:StyleID=\"hdr\"><Data ss:Type=\"String\">Заряд, %</Data></Cell></Row>"));
  _RowsCtx rc; rc.out = &out; rc.prevW = 0; rc.hasPrev = false;
  size_t n = _stream_filter_range(from, to, _emit_xml_row, &rc);
  out.print(F("</Table><WorksheetOptions xmlns=\"urn:schemas-microsoft-com:office:excel\">"
              "<FreezePanes/><FrozenNoSplit/><SplitHorizontal>1</SplitHorizontal>"
              "<TopRowBottomPane>1</TopRowBottomPane><ActivePane>2</ActivePane>"
              "</WorksheetOptions></Worksheet>\n"));

  // ─── Лист «По дням» ───
  out.print(F("<Worksheet ss:Name=\"По дням\"><Table>"
              "<Column ss:Width=\"80\"/><Column ss:Width=\"60\"/><Column ss:Width=\"90\"/>"
              "<Column ss:Width=\"100\"/><Column ss:Width=\"70\"/><Column ss:Width=\"70\"/>"
              "<Column ss:Width=\"70\"/><Column ss:Width=\"70\"/><Column ss:Width=\"90\"/>"
              "<Row ss:Height=\"30\">"
              "<Cell ss:StyleID=\"hdr\"><Data ss:Type=\"String\">Дата</Data></Cell>"
              "<Cell ss:StyleID=\"hdr\"><Data ss:Type=\"String\">Замеров</Data></Cell>"
              "<Cell ss:StyleID=\"hdr\"><Data ss:Type=\"String\">Вес на конец</Data></Cell>"
              "<Cell ss:StyleID=\"hdr\"><Data ss:Type=\"String\">Привес за сутки</Data></Cell>"
              "<Cell ss:StyleID=\"hdr\"><Data ss:Type=\"String\">Вес мин</Data></Cell>"
              "<Cell ss:StyleID=\"hdr\"><Data ss:Type=\"String\">Вес макс</Data></Cell>"
              "<Cell ss:StyleID=\"hdr\"><Data ss:Type=\"String\">Темп. мин</Data></Cell>"
              "<Cell ss:StyleID=\"hdr\"><Data ss:Type=\"String\">Темп. макс</Data></Cell>"
              "<Cell ss:StyleID=\"hdr\"><Data ss:Type=\"String\">Батарея на конец</Data></Cell></Row>"));
  _DayCtx dc;
  memset(&dc, 0, sizeof(dc));
  dc.out = &out;
  _stream_filter_range(from, to, _emit_day_row, &dc);
  _day_flush(&dc);
  out.print(F("</Table></Worksheet>\n</Workbook>"));
  return n;
}
