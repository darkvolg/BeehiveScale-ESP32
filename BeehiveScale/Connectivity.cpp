#include "Connectivity.h"
#include "Memory.h"
#include "RTC_Module.h"
#include "Battery.h"
#include <ArduinoJson.h>
#include <time.h>
#include <RTClib.h>
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <ESP8266mDNS.h>
#else
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ESPmDNS.h>
#include <esp_task_wdt.h>
#include <esp_wifi.h>
#include <esp_phy_init.h>
#endif

#define HTTP_TIMEOUT_MS  5000

static WifiStatus _wifiStatus = WIFI_DISCONNECTED;

#if defined(ESP32)
// v5.0.26: _wifiRetryCount в RTC memory чтобы переживать ESP.restart().
// Иначе при каждом SW_CPU_RESET счётчик сбрасывался и могла быть бесконечная петля.
RTC_DATA_ATTR static uint8_t _wifiRetryCount = 0;
#endif

// Инициализация WiFi в режиме STA или AP.
// Если выбран STA, но подключение не удалось за WIFI_TIMEOUT_MS — авто-fallback в AP,
// чтобы пользователь всегда мог достучаться до Web UI и перенастроить.
bool wifi_init() {
  wifi_settings_init();
  uint8_t mode = get_wifi_mode();

  if (mode == 1) {
    if (wifi_connect()) return true;
    Serial.println(F("[WiFi] STA failed — fallback to AP mode"));
  }

  // AP режим — точка доступа без роутера
  Serial.println(F("[WiFi] Starting AP mode..."));
  WiFi.mode(WIFI_AP);
  char apPassBuf[24];
  get_ap_pass(apPassBuf, sizeof(apPassBuf));
  WiFi.softAP(AP_SSID, apPassBuf, AP_CHANNEL, false, AP_MAX_CLIENTS);

  delay(1000);

  IPAddress IP = WiFi.softAPIP();
  Serial.print(F("[WiFi] AP IP address: "));
  Serial.println(IP);

  if (IP == IPAddress(0,0,0,0)) {
    Serial.println(F("[WiFi] AP failed to start!"));
    _wifiStatus = WIFI_DISCONNECTED;
    return false;
  }

  _wifiStatus = WIFI_CONNECTED;
  Serial.println(F("[WiFi] AP mode ready, IP: 192.168.4.1"));

  if (MDNS.begin(MDNS_HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.println(F("[mDNS] " MDNS_HOSTNAME ".local ready"));
  }
  return true;
}

bool wifi_connect() {
  // SSID и пароль: из EEPROM если сохранены, иначе из хардкода
  char ssidBuf[33], passBuf[33];
  get_wifi_ssid(ssidBuf, sizeof(ssidBuf));
  get_wifi_sta_pass(passBuf, sizeof(passBuf));
  const char *ssid = (ssidBuf[0] != '\0') ? ssidBuf : WIFI_SSID;
  const char *pass = (passBuf[0] != '\0') ? passBuf : WIFI_PASSWORD;

  Serial.print(F("[WiFi] Connecting to: "));
  Serial.println(ssid);

  // v5.0.26: правильная последовательность init WiFi (по анализу 2 агентов + ESP32 forum).
  // КРИТИЧНО: WiFi.mode(WIFI_STA) ДО WiFi.disconnect(true,true) — иначе disconnect трогает
  // uninitialized station_handle (баг Arduino-ESP32 3.0.x, см. issues #9658, #9329, #9913).
  // esp_wifi_stop() в начале — на случай если предыдущий boot оставил half-state в WiFi-драйвере.
#if defined(ESP32)
  // Cleanup any half-state from prior boot (ignore error если WiFi ещё не был init)
  esp_wifi_stop();
  delay(50);

  WiFi.persistent(false);              // не писать креды в NVS лишний раз
  WiFi.mode(WIFI_STA);                 // СНАЧАЛА mode — инициализирует netif + event loop
  delay(100);
  WiFi.disconnect(true, true);         // ТЕПЕРЬ безопасно стирать NVS-кэш WiFi
  delay(200);

  // Если это уже не первая попытка подряд — стереть PHY/RF calibration cache.
  // Помогает при corruption калибровки после нескольких ESP.restart() циклов.
  if (_wifiRetryCount >= 2) {
    Serial.println(F("[WiFi] Erasing PHY calibration data (retry recovery)..."));
    esp_phy_erase_cal_data_in_nvs();
    delay(100);
  }
#else
  WiFi.mode(WIFI_STA);
#endif
  WiFi.begin(ssid, pass);

  _wifiStatus = WIFI_CONNECTING;
  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > WIFI_TIMEOUT_MS) {
      _wifiStatus = WIFI_DISCONNECTED;
      wl_status_t s = WiFi.status();
      Serial.print(F("[WiFi] Timeout! Status="));
      switch (s) {
        case WL_NO_SSID_AVAIL: Serial.println(F("NO_SSID_AVAIL — сеть не найдена (проверь имя SSID, 2.4GHz)")); break;
        case WL_CONNECT_FAILED: Serial.println(F("CONNECT_FAILED — неверный пароль или WPA3-only")); break;
        case WL_CONNECTION_LOST: Serial.println(F("CONNECTION_LOST")); break;
        case WL_DISCONNECTED: Serial.println(F("DISCONNECTED — попытка не удалась")); break;
        case WL_IDLE_STATUS: Serial.println(F("IDLE — слабый сигнал или роутер не отвечает")); break;
        default: Serial.println(s);
      }
#if defined(ESP32)
      // v5.0.27: ESP.restart() retry loop УБРАН — он накапливал RTC slow memory corruption
      // между SW_CPU_RESET циклами. Теперь при WiFi timeout → возврат false → fallback на AP
      // (в wifi_init), без принудительного перезапуска ESP.
      _wifiRetryCount = 0;
#endif
      return false;
    }
#if defined(ESP32)
    esp_task_wdt_reset();
#elif defined(ESP8266)
    ESP.wdtFeed();
#endif
    yield();
    delay(10);
  }
  _wifiStatus = WIFI_CONNECTED;
#if defined(ESP32)
  _wifiRetryCount = 0;  // v5.0.26: сброс счётчика на успехе
#endif
  Serial.print(F("[WiFi] Connected, IP: "));
  Serial.println(WiFi.localIP());

  // mDNS — доступ по http://vesy.local без знания IP (v5.0.67)
  if (MDNS.begin(MDNS_HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.println(F("[mDNS] " MDNS_HOSTNAME ".local ready"));
  }
  return true;
}

WifiStatus wifi_status() {
  return _wifiStatus;
}

void wifi_ensure_connected() {
  // Если EEPROM=AP ИЛИ сейчас фактически работаем в AP (fallback после STA-таймаута)
  // — не пытаемся реконнектиться, иначе будем рвать сеть каждые 10 сек.
  if (get_wifi_mode() == 0 || WiFi.getMode() == WIFI_AP) {
    _wifiStatus = (WiFi.softAPIP() == IPAddress(0,0,0,0)) ? WIFI_DISCONNECTED : WIFI_CONNECTED;
    return;
  }

  static unsigned long lastReconnectAttempt = 0;
  static unsigned long connectionFailedAt = 0;
  static bool hasPenalty = false;
  const unsigned long RECONNECT_DEBOUNCE_MS = 10000UL;
  const unsigned long PENALTY_MS = 300000UL; // 5 минут отдыха при неудаче

  if (WiFi.status() != WL_CONNECTED) {
    _wifiStatus = WIFI_DISCONNECTED;
    unsigned long now = millis();

    // Если была неудача, ждем PENALTY_MS перед следующей попыткой
    if (hasPenalty && now - connectionFailedAt < PENALTY_MS) {
      return;
    }

    if (now - lastReconnectAttempt < RECONNECT_DEBOUNCE_MS) {
      return;
    }

    Serial.println(F("[WiFi] Lost connection, reconnecting..."));
    WiFi.reconnect();
    lastReconnectAttempt = now;
    unsigned long start = now;

    // Сокращенный таймаут для переподключения (7 сек)
    while (WiFi.status() != WL_CONNECTED && millis() - start < 7000UL) {
#if defined(ESP32)
      esp_task_wdt_reset();
#elif defined(ESP8266)
      ESP.wdtFeed();
#endif
      yield();
      delay(10);
    }

    if (WiFi.status() == WL_CONNECTED) {
      _wifiStatus = WIFI_CONNECTED;
      hasPenalty = false;
      Serial.println(F("[WiFi] Reconnected."));
    } else {
      connectionFailedAt = millis();
      hasPenalty = true;
      Serial.println(F("[WiFi] Reconnect failed, cooling down..."));
    }
  } else if (hasPenalty) {
    // WiFi восстановился самостоятельно — сбросить penalty
    hasPenalty = false;
  }
}

static bool _wifi_active() {
  // В AP-режиме нет интернета — внешние сервисы (TG, ThingSpeak, NTP) недоступны
  if (get_wifi_mode() == 0) return false;
  return WiFi.status() == WL_CONNECTED;
}

// Cloudflare Worker proxy — обход блокировки api.telegram.org провайдером (Beeline RU).
// Worker проксит as-is, Token и chat_id остаются в EEPROM ESP32.
// См. cloudflare-worker/ESP32_INTEGRATION.md
static const char* TG_HOST = "beehive-relay.darkvolg.workers.dev";

#include <LittleFS.h>

#define QUEUE_FILE "/queue.bin"
#define MAX_QUEUE_ITEMS 50

static bool _ensureFS() {
  static bool _fsReady = false;
  if (_fsReady) return true;
#if defined(ESP32)
  _fsReady = LittleFS.begin(true);  // auto-format on mount fail (свежий чип)
#else
  _fsReady = LittleFS.begin();
#endif
  return _fsReady;
}

void queue_add(float weight, float temp, float hum, float rtcTemp, const String& dt) {
  // Если ThingSpeak не настроен (дефолтный ключ) — не копим очередь.
  // Иначе данные тысячами накапливаются в /queue.bin без шанса быть отправленными.
  if (strncmp(TS_API_KEY, "YOUR_", 5) == 0) return;
  if (!_ensureFS()) return;
  File f = LittleFS.open(QUEUE_FILE, "a");
  if (!f) return;

  if (f.size() >= MAX_QUEUE_ITEMS * sizeof(UnsentData)) {
    f.close();
    Serial.println(F("[Queue] Full, skipping"));
    return;
  }

  UnsentData data;
  data.weight = weight;
  data.temp = temp;
  data.hum = hum;
  data.rtcTemp = rtcTemp;
  memset(data.datetime, 0, sizeof(data.datetime));
  strncpy(data.datetime, dt.c_str(), sizeof(data.datetime) - 1);

  f.write((uint8_t*)&data, sizeof(UnsentData));
  f.close();
  Serial.println(F("[Queue] Data saved offline"));
}

void queue_process() {
  if (!_wifi_active()) return;
  if (!_ensureFS()) return;
  // Если ThingSpeak не настроен — удаляем накопленную очередь, не пытаемся слать.
  if (strncmp(TS_API_KEY, "YOUR_", 5) == 0) {
    if (LittleFS.exists(QUEUE_FILE)) {
      LittleFS.remove(QUEUE_FILE);
      Serial.println(F("[Queue] Cleared (ThingSpeak disabled)"));
    }
    if (LittleFS.exists("/queue_tmp.bin")) LittleFS.remove("/queue_tmp.bin");
    return;
  }
  // Восстановление orphaned tmp-файла после неудачного rename
  if (!LittleFS.exists(QUEUE_FILE) && LittleFS.exists("/queue_tmp.bin")) {
    LittleFS.rename("/queue_tmp.bin", QUEUE_FILE);
    Serial.println(F("[Queue] Recovered orphaned tmp file"));
  }
  if (!LittleFS.exists(QUEUE_FILE)) return;

  File f = LittleFS.open(QUEUE_FILE, "r");
  if (!f) return;

  size_t count = f.size() / sizeof(UnsentData);
  if (count == 0) { f.close(); LittleFS.remove(QUEUE_FILE); return; }

  Serial.print(F("[Queue] Processing items: ")); Serial.println(count);

  // Читаем и отправляем по одному элементу — без выделения heap под весь массив
  size_t sent = 0;
  size_t maxSend = (count > 5) ? 5 : count;
  bool sendFailed = false;
  UnsentData failedItem = {};
  bool hasFailedItem = false;

  for (size_t i = 0; i < maxSend; i++) {
    UnsentData item;
    if (f.read((uint8_t*)&item, sizeof(UnsentData)) != sizeof(UnsentData)) break;
    Serial.print(F("[Queue] Sending item ")); Serial.println(i+1);
    if (!ts_send(item.weight, item.temp, item.hum, item.rtcTemp)) {
      Serial.println(F("[Queue] Send failed, saving remaining"));
      failedItem = item;
      hasFailedItem = true;
      sendFailed = true;
      break;
    }
    sent++;
    delay(500);
#if defined(ESP8266)
    ESP.wdtFeed();
#endif
    yield();
  }

  // Снимок позиции в f ДО закрытия: всё что осталось после сейчас-прочитанного —
  // надо перенести в tmp. Исправление пункта 18: раньше код читал f.available()
  // ПОСЛЕ f.close(), что давало false. Теперь сначала копируем, потом закрываем.
  bool hasRemaining = f.available();
  if (sendFailed || (maxSend < count && hasRemaining)) {
    File tmp = LittleFS.open("/queue_tmp.bin", "w");
    if (tmp) {
      if (hasFailedItem) {
        tmp.write((uint8_t*)&failedItem, sizeof(UnsentData));
      }
      while (f.available()) {
        UnsentData rem;
        if (f.read((uint8_t*)&rem, sizeof(UnsentData)) == sizeof(UnsentData)) {
          tmp.write((uint8_t*)&rem, sizeof(UnsentData));
        } else {
          break;
        }
        yield();
      }
      tmp.close();
    } else {
      Serial.println(F("[Queue] Cannot open tmp file"));
    }
    f.close();
    LittleFS.remove(QUEUE_FILE);
    if (LittleFS.exists("/queue_tmp.bin")) {
      if (!LittleFS.rename("/queue_tmp.bin", QUEUE_FILE)) {
        Serial.println(F("[Queue] Rename failed, keeping tmp"));
      }
    }
    Serial.print(F("[Queue] Sent ")); Serial.print(sent);
    Serial.print(F(" of ")); Serial.println(count);
    return;
  }

  f.close();
  LittleFS.remove(QUEUE_FILE);
  Serial.println(F("[Queue] Done"));
}

static bool _tg_post(const char* message) {
  if (!_wifi_active()) return false;

  // Приоритет: EEPROM-настройки → хардкод из Connectivity.h
  char tgToken[50] = {0};
  char tgChatId[16] = {0};
  get_tg_token(tgToken, sizeof(tgToken));
  get_tg_chatid(tgChatId, sizeof(tgChatId));

  const char* useToken  = (tgToken[0]  != '\0') ? tgToken  : TG_BOT_TOKEN;
  const char* useChatId = (tgChatId[0] != '\0') ? tgChatId : TG_CHAT_ID;

  if (strncmp(useToken, "YOUR_", 5) == 0) return false;

  // Validate token contains only safe URL characters
  for (const char *p = useToken; *p; p++) {
    char c = *p;
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ':' || c == '_' || c == '-')) {
      Serial.println(F("[TG] Invalid token characters"));
      return false;
    }
  }

#if defined(ESP32)
  // На ESP32 TLS handshake требует ~30кБ свободной кучи. Логируем перед запросом
  // чтобы при ошибках было видно "за что зацепились".
  Serial.print(F("[TG] Free heap before TLS: "));
  Serial.println(ESP.getFreeHeap());
#endif

#if defined(ESP8266)
  BearSSL::WiFiClientSecure client;
#else
  WiFiClientSecure client;
#endif
  client.setInsecure();

  HTTPClient http;
  // Telegram TLS handshake может занимать до 8 секунд при первом запросе
  http.setTimeout(15000);

  char url[160];
  snprintf(url, sizeof(url), "https://%s/bot%s/sendMessage", TG_HOST, useToken);

  // yield() перед/после BearSSL — handshake на ESP8266 занимает 2-5 сек,
  // даёт шанс WDT feed и TCP стеку обработать backlog.
  yield();
#if defined(ESP8266)
  ESP.wdtFeed();
  if (!http.begin(client, url)) return false;
#else
  if (!http.begin(client, url)) {
    Serial.println(F("[TG] http.begin() failed"));
    return false;
  }
#endif
  yield();
  http.addHeader("Content-Type", "application/json");
  // Cloudflare Worker проверяет этот заголовок — без него отдаст 403.
  http.addHeader("X-Beehive-Secret", BEEHIVE_RELAY_SECRET);

  // Размер: text может быть до ~1500 байт (приветственное сообщение + кириллица в UTF-8 = по 2 байта/символ),
  // плюс JSON-обвязка ~80 байт. Маленький буфер обрезал JSON и терялся parse_mode → HTML не парсился.
  StaticJsonDocument<2048> doc;
  doc["chat_id"] = useChatId;
  doc["text"] = message;
  doc["parse_mode"] = "HTML";
  char body[2048];
  size_t bodyLen = serializeJson(doc, body, sizeof(body));
  (void)bodyLen;

#if defined(ESP8266)
  ESP.wdtFeed();
#endif
  int code = http.POST(body);
  yield();
  http.end();

  if (code == 200) {
    Serial.println(F("[TG] Message sent OK"));
    return true;
  }
  Serial.print(F("[TG] Error code: ")); Serial.print(code);
  // Расшифровка типичных HTTPClient ошибок
  switch (code) {
    case -1:  Serial.println(F(" (CONNECTION_REFUSED — не достучались до api.telegram.org)")); break;
    case -2:  Serial.println(F(" (SEND_HEADER_FAILED)")); break;
    case -3:  Serial.println(F(" (SEND_PAYLOAD_FAILED)")); break;
    case -4:  Serial.println(F(" (NOT_CONNECTED)")); break;
    case -5:  Serial.println(F(" (CONNECTION_LOST — TLS handshake провалился, обычно мало RAM)")); break;
    case -11: Serial.println(F(" (READ_TIMEOUT)")); break;
    case 401: Serial.println(F(" (Unauthorized — неверный токен)")); break;
    case 400: Serial.println(F(" (Bad Request — неверный chat_id или формат)")); break;
    default:  Serial.println();
  }
  return false;
}

bool tg_send_message(const String &text) {
  return _tg_post(text.c_str());
}

bool tg_send_alert(float weight, float tempC, const String &datetime, float refWeight) {
  char msg[384];
  char tempStr[16];
  if (tempC > -90) {
    snprintf(tempStr, sizeof(tempStr), "%.1f C", tempC);
  } else {
    snprintf(tempStr, sizeof(tempStr), "n/d");
  }
  float delta = weight - refWeight;
  const char* reason;
  const char* emoji;
  if (delta >= 0) {
    reason = "резкий прирост веса";
    emoji = "📈";
  } else {
    reason = "резкая убыль веса (роение/кража)";
    emoji = "📉";
  }
  snprintf(msg, sizeof(msg),
    "🚨 <b>ТРЕВОГА: улей</b>\n"
    "Причина: %s %s\n"
    "Изменение: <b>%s%.1f кг</b>\n"
    "Время: %s\n"
    "Вес: <b>%.1f кг</b> (было %.1f)\n"
    "Температура: %s",
    emoji, reason,
    (delta >= 0 ? "+" : ""), delta,
    datetime.c_str(), weight, refWeight, tempStr);
  return _tg_post(msg);
}

bool tg_send_report(float weight, float tempC, float humidity, const String &datetime,
                    float prevWeight, uint32_t prevWeightDate,
                    float lastReportWeight, bool hasLastReport,
                    bool hasDailyGain, float prevEveningWeight,
                    uint16_t eveningSlotMin) {
  (void)humidity;  // нет датчика влажности — не выводим в отчёт
  float deltaRef    = weight - prevWeight;          // от опорного эталона (ручной)
  float deltaPeriod = weight - lastReportWeight;    // от прошлого отчёта (за сутки)
  char msg[700];
  int pos = 0;
  pos += snprintf(msg + pos, sizeof(msg) - pos,
    "🐝 <b>Отчёт: улей</b>\n"
    "Время: %s\n"
    "Вес: <b>%.1f кг</b>\n",
    datetime.c_str(), weight);

  if (hasLastReport) {
    pos += snprintf(msg + pos, sizeof(msg) - pos,
      "📈 <b>С прошлого замера:</b> %s%.1f кг (было %.1f)\n",
      (deltaPeriod >= 0 ? "+" : ""), deltaPeriod, lastReportWeight);
  }
  // v5.0.62: привес за сутки (только вечерний замер) — сравнение с вчерашним вечером.
  if (hasDailyGain) {
    float dailyGain = weight - prevEveningWeight;
    pos += snprintf(msg + pos, sizeof(msg) - pos,
      "🍯 <b>Привес за сутки:</b> %s%.1f кг (вчера %02u:%02u → %.1f)\n",
      (dailyGain >= 0 ? "+" : ""), dailyGain,
      eveningSlotMin / 60, eveningSlotMin % 60, prevEveningWeight);
  }
  if (prevWeight > 0.05f) {
    // Форматируем дату фиксации
    char dateStr[40] = "";
    if (prevWeightDate > 0) {
      DateTime dt(prevWeightDate);
      // Сколько дней прошло — для контекста "уже X дней"
      TimeStamp ts = rtc_now();
      int daysAgo = 0;
      if (ts.valid) {
        DateTime cur(ts.year, ts.month, ts.day, ts.hour, ts.minute, ts.second);
        if (cur.unixtime() > prevWeightDate) {
          daysAgo = (int)((cur.unixtime() - prevWeightDate) / 86400UL);
        }
      }
      if (daysAgo > 0) {
        snprintf(dateStr, sizeof(dateStr), " от %02u.%02u.%04u, %d дн назад",
                 dt.day(), dt.month(), dt.year(), daysAgo);
      } else {
        snprintf(dateStr, sizeof(dateStr), " от %02u.%02u.%04u",
                 dt.day(), dt.month(), dt.year());
      }
    }
    pos += snprintf(msg + pos, sizeof(msg) - pos,
      "🎯 <b>От зафикс. точки:</b> %s%.1f кг (зафикс. %.1f%s)\n",
      (deltaRef >= 0 ? "+" : ""), deltaRef, prevWeight, dateStr);
  }
  if (tempC > -90) {
    const char* tIcon = (tempC < 8.0f) ? "❄️" : (tempC > 35.0f) ? "🔥" : "🌡️";
    pos += snprintf(msg + pos, sizeof(msg) - pos, "%s Температура: %.1f °C\n", tIcon, tempC);
  }

  // v5.0.51: батарея + WiFi сигнал в отчёт
  float batV = bat_voltage();
  int batPct = bat_percent();
  if (batV > 0.5f && batV < 5.5f) {
    const char* batIcon = (batPct < 15) ? "🪫" : "🔋";
    pos += snprintf(msg + pos, sizeof(msg) - pos,
      "%s Батарея: %.2f В (%d%%)\n", batIcon, batV, batPct);
  }
  if (WiFi.status() == WL_CONNECTED) {
    long rssi = WiFi.RSSI();
    const char* sigIcon = (rssi > -60) ? "📶" : (rssi > -75) ? "📶" : "📵";
    pos += snprintf(msg + pos, sizeof(msg) - pos,
      "%s WiFi: %ld dBm\n", sigIcon, rssi);
  }

  return _tg_post(msg);
}

// v5.0.58: отправить задержанный TG отчёт с пометкой "Поздний"
bool tg_send_pending(uint32_t origUnix, float weight, float tempC,
                     float batV, uint8_t batPct, int8_t rssi) {
  DateTime dt(origUnix);
  char msg[400];
  int pos = 0;
  pos += snprintf(msg + pos, sizeof(msg) - pos,
    "🕐 <b>Поздний отчёт (retry)</b>\n"
    "Время замера: %02u.%02u.%04u %02u:%02u:%02u\n"
    "Вес: <b>%.1f кг</b>\n",
    dt.day(), dt.month(), dt.year(), dt.hour(), dt.minute(), dt.second(),
    weight);
  if (tempC > -90) {
    const char* tIcon = (tempC < 8.0f) ? "❄️" : (tempC > 35.0f) ? "🔥" : "🌡️";
    pos += snprintf(msg + pos, sizeof(msg) - pos,
      "%s Температура: %.1f °C\n", tIcon, tempC);
  }
  if (batV > 0.5f && batV < 5.5f) {
    const char* batIcon = (batPct < 15) ? "🪫" : "🔋";
    pos += snprintf(msg + pos, sizeof(msg) - pos,
      "%s Батарея: %.2f В (%u%%)\n", batIcon, batV, batPct);
  }
  if (rssi != 0) {
    pos += snprintf(msg + pos, sizeof(msg) - pos,
      "📶 WiFi: %d dBm\n", (int)rssi);
  }
  pos += snprintf(msg + pos, sizeof(msg) - pos,
    "\n<i>Сообщение задержано — TG был недоступен при попытке отправки.</i>");
  return _tg_post(msg);
}

bool ts_send(float weight, float tempC, float humidity, float rtcTempC) {
  if (!_wifi_active()) return false;
  if (strncmp(TS_API_KEY, "YOUR_", 5) == 0) return false;

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);

  char url[180];
  snprintf(url, sizeof(url),
    "https://api.thingspeak.com/update?api_key=%s&field1=%.2f&field2=%.1f&field3=%.1f&field4=%.2f",
    TS_API_KEY, weight, tempC, humidity, rtcTempC);

  yield();
#if defined(ESP8266)
  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  ESP.wdtFeed();
  if (!http.begin(client, url)) return false;
#else
  WiFiClientSecure client;
  client.setInsecure();
  http.begin(client, url);
#endif
  yield();
  int code = http.GET();
  yield();
  http.end();

  if (code == 200) {
    Serial.println(F("[TS] OK"));
    return true;
  }
  Serial.print(F("[TS] Error code: ")); Serial.println(code);
  return false;
}

// ─── NTP синхронизация ────────────────────────────────────────────────────
static unsigned long _lastNtpSync = 0;
static bool _ntpInitialized = false;

extern bool rtc_set(uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi, uint8_t s);

bool ntp_sync_time() {
  if (!_wifi_active()) {
    Serial.println(F("[NTP] Error: no WiFi"));
    return false;
  }

  Serial.println(F("[NTP] Sync time..."));
  Serial.print(F("[NTP] Server: "));
  Serial.println(NTP_SERVER_1);

#if defined(ESP32)
  configTime(NTP_TIMEZONE * 3600, 0, NTP_SERVER_1, NTP_SERVER_2);

  struct tm timeinfo;
  int retry = 0;
  Serial.print(F("[NTP] Waiting"));

  while (!getLocalTime(&timeinfo) && retry < 15) {
    delay(1000);
    retry++;
    Serial.print(F("."));
    esp_task_wdt_reset();
    yield();
  }
  Serial.println();

  if (retry >= 15) {
    Serial.println(F("[NTP] Sync failed!"));
    return false;
  }

  char timeStr[64];
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
  Serial.print(F("[NTP] Got time: "));
  Serial.println(timeStr);

  if (rtc_set(
    timeinfo.tm_year + 1900,
    timeinfo.tm_mon + 1,
    timeinfo.tm_mday,
    timeinfo.tm_hour,
    timeinfo.tm_min,
    timeinfo.tm_sec
  )) {
    Serial.println(F("[NTP] Time set to RTC!"));
    _lastNtpSync = millis();
    _ntpInitialized = true;
    return true;
  } else {
    Serial.println(F("[NTP] RTC error"));
    return false;
  }

#elif defined(ESP8266)
  configTime(NTP_TIMEZONE * 3600, 0, NTP_SERVER_1, NTP_SERVER_2);

  int retry = 0;
  Serial.print(F("[NTP] Waiting"));
  time_t now = 0;
  while (now < 100000 && retry < 15) {
    delay(1000);
    ESP.wdtFeed();
    yield();
    now = time(nullptr);
    retry++;
    Serial.print(F("."));
  }
  Serial.println();

  if (now < 100000) {
    Serial.println(F("[NTP] Sync failed!"));
    return false;
  }

  struct tm *timeinfo = localtime(&now);
  char timeStr[64];
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
  Serial.print(F("[NTP] Got time: "));
  Serial.println(timeStr);

  if (rtc_set(
    timeinfo->tm_year + 1900,
    timeinfo->tm_mon + 1,
    timeinfo->tm_mday,
    timeinfo->tm_hour,
    timeinfo->tm_min,
    timeinfo->tm_sec
  )) {
    Serial.println(F("[NTP] Time set to RTC!"));
    _lastNtpSync = millis();
    _ntpInitialized = true;
    return true;
  } else {
    Serial.println(F("[NTP] RTC error"));
    return false;
  }
#endif
}

void ntp_loop() {
  if (!_ntpInitialized) {
    _ntpInitialized = true;
    if (!ntp_sync_time()) {
      // При неудаче — повтор через 1 мин вместо NTP_SYNC_INTERVAL (1 час)
      _lastNtpSync = millis() - NTP_SYNC_INTERVAL + 60000UL;
    } else {
      _lastNtpSync = millis();
    }
    return;
  }

  if (millis() - _lastNtpSync >= NTP_SYNC_INTERVAL) {
    Serial.println(F("[NTP] Periodic sync..."));
    if (ntp_sync_time()) {
      Serial.println(F("[NTP] Sync OK"));
      _lastNtpSync = millis();
    } else {
      Serial.println(F("[NTP] Sync failed"));
      // Повтор через 2 мин вместо полного интервала
      _lastNtpSync = millis() - NTP_SYNC_INTERVAL + 120000UL;
    }
  }
}
