#ifndef BEEHIVE_TG_PENDING_H
#define BEEHIVE_TG_PENDING_H
// ─── v5.0.58: Отложенная отправка TG при fail ────────────────────────────
// Сохраняет один pending TG в LittleFS файл /tg_pending.bin.
// При next wake пробует отправить с пометкой "Поздний отчёт от HH:MM".
// Max 3 попытки → дропает (что-то глобально сломано, нет смысла копить).

#include <Arduino.h>

struct TgPendingData {
  uint32_t magic;       // 0xDEAFBEEF
  uint32_t unixtime;    // когда был замер
  float    weight;
  float    tempC;
  float    batV;
  uint8_t  batPct;
  int8_t   rssi;
  uint8_t  retryCount;
};

// Сохранить pending TG (overwrite если уже есть)
void tg_pending_save(uint32_t unixtime, float weight, float tempC,
                     float batV, uint8_t batPct, int8_t rssi);

// Загрузить pending TG. Возвращает true если есть валидная запись.
bool tg_pending_load(TgPendingData &out);

// Удалить pending file
void tg_pending_clear();

// Increment retry counter (после неудачной попытки). Возвращает true если
// retryCount достиг 3 — пора дропать.
bool tg_pending_inc_retry();

#endif
