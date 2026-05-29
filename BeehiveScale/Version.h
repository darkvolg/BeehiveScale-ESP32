#ifndef BEEHIVE_VERSION_H
#define BEEHIVE_VERSION_H

#define FW_VERSION_MAJOR 5
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 64
#define FW_VERSION_SUFFIX ""  // 5.0.64 — icon температуры в TG (❄️/🌡️/🔥 по порогам <8/норма/>35°C). FIX: software restart (web 'Перезагрузить'/OTA) больше НЕ шлёт boot-TG. Раньше перезапуск для проверки рядом со слотом (21:01 при слоте 21:00) проскакивал ±10-мин фильтр и слал дубль. Теперь esp_reset_reason()==ESP_RST_SW → TG подавляется.

#define _FW_STR_HELPER(x) #x
#define _FW_STR(x) _FW_STR_HELPER(x)

#define FW_VERSION  _FW_STR(FW_VERSION_MAJOR) "." _FW_STR(FW_VERSION_MINOR) "." _FW_STR(FW_VERSION_PATCH) FW_VERSION_SUFFIX
#define FW_NAME     "BeehiveScale"
#define FW_FULLNAME FW_NAME " v" FW_VERSION

#endif
