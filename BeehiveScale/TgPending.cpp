#include "TgPending.h"
#include <LittleFS.h>

#define TG_PENDING_FILE   "/tg_pending.bin"
#define TG_PENDING_MAGIC  0xDEAFBEEF
#define TG_MAX_RETRIES    3

void tg_pending_save(uint32_t unixtime, float weight, float tempC,
                     float batV, uint8_t batPct, int8_t rssi) {
  if (unixtime < 1546300800UL) return;  // <2019-01-01 — мусор
  TgPendingData d;
  d.magic      = TG_PENDING_MAGIC;
  d.unixtime   = unixtime;
  d.weight     = weight;
  d.tempC      = tempC;
  d.batV       = batV;
  d.batPct     = batPct;
  d.rssi       = rssi;
  d.retryCount = 0;
  if (!LittleFS.begin()) return;
  File f = LittleFS.open(TG_PENDING_FILE, "w");
  if (!f) return;
  f.write((uint8_t*)&d, sizeof(d));
  f.close();
  Serial.print(F("[TgPending] Saved for retry at unix="));
  Serial.println(unixtime);
}

bool tg_pending_load(TgPendingData &out) {
  if (!LittleFS.begin()) return false;
  if (!LittleFS.exists(TG_PENDING_FILE)) return false;
  File f = LittleFS.open(TG_PENDING_FILE, "r");
  if (!f) return false;
  size_t read = f.read((uint8_t*)&out, sizeof(out));
  f.close();
  if (read != sizeof(out)) return false;
  if (out.magic != TG_PENDING_MAGIC) return false;
  return true;
}

void tg_pending_clear() {
  if (!LittleFS.begin()) return;
  if (LittleFS.exists(TG_PENDING_FILE)) {
    LittleFS.remove(TG_PENDING_FILE);
    Serial.println(F("[TgPending] Cleared"));
  }
}

bool tg_pending_inc_retry() {
  TgPendingData d;
  if (!tg_pending_load(d)) return true;  // нет записи, считать что done
  d.retryCount++;
  if (d.retryCount >= TG_MAX_RETRIES) {
    Serial.print(F("[TgPending] Max retries reached ("));
    Serial.print(d.retryCount);
    Serial.println(F("), dropping"));
    tg_pending_clear();
    return true;
  }
  // Сохранить обновлённый retryCount
  if (!LittleFS.begin()) return false;
  File f = LittleFS.open(TG_PENDING_FILE, "w");
  if (!f) return false;
  f.write((uint8_t*)&d, sizeof(d));
  f.close();
  Serial.print(F("[TgPending] Retry counter = "));
  Serial.println(d.retryCount);
  return false;
}
