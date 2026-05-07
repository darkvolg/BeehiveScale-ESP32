#include "WebServerModule.h"
#include "Version.h"
#include <stdint.h>   // INT32_MIN используется в _handleBackupRestore
#if defined(ESP8266)
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
using WebServerCompat = ESP8266WebServer;
#else
#include <WebServer.h>
#include <WiFi.h>
using WebServerCompat = WebServer;
#endif
#include <ArduinoJson.h>   // ArduinoJson v6 — установить через Library Manager
#include "Memory.h"
#include "Connectivity.h"  // для ntp_sync_time()
#include "Logger.h"
#ifdef USE_SD_CARD
#include <SPI.h>
#include <SD.h>
#include <LittleFS.h>
#elif defined(ESP8266)
#include <LittleFS.h>
#define LOG_FS LittleFS
#elif defined(ESP32)
#include <SPIFFS.h>
#define LOG_FS SPIFFS
#endif

static WebServerCompat _srv(WEB_SERVER_PORT);
static WebData    _wd;
static WebActions _wa;

// ─── CSRF-токен (32 hex-символа, ротируется при каждой перезагрузке) ─────
static char _csrfToken[33] = {0};
static void _csrf_init() {
  if (_csrfToken[0] != '\0') return;
#if defined(ESP8266) || defined(ESP32)
  uint32_t r1 = ESP.getCycleCount() ^ micros();
  uint32_t r2 = millis() ^ (uint32_t)random(0x7FFFFFFF);
#else
  uint32_t r1 = micros();
  uint32_t r2 = millis();
#endif
  snprintf(_csrfToken, sizeof(_csrfToken), "%08lx%08lx%08lx%08lx",
           (unsigned long)r1, (unsigned long)r2,
           (unsigned long)(r1 ^ 0xDEADBEEF), (unsigned long)(r2 ^ 0xCAFEBABE));
}

// ─── Basic Auth проверка (credentials из EEPROM) ─────────────────────────
static bool _auth() {
  char u[24], p[32];
  get_admin_user(u, sizeof(u));
  get_admin_pass(p, sizeof(p));
  if (!_srv.authenticate(u, p)) {
    _srv.requestAuthentication();
    return false;
  }
  return true;
}

// ─── CSRF проверка для state-changing запросов (POST) ────────────────────
// Токен ожидается в заголовке X-CSRF-Token. Клиент получает его через /api/data.
static bool _csrf_check() {
  if (_csrfToken[0] == '\0') _csrf_init();
  String hdr = _srv.header("X-CSRF-Token");
  if (hdr.length() != 32 || strcmp(hdr.c_str(), _csrfToken) != 0) {
    _srv.send(403, "application/json",
              "{\"ok\":false,\"msg\":\"CSRF token missing or invalid\"}");
    return false;
  }
  return true;
}

// ─── Rate limit для дорогих GET-эндпоинтов (1 сек) ───────────────────────
static bool _rate_limit(unsigned long &lastReq, unsigned long minMs) {
  unsigned long now = millis();
  if (lastReq != 0 && (now - lastReq) < minMs) {
    _srv.send(429, "application/json",
              "{\"ok\":false,\"msg\":\"Rate limited\"}");
    return false;
  }
  lastReq = now;
  return true;
}

// ─── Главная HTML страница (хранится во Flash) ────────────────────────────
static const char PAGE_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html lang="ru"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>🐝 BeehiveScale</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
:root{
  --bg:#0d0f0b;--panel:#141710;--border:#2e3829;
  --amber:#f5a623;--amber2:#ffd166;--green:#6fcf97;
  --red:#eb5757;--blue:#56ccf2;--text:#c8d4b8;--text2:#a5b894;--text3:#a5b894;
  --mono:'Courier New',monospace;
}
body{background:var(--bg);color:var(--text);font-family:var(--mono);font-size:15px;min-height:100vh}
a{color:var(--amber);text-decoration:none}

.refresh-bar{height:2px;background:var(--border);position:fixed;top:0;left:0;right:0;z-index:200}
.refresh-fill{height:100%;background:var(--amber);transition:width 0.5s linear}

.hdr{background:rgba(20,23,16,.97);border-bottom:1px solid var(--border);padding:8px 24px;position:sticky;top:0;z-index:99;min-height:52px;display:flex;align-items:center;justify-content:center}
.hdr-inner{display:flex;flex-direction:column;align-items:center;gap:0;position:relative;z-index:1}
.hdr-logo{font-size:18px;font-weight:700;letter-spacing:3px;color:var(--amber);text-align:center}
.hdr-sub{font-size:10px;color:var(--text3);letter-spacing:2px}
.hdr-right{position:absolute;left:62%;top:50%;transform:translateY(-50%);
  display:flex;align-items:center;gap:14px;font-size:13px;color:var(--text3)}
.live{display:inline-block;width:7px;height:7px;border-radius:50%;
  background:var(--green);box-shadow:0 0 5px var(--green);animation:pulse 2s infinite;margin-right:5px}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.3}}

.tabs{display:flex;border-bottom:1px solid var(--border);
  background:rgba(20,23,16,.9);position:sticky;top:52px;z-index:98;overflow-x:auto;
  justify-content:center;padding:0;
  -webkit-overflow-scrolling:touch;scrollbar-width:thin}
.tab{padding:10px 18px;font-size:13px;letter-spacing:1px;color:var(--text3);
  border-bottom:2px solid transparent;cursor:pointer;white-space:nowrap;text-transform:uppercase;
  background:none;border-top:none;border-left:none;border-right:none;font-family:var(--mono);
  flex-shrink:0}
.tab:hover{color:var(--text2)}
.tab.active{color:var(--amber);border-bottom-color:var(--amber)}
/* Мобильная адаптация: табы становятся скроллируемыми с выравниванием от начала */
@media (max-width:760px){
  .tabs{justify-content:flex-start;padding:0 8px}
  .tab{padding:10px 12px;font-size:12px;letter-spacing:.5px}
  .hdr{padding:8px 12px;flex-wrap:nowrap;justify-content:space-between;gap:8px}
  .hdr-inner{align-items:flex-start;flex:0 1 auto}
  .hdr-logo{font-size:15px;display:flex;flex-direction:column;align-items:flex-start;gap:2px}
  .hdr-logo #fw-ver{margin-left:0 !important;font-size:11px;color:var(--text3)}
  .hdr-sub{display:none !important}
  .hdr-right{position:static;transform:none;left:auto;top:auto;font-size:11px;gap:8px;flex:0 0 auto;flex-direction:column;align-items:flex-end;text-align:right}
}

.section{display:none;padding:20px 24px;max-width:1080px;margin:0 auto;width:100%}
.section.active{display:block}

.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.grid-3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:10px}
.grid-auto{display:grid;grid-template-columns:repeat(auto-fill,minmax(180px,1fr));gap:8px}
@media(max-width:700px){.grid,.grid-3{grid-template-columns:1fr}}

.card{background:var(--panel);border:1px solid var(--border);padding:14px;position:relative;overflow:hidden}
.card::before{content:'';position:absolute;top:0;left:0;right:0;height:2px;
  background:linear-gradient(90deg,var(--amber),transparent)}
.card.blue::before{background:linear-gradient(90deg,var(--blue),transparent)}
.card.green::before{background:linear-gradient(90deg,var(--green),transparent)}
.card.red::before{background:linear-gradient(90deg,var(--red),transparent)}
.card.full{grid-column:1/-1}
.card-title{font-size:13px;letter-spacing:1px;color:var(--text3);text-transform:uppercase;margin-bottom:12px;display:flex;align-items:center;justify-content:space-between}
.card-title span{cursor:pointer;color:var(--text3);font-size:12px}
.card-title span:hover{color:var(--amber)}

.val-big{font-size:42px;font-weight:700;color:var(--amber);line-height:1;letter-spacing:-1px}
.val-unit{font-size:18px;color:var(--text2);margin-left:3px}
.val-sub{font-size:13px;color:var(--text3);margin-top:6px;line-height:1.7}

.gauge-wrap{display:flex;align-items:center;gap:8px;margin-top:8px}
.gauge{flex:1;height:4px;background:var(--border)}
.gauge-fill{height:100%;background:var(--amber);transition:width .5s}
.gauge-lbl{font-size:13px;color:var(--text3);min-width:40px;text-align:right}

.status-row{display:flex;align-items:center;gap:8px;padding:6px 0;
  border-bottom:1px solid #1c2018;font-size:13px}
.status-row:last-child{border:none}
.dot{width:8px;height:8px;border-radius:50%;flex-shrink:0}
.dot.ok{background:var(--green);box-shadow:0 0 4px var(--green)}
.dot.err{background:var(--red);box-shadow:0 0 4px var(--red)}
.dot.warn{background:var(--amber);box-shadow:0 0 4px var(--amber)}
.status-lbl{flex:1;color:var(--text2)}
.status-val{color:var(--text);font-size:13px;text-align:right}

.btn{display:inline-flex;align-items:center;justify-content:center;font-family:var(--mono);
  font-size:12px;letter-spacing:1px;padding:9px 16px;border:1px solid;cursor:pointer;
  background:transparent;transition:all .15s;text-transform:uppercase;gap:6px}
.btn-amber{border-color:var(--amber);color:var(--amber)}
.btn-amber:hover{background:var(--amber);color:#000}
.btn-red{border-color:var(--red);color:var(--red)}
.btn-red:hover{background:var(--red);color:#fff}
.btn-green{border-color:var(--green);color:var(--green)}
.btn-green:hover{background:var(--green);color:#000}
.btn-blue{border-color:var(--blue);color:var(--blue)}
.btn-blue:hover{background:var(--blue);color:#000}
.btn:disabled{opacity:.4;cursor:not-allowed}
.btn:active{transform:scale(0.96)}
.btn-row{display:flex;gap:8px;flex-wrap:wrap;margin-top:10px}

.form-row{margin-bottom:12px}
.form-row label{display:block;font-size:12px;letter-spacing:0.8px;color:var(--text3);
  text-transform:uppercase;margin-bottom:5px}
input,select{background:#1c2018;border:1px solid var(--border);color:var(--text);
  font-family:var(--mono);font-size:13px;padding:9px 12px;outline:none;width:100%}
input:focus,select:focus{border-color:var(--amber)}
input[type=checkbox]{width:auto}

/* ── WiFi mode cards ── */
.wm-opts{display:flex;gap:8px;margin-bottom:12px}
.wm-opt{flex:1;background:#1c2018;border:1px solid var(--border);padding:14px 10px;cursor:pointer;
  display:flex;flex-direction:column;align-items:center;justify-content:center;text-align:center;gap:8px;min-height:130px;transition:border-color .15s}
.wm-opt:hover{border-color:var(--text3)}
.wm-opt.sel{border-color:var(--amber)}
.wm-opt input[type=radio]{accent-color:var(--amber)}
.wm-opt-body{font-size:13px;line-height:1.8;color:var(--text2);text-align:center}
.wm-opt-body b{color:var(--text);font-size:14px;display:block;margin-bottom:4px}
.wm-opt-body small{color:var(--text3);font-size:12px}

/* ── Pass change ── */
.pass-section{border-top:1px solid var(--border);padding-top:12px;margin-top:12px}
.pass-row{display:flex;gap:8px;align-items:flex-end}
.pass-row .form-row{flex:1;margin:0}
.pass-strength{height:3px;background:var(--border);margin-top:4px;transition:all .2s}

/* ── Chart ── */
.chart-container{position:relative;margin-bottom:10px}
.chart-svg{width:100%;height:100%;display:block;overflow:visible;touch-action:pan-y}
.chart-cursor{position:absolute;top:0;width:1.5px;background:var(--amber);pointer-events:none;z-index:5;display:none;opacity:.7}
.chart-dot{position:absolute;width:10px;height:10px;border-radius:50%;background:var(--amber);border:2px solid #fff;pointer-events:none;z-index:6;display:none;transform:translate(-50%,-50%);box-shadow:0 0 6px var(--amber)}
.period-tabs{display:flex;gap:4px;margin-bottom:8px;flex-wrap:wrap}
.period-btn{padding:5px 12px;font-size:12px;letter-spacing:1px;border:1px solid var(--border);
  background:transparent;color:var(--text3);cursor:pointer;font-family:var(--mono);text-transform:uppercase}
.period-btn.active{border-color:var(--amber);color:var(--amber)}
.series-btns{display:flex;gap:4px;flex-wrap:wrap}
.ser-btn{padding:5px 12px;font-size:12px;border:1px solid var(--border);background:transparent;
  color:var(--text3);cursor:pointer;font-family:var(--mono)}
.ser-btn.active{background:#1c2018}
.ser-btn.s-w.active{border-color:var(--amber);color:var(--amber)}
.ser-btn.s-t.active{border-color:var(--blue);color:var(--blue)}
.ser-btn.s-b.active{border-color:var(--green);color:var(--green)}

/* ── Tooltip ── */
.tip{position:absolute;display:none;background:rgba(13,15,11,.95);border:1px solid var(--border);
  padding:8px 12px;font-size:13px;pointer-events:none;z-index:50;line-height:1.5;min-width:140px;box-shadow:0 4px 12px rgba(0,0,0,.5)}

/* ── Export panel ── */
.exp-panel{background:#0f1209;border:1px solid var(--border);padding:12px;margin-top:10px;position:relative;z-index:10}
.exp-panel-title{font-size:12px;letter-spacing:2px;color:var(--text3);text-transform:uppercase;margin-bottom:10px}
.exp-cols{display:flex;flex-direction:column;gap:4px}
.exp-col-item{display:flex;align-items:center;gap:8px;font-size:13px;color:var(--text2);cursor:pointer}
.exp-col-item input{width:auto}
.exp-date-row{display:flex;gap:8px;margin-bottom:10px}
.exp-date-row .form-row{flex:1;margin:0}

/* ── API ── */
.api-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(200px,1fr));gap:6px;margin-top:8px}
.api-item{background:#1c2018;padding:10px 12px;border:1px solid var(--border);font-size:12px}
.api-method{font-weight:700;margin-bottom:2px}
.api-desc{color:var(--text3)}
.get{color:var(--green)}.post{color:var(--amber)}.del{color:var(--red)}

/* ── Info tiles ── */
.info-tile{background:#1c2018;border:1px solid var(--border);padding:8px 10px}
.info-tile .lbl{font-size:11px;color:var(--text3);letter-spacing:1px;margin-bottom:4px;text-transform:uppercase}
.info-tile .val{font-size:17px}

/* ── Calibration wizard ── */
.wiz-steps{display:flex;gap:0;margin-bottom:12px}
.wiz-step{flex:1;text-align:center;padding:6px 3px;font-size:11px;letter-spacing:.5px;
  border-bottom:2px solid var(--border);color:var(--text3);text-transform:uppercase}
.wiz-step.active{border-color:var(--amber);color:var(--amber)}
.wiz-step.done{border-color:var(--green);color:var(--green)}
.wiz-body{background:#1c2018;border:1px solid var(--border);padding:14px;min-height:80px;font-size:13px;line-height:1.8}

/* ── Toast ── */
.toast{position:fixed;bottom:16px;right:16px;z-index:500;font-family:var(--mono);font-size:11px;
  padding:10px 16px;border:1px solid var(--green);background:rgba(13,15,11,.97);color:var(--green);
  letter-spacing:1px;transform:translateX(220%);transition:transform .3s;max-width:280px}
.toast.show{transform:none}
.toast.err{border-color:var(--red);color:var(--red)}

/* ── Preview table ── */
.prev-wrap{overflow-x:auto;margin-top:10px}
.prev-table{width:100%;border-collapse:collapse;font-size:12px}
.prev-table th{background:#1c2018;color:var(--text3);padding:6px 10px;border:1px solid var(--border);
  text-align:left;font-weight:normal;letter-spacing:1px;text-transform:uppercase;font-size:11px}
.prev-table td{padding:5px 10px;border:1px solid var(--border);color:var(--text2)}
.prev-table tr:hover td{background:#1a1e15}

/* ── Wiz step bar ── */
.wiz-actions{display:flex;gap:8px;margin-top:10px;align-items:center}
.wiz-cur-w{font-size:13px;color:var(--text3);margin-left:auto}
</style>
</head>
<body>

<div class="refresh-bar"><div class="refresh-fill" id="rbar" style="width:100%"></div></div>

<div id="creds-warn" style="display:none;background:#3b1c18;border-bottom:2px solid var(--red);padding:10px 16px;text-align:center;color:var(--red);font-size:13px;letter-spacing:1px">
  ⚠ Используются стандартные учётные данные (admin/beehive). Смените пароль в разделе <a href="#" onclick="nav('wifi');return false" style="color:var(--amber);text-decoration:underline">Wi-Fi → Пароли и доступ</a>.
</div>

<div class="hdr">
  <div class="hdr-inner">
    <div class="hdr-logo">🐝 BeehiveScale <span id="fw-ver" style="font-size:12px;color:var(--text2);font-weight:400;margin-left:8px"></span></div>
  </div>
  <div class="hdr-right">
    <div class="hdr-sub" style="margin-right:16px">LIVE MONITOR · ESP32</div>
    <span><span class="live"></span>ONLINE</span>
    <span id="cur-time">--:--:--</span>
  </div>
</div>

<div class="tabs">
  <button class="tab active"  onclick="nav('main')">⌂ Главная</button>
  <button class="tab"         onclick="nav('chart')">📈 График + Экспорт</button>
  <button class="tab"         onclick="nav('wifi')">📶 Wi-Fi</button>
  <button class="tab"         onclick="nav('settings')">⚙ Настройки</button>
  <button class="tab"         onclick="nav('calib')">⚖ Калибровка</button>
  <button class="tab"         onclick="nav('tg')">✉ Telegram</button>
  <button class="tab"         onclick="nav('api')">🔌 API</button>
</div>

<!-- ═══════════════ MAIN ═══════════════ -->
<div class="section active" id="sec-main">
  <div class="card full" style="padding:10px 14px;margin-bottom:10px;display:flex;align-items:center;justify-content:space-between;gap:10px;flex-wrap:wrap">
    <div style="font-size:13px;color:var(--text2)">
      💤 Auto-sleep: <b id="ka-status" style="color:var(--amber)">обычный режим</b>
    </div>
    <button class="btn btn-blue" style="padding:8px 14px;font-size:13px" onclick="doKeepAlive()">☕ Продлить на 10 мин</button>
  </div>
  <div class="grid">

    <div class="card">
      <div class="card-title">⚖ Текущий вес</div>
      <div class="val-big" id="w-val">--<span class="val-unit">кг</span></div>
      <div class="val-sub" style="display:flex;flex-wrap:wrap;gap:4px 10px"><span style="white-space:nowrap">🎯 От зафикс. точки: <b id="w-ref">--</b> кг</span><span style="white-space:nowrap">Δ: <b id="w-delta" style="color:var(--amber2)">--</b> кг</span></div>
      <div class="val-sub" id="w-ref-date" style="font-size:12px;margin-top:2px"></div>
      <div class="val-sub" style="margin-top:4px">📈 С прошлого замера: <b id="w-period-delta" style="color:var(--green)">--</b> кг (было <b id="w-period-prev">--</b>)</div>
      <div class="gauge-wrap">
        <div class="gauge"><div class="gauge-fill" id="w-gauge" style="width:0%"></div></div>
        <div class="gauge-lbl" id="w-gpct">0%</div>
      </div>
    </div>

    <div class="card blue">
      <div class="card-title">🌡 Температура / Влажность</div>
      <div class="val-big" id="t-val">--<span class="val-unit">°C</span></div>
      <div class="val-sub">RTC: <b id="rtc-val">--</b>°C</div>
      <div class="gauge-wrap">
        <div class="gauge"><div class="gauge-fill" id="t-gauge" style="width:0%;background:var(--blue)"></div></div>
        <div class="gauge-lbl" id="t-gpct">--°C</div>
      </div>
    </div>

    <div class="card">
      <div class="card-title">🔋 Батарея</div>
      <div class="val-big" id="bat-v-big">--<span class="val-unit">В</span></div>
      <div class="val-sub">Заряд: <b id="bat-pct-v">--%</b></div>
      <div class="gauge-wrap">
        <div class="gauge"><div class="gauge-fill" id="bat-gauge" style="width:0%;background:var(--green)"></div></div>
        <div class="gauge-lbl" id="bat-gpct">--%</div>
      </div>
      <div class="card-title" style="margin-top:12px;font-size:11px;color:var(--text2)">💾 Лог / Память</div>
      <div class="val-sub">Занято: <b id="mem-used">--</b> КБ из <b id="mem-total">--</b> КБ</div>
      <div class="gauge-wrap">
        <div class="gauge"><div class="gauge-fill" id="mem-gauge" style="width:0%;background:var(--green)"></div></div>
        <div class="gauge-lbl" id="mem-gpct">--%</div>
      </div>
    </div>

    <div class="card">
      <div class="card-title">📡 Статус системы</div>
      <div class="status-row"><div class="dot warn" id="sr-dot"></div><div class="status-lbl">HX711 датчик</div><div class="status-val" id="sr-val">…</div></div>
      <div class="status-row"><div class="dot warn" id="wf-dot"></div><div class="status-lbl">Wi-Fi</div><div class="status-val" id="wf-val">…</div></div>
      <div class="status-row"><div class="dot ok"></div><div class="status-lbl">Веб-сервер</div><div class="status-val">Активен :80</div></div>
      <div class="status-row"><div class="dot ok" id="sd-dot"></div><div class="status-lbl">Хранилище лог</div><div class="status-val" id="sd-val">--</div></div>
      <div class="status-row"><div class="dot ok" id="heap-dot"></div><div class="status-lbl">Free Heap</div><div class="status-val" id="heap-val">--</div></div>
      <div class="status-row"><div class="dot ok"></div><div class="status-lbl">Пробуждений</div><div class="status-val" id="wkc-val">--</div></div>
      <div class="status-row"><div class="dot ok"></div><div class="status-lbl">Cal. Factor</div><div class="status-val" id="cf-val">--</div></div>
      <div class="status-row"><div class="dot ok"></div><div class="status-lbl">Offset</div><div class="status-val" id="ofs-val">--</div></div>
      <div class="status-row"><div class="dot ok"></div><div class="status-lbl">Дата/время</div><div class="status-val" id="dt-val">--</div></div>
      <div class="status-row"><div class="dot ok"></div><div class="status-lbl">Uptime</div><div class="status-val" id="upt-val">--</div></div>
    </div>

    <div class="card full">
      <div class="card-title">🐝 Информация об улье (сегодня)</div>
      <div class="grid-auto">
        <div class="info-tile"><div class="lbl">Сезон</div><div class="val" style="color:var(--amber)" id="hi-season">--</div></div>
        <div class="info-tile"><div class="lbl">Вес мин / макс</div><div class="val" style="color:var(--green);font-size:12px" id="hi-wrange">--/-- кг</div></div>
        <div class="info-tile"><div class="lbl">Темп мин / макс</div><div class="val" style="color:var(--blue);font-size:12px" id="hi-trange">--/-- °C</div></div>
        <div class="info-tile"><div class="lbl">Изменение за день</div><div class="val" id="hi-delta">-- кг</div></div>
        <div class="info-tile"><div class="lbl">Точек сегодня</div><div class="val" style="color:var(--text2)" id="hi-count">--</div></div>
        <div class="info-tile"><div class="lbl">Дней наблюдений</div><div class="val" style="color:var(--text2)" id="hi-days">--</div></div>
      </div>
    </div>

    <div class="card full">
      <div class="card-title">
        📈 Мини-график
        <span onclick="nav('chart')">[открыть полный →]</span>
      </div>
      <div class="chart-container" style="aspect-ratio:3/2">
        <div class="tip" id="tip-mini"></div>
        <div class="chart-cursor" id="cur-mini"></div>
        <div class="chart-dot" id="dot-mini"></div>
        <svg id="mini-svg" class="chart-svg" viewBox="0 0 900 600" preserveAspectRatio="xMidYMid meet"
             onmousemove="onTip(event,'mini')" onmouseleave="hideTip('mini')"
             ontouchstart="onTip(event,'mini')" ontouchmove="onTip(event,'mini')" ontouchend="onTipEnd('mini')">
          <text x="450" y="280" text-anchor="middle" fill="#506040" font-size="22">Загрузка...</text>
        </svg>
      </div>
    </div>


  </div>
</div>

<!-- ═══════════════ CHART + EXPORT ═══════════════ -->
<div class="section" id="sec-chart">

  <!-- Верхняя панель управления графиком -->
  <div class="card" style="margin-bottom:10px">
    <div style="display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px;margin-bottom:10px">
      <div class="period-tabs" style="margin:0">
        <button class="period-btn active" onclick="setPeriod(1,this)">1ч</button>
        <button class="period-btn" onclick="setPeriod(6,this)">6ч</button>
        <button class="period-btn" onclick="setPeriod(24,this)">24ч</button>
        <button class="period-btn" onclick="setPeriod(72,this)">3д</button>
        <button class="period-btn" onclick="setPeriod(168,this)">7д</button>
        <button class="period-btn" onclick="setPeriod(0,this)">Всё</button>
      </div>
      <div class="series-btns">
        <button class="ser-btn s-w active" id="sb-w" onclick="toggleSeries('w',this)">⚖ Вес</button>
        <button class="ser-btn s-t active" id="sb-t" onclick="toggleSeries('t',this)">🌡 Темп</button>
        <button class="ser-btn s-b active" id="sb-b" onclick="toggleSeries('b',this)">🔋 Батарея</button>
      </div>
    </div>

    <!-- График веса -->
    <div id="cwrap-w" style="margin-bottom:20px">
      <div style="font-size:13px;color:var(--text3);margin-bottom:6px">
        Мин: <b id="c-wmin" style="color:var(--amber)">--</b> &nbsp;
        Макс: <b id="c-wmax" style="color:var(--amber)">--</b> &nbsp;
        Среднее: <b id="c-wavg" style="color:var(--amber)">--</b> кг &nbsp;
        Точек: <b id="c-pts">0</b>
      </div>
      <div class="chart-container" style="aspect-ratio:9/5">
        <div class="tip" id="tip-w"></div>
        <div class="chart-cursor" id="cur-w"></div>
        <div class="chart-dot" id="dot-w"></div>
        <svg id="chart-w" class="chart-svg" viewBox="0 0 900 500" preserveAspectRatio="xMidYMid meet"
             onmousemove="onTip(event,'w')" onmouseleave="hideTip('w')"
             ontouchstart="onTip(event,'w')" ontouchmove="onTip(event,'w')" ontouchend="onTipEnd('w')">
          <text x="450" y="240" text-anchor="middle" fill="#506040" font-size="22">Загрузка...</text>
        </svg>
      </div>
    </div>

    <!-- График температуры -->
    <div id="cwrap-t" style="margin-bottom:20px;border-top:1px solid var(--border);padding-top:14px">
      <div style="font-size:13px;color:var(--text3);margin-bottom:6px">
        Темп мин: <b id="c-tmin" style="color:var(--blue)">--</b> &nbsp;
        Макс: <b id="c-tmax" style="color:var(--blue)">--</b> °C
      </div>
      <div class="chart-container" style="aspect-ratio:9/5">
        <div class="tip" id="tip-t"></div>
        <div class="chart-cursor" id="cur-t"></div>
        <div class="chart-dot" id="dot-t"></div>
        <svg id="chart-t" class="chart-svg" viewBox="0 0 900 500" preserveAspectRatio="xMidYMid meet"
             onmousemove="onTip(event,'t')" onmouseleave="hideTip('t')"
             ontouchstart="onTip(event,'t')" ontouchmove="onTip(event,'t')" ontouchend="onTipEnd('t')">
          <text x="450" y="240" text-anchor="middle" fill="#506040" font-size="22">Загрузка...</text>
        </svg>
      </div>
    </div>

    <!-- График батареи -->
    <div id="cwrap-b" style="border-top:1px solid var(--border);padding-top:14px">
      <div class="chart-container" style="aspect-ratio:9/5">
        <div class="tip" id="tip-b"></div>
        <div class="chart-cursor" id="cur-b"></div>
        <div class="chart-dot" id="dot-b"></div>
        <svg id="chart-b" class="chart-svg" viewBox="0 0 900 500" preserveAspectRatio="xMidYMid meet"
             onmousemove="onTip(event,'b')" onmouseleave="hideTip('b')"
             ontouchstart="onTip(event,'b')" ontouchmove="onTip(event,'b')" ontouchend="onTipEnd('b')">
          <text x="450" y="240" text-anchor="middle" fill="#506040" font-size="22">Загрузка...</text>
        </svg>
      </div>
    </div>
  </div>

  <!-- ── Панель экспорта ── -->
  <div class="exp-panel">
    <div class="exp-panel-title">⬇ Экспорт данных</div>

    <div class="exp-date-row">
      <div class="form-row"><label>С даты</label><input type="date" id="exp-from"></div>
      <div class="form-row"><label>По дату</label><input type="date" id="exp-to"></div>
    </div>

    <div style="display:flex;gap:20px;flex-wrap:wrap;margin-bottom:12px">
      <div>
        <div style="font-size:12px;letter-spacing:1px;color:var(--text3);text-transform:uppercase;margin-bottom:6px">Столбцы</div>
        <div class="exp-cols">
          <label class="exp-col-item"><input type="checkbox" id="col-dt" checked> Дата/время</label>
          <label class="exp-col-item"><input type="checkbox" id="col-w"  checked> Вес (кг)</label>
          <label class="exp-col-item"><input type="checkbox" id="col-t"  checked> Температура (°C)</label>
          <label class="exp-col-item"><input type="checkbox" id="col-bat"> Батарея (В)</label>
        </div>
      </div>
      <div>
        <div style="font-size:12px;letter-spacing:1px;color:var(--text3);text-transform:uppercase;margin-bottom:6px">Листы Excel</div>
        <div style="font-size:13px;color:var(--text3);line-height:1.9">
          ✓ «Данные» — все записи<br>
          ✓ «Статистика» — мин/макс/среднее<br>
          ✓ «Дневные итоги» — группировка
        </div>
      </div>
      <div>
        <div style="font-size:12px;letter-spacing:1px;color:var(--text3);text-transform:uppercase;margin-bottom:6px">Прямое скачивание с SD</div>
        <div class="btn-row" style="margin:0;flex-direction:column;gap:6px">
          <button class="btn btn-green" style="width:100%" onclick="window.open('/api/log','_blank')">📥 Весь CSV (SD-карта)</button>
          <div style="display:flex;gap:6px">
            <input type="date" id="exp-date-sd" style="flex:1;padding:5px 8px">
            <button class="btn btn-amber" onclick="dlSdDate()">📥 За дату</button>
          </div>
        </div>
      </div>
    </div>

    <div class="btn-row">
      <button class="btn btn-green" onclick="exportExcel()">📊 Скачать Excel (.xlsx)</button>
      <button class="btn btn-amber" onclick="exportCsv()">📄 Скачать CSV</button>
      <button class="btn btn-blue"  onclick="previewExport()">👁 Предпросмотр</button>
      <button class="btn btn-red"   onclick="if(confirm('Очистить лог?'))doApi('/api/log/clear')">🗑 Очистить лог</button>
    </div>

    <div id="preview-wrap" style="display:none">
      <div style="font-size:12px;color:var(--text3);margin-top:10px;letter-spacing:1px;text-transform:uppercase">
        Предпросмотр (последние 10 строк)
      </div>
      <div class="prev-wrap">
        <table class="prev-table" id="prev-table"></table>
      </div>
    </div>
  </div>
</div>

<!-- ═══════════════ WIFI ═══════════════ -->
<div class="section" id="sec-wifi">
  <div class="grid">

    <!-- Режим подключения -->
    <div class="card">
      <div class="card-title">📶 Режим подключения</div>
      <div class="wm-opts">
        <div class="wm-opt sel" id="wopt-ap" onclick="selWm(0)">
          <input type="radio" name="wm" checked>
          <div class="wm-opt-body">
            <b>📡 Точка доступа (AP)</b>
            <small>Устройство создаёт свою сеть<br>SSID: BeehiveScale<br>IP: 192.168.4.1</small>
          </div>
        </div>
        <div class="wm-opt" id="wopt-sta" onclick="selWm(1)">
          <input type="radio" name="wm">
          <div class="wm-opt-body">
            <b>🌐 Роутер (STA)</b>
            <small>Подключение к домашнему Wi-Fi<br>IP: назначает роутер (DHCP)<br>NTP и Telegram доступны</small>
          </div>
        </div>
      </div>
      <div id="sta-block" style="display:none">
        <div class="form-row"><label>SSID роутера</label><input type="text" id="wifi-ssid" placeholder="Название вашей Wi-Fi сети" maxlength="32" autocomplete="off"></div>
        <div class="form-row"><label>Пароль роутера</label><input type="password" id="wifi-pass" placeholder="Пароль (оставьте пустым чтобы не менять)" maxlength="32" autocomplete="new-password"></div>
      </div>
      <div class="btn-row">
        <button class="btn btn-green" onclick="saveWifi()">💾 Сохранить и перезагрузить</button>
        <button class="btn btn-blue"  onclick="doApi('/api/ntp')">🕐 NTP Время</button>
      </div>
      <div style="font-size:13px;color:var(--text3);margin-top:10px;line-height:1.7">
        <b style="color:var(--amber)">AP режим:</b> прямое подключение к устройству → 192.168.4.1<br>
        <b style="color:var(--amber)">STA режим:</b> устройство в вашей сети → доступен NTP и Telegram
      </div>
    </div>

    <!-- Смена паролей -->
    <div class="card">
      <div class="card-title">🔒 Пароли и доступ</div>

      <!-- Пароль AP точки доступа -->
      <div style="margin-bottom:14px">
        <div style="font-size:13px;color:var(--text2);margin-bottom:8px;font-weight:600">Пароль сети AP (BeehiveScale)</div>
        <div class="form-row" style="margin-bottom:6px">
          <label>Новый пароль (8–23 символа)</label>
          <input type="password" id="ap-pass-new" placeholder="••••••••" maxlength="23" autocomplete="new-password" oninput="checkPassStrength('ap-pass-new','ap-pass-str')">
          <div class="pass-strength" id="ap-pass-str"></div>
        </div>
        <div class="form-row" style="margin-bottom:6px">
          <label>Повтор пароля</label>
          <input type="password" id="ap-pass-confirm" placeholder="••••••••" maxlength="23" autocomplete="new-password">
        </div>
        <button class="btn btn-amber" onclick="saveApPass()">🔑 Сменить пароль AP</button>
        <div style="font-size:13px;color:var(--text3);margin-top:6px">
          Применится после перезагрузки. После смены подключайтесь к AP с новым паролем.
        </div>
      </div>

      <!-- Пароль веб-интерфейса -->
      <div class="pass-section">
        <div style="font-size:13px;color:var(--text2);margin-bottom:8px;font-weight:600">Веб-авторизация (admin)</div>
        <div class="form-row" style="margin-bottom:6px">
          <label>Логин (оставьте пустым чтобы не менять)</label>
          <input type="text" id="web-user-new" placeholder="admin" maxlength="23" autocomplete="off">
        </div>
        <div class="form-row" style="margin-bottom:6px">
          <label>Новый пароль (6-31)</label>
          <input type="password" id="web-pass-new" placeholder="********" maxlength="31" autocomplete="new-password">
        </div>
        <div class="form-row" style="margin-bottom:6px">
          <label>Повтор пароля</label>
          <input type="password" id="web-pass-confirm" placeholder="********" maxlength="31" autocomplete="new-password">
        </div>
        <button class="btn btn-amber" onclick="saveWebAuth()">🔑 Сменить пароль web</button>
        <div style="font-size:13px;color:var(--text3);margin-top:6px">
          После смены браузер запросит новый логин/пароль при первом запросе.
        </div>
      </div>

      <!-- OTA пароль -->
      <div class="pass-section">
        <div style="font-size:13px;color:var(--text2);margin-bottom:8px;font-weight:600">Пароль OTA (прошивка по воздуху)</div>
        <div class="form-row" style="margin-bottom:6px">
          <label>Новый пароль OTA (6-31)</label>
          <input type="password" id="ota-pass-new" placeholder="********" maxlength="31" autocomplete="new-password">
        </div>
        <button class="btn btn-amber" onclick="saveOtaPass()">🔑 Сменить OTA пароль</button>
        <div style="font-size:13px;color:var(--text3);margin-top:6px">
          Вступит в силу после перезагрузки.
        </div>
      </div>
    </div>

  </div>
</div>

<!-- ═══════════════ SETTINGS ═══════════════ -->
<div class="section" id="sec-settings">
  <div class="grid">
    <div class="card">
      <div class="card-title">⚙ Параметры устройства</div>
      <div class="form-row"><label>Порог тревоги Telegram (кг, 0.1–10)</label><input type="number" id="cfg-alert" step="0.1" min="0.1" max="10" placeholder="0.5"></div>
      <div class="form-row"><label>Эталонный груз калибровки (г, 100–50000)</label><input type="number" id="cfg-calib" step="100" min="100" max="50000" placeholder="1000"></div>
      <div class="form-row"><label>Скорость отклика весов (0.1=медленно, 0.3=средне, 0.6=быстро)</label><input type="number" id="cfg-ema" step="0.05" min="0.05" max="0.9" placeholder="0.3"><div style="font-size:12px;color:var(--text3);margin-top:4px">EMA α-фильтр. <b>0.1</b> ≈ 30 сек до стабилизации (для улья, фильтрует пчёл). <b>0.3</b> ≈ 10 сек (рекомендуется). <b>0.6</b> ≈ 4 сек (для тестов/калибровки).</div></div>
      <div class="form-row"><label>Deep Sleep интервал (сек, 30–86400)</label><input type="number" id="cfg-sleep" step="1" min="30" max="86400" placeholder="900"></div>
      <div class="form-row"><label>Расписание замеров (HH:MM через пробел, до 8 времён)</label><input type="text" id="cfg-sched" placeholder="08:00 14:00 20:00" maxlength="60"></div>
      <div class="form-row"><label>Уйти в сон после бездействия (сек, 0=не засыпать)</label><input type="number" id="cfg-autosleep" step="1" min="0" max="86400" placeholder="180"></div>
      <div class="form-row"><label>Таймаут подсветки LCD (сек, 0=всегда)</label><input type="number" id="cfg-bl" step="10" min="0" max="3600" placeholder="30"></div>
      <div class="btn-row">
        <button class="btn btn-green" onclick="saveSettings()">💾 Сохранить</button>
        <button class="btn btn-blue"  onclick="loadConfig()">↺ Загрузить</button>
        <button class="btn btn-red"   onclick="if(confirm('Перезагрузить ESP?'))doApi('/api/reboot')">↺ Перезагрузить</button>
      </div>
    </div>
    <div class="card">
      <div class="card-title">ℹ Описание</div>
      <div style="font-size:13px;color:var(--text3);line-height:2">
        <b style="color:var(--amber)">Порог тревоги</b> — изменение веса для уведомления в Telegram (роение, кража).<br>
        <b style="color:var(--amber)">Эталонный груз</b> — масса гири при калибровке HX711.<br>
        <b style="color:var(--amber)">Скорость отклика</b> — коэффициент EMA-фильтра шума. <b>В улье ставь 0.1–0.2</b> (фильтрует прилёт/вылет пчёл). <b>Для тестов 0.5–0.6</b> (быстрый отклик).<br>
        <b style="color:var(--amber)">Deep Sleep</b> — длительность сна ESP. Используется только если расписание не задано.<br>
        <b style="color:var(--amber)">Расписание</b> — конкретные времена пробуждения (напр. 08:00 14:00 20:00). Если задано — приоритет над интервалом.<br>
        <b style="color:var(--amber)">Auto-sleep</b> — через сколько секунд бездействия уйти в сон. 0 = не засыпать (отладка). Сбрасывается любой кнопкой и веб-запросом.<br>
        <b style="color:var(--amber)">Подсветка LCD</b> — 0 = всегда включена; иначе — таймаут без нажатий.
      </div>
    </div>
    <div class="card full">
      <div class="card-title">💾 Бэкап и восстановление</div>
      <div style="font-size:13px;color:var(--text3);margin-bottom:12px;line-height:1.7">
        Скачайте полный бэкап всех настроек (калибровка, WiFi, Telegram, настройки).<br>
        При каждом сохранении настроек бэкап автоматически копируется на SD-карту.
      </div>
      <div class="btn-row" style="gap:10px;flex-wrap:wrap">
        <button class="btn btn-green" onclick="downloadBackup()">📥 Скачать бэкап</button>
        <label class="btn btn-amber" style="cursor:pointer">📤 Загрузить бэкап<input type="file" id="backup-file" accept=".json" style="display:none" onchange="restoreBackup(this)"></label>
        <button class="btn btn-blue" onclick="viewBackup()">👁 Просмотр</button>
      </div>
      <pre id="backup-preview" style="display:none;font-size:12px;color:var(--text2);line-height:1.5;margin-top:10px;max-height:200px;overflow:auto;background:var(--bg);padding:8px;border:1px solid var(--border)"></pre>
    </div>
  </div>
</div>

<!-- ═══════════════ CALIBRATION ═══════════════ -->
<div class="section" id="sec-calib">
  <div class="grid">
    <div class="card">
      <div class="card-title">🧙 Мастер калибровки</div>
      <div class="wiz-steps">
        <div class="wiz-step active" id="ws0">1</div>
        <div class="wiz-step" id="ws1">2</div>
        <div class="wiz-step" id="ws2">3</div>
        <div class="wiz-step" id="ws3">4</div>
        <div class="wiz-step" id="ws4">✓</div>
      </div>
      <div class="wiz-body" id="wiz-body"></div>
      <div class="wiz-actions">
        <button class="btn btn-amber" id="wiz-btn" onclick="wizNext()">Далее →</button>
        <button class="btn btn-red" onclick="wizReset()">↺ Сначала</button>
        <span class="wiz-cur-w">Текущий вес: <b id="wiz-w" style="color:var(--amber)">--</b> кг</span>
      </div>
    </div>
    <div class="card">
      <div class="card-title">✏ Ручная калибровка</div>
      <div class="form-row">
        <label>Cal. Factor (текущий: <b id="cf-live" style="color:var(--amber)">--</b>)</label>
        <input type="number" id="calib-cf" step="1" min="100" max="100000" placeholder="напр. 2280">
      </div>
      <div class="form-row">
        <label>Offset (текущий: <b id="ofs-live" style="color:var(--amber)">--</b>)</label>
        <input type="number" id="calib-ofs" step="1" placeholder="обычно не меняется">
      </div>
      <div class="btn-row">
        <button class="btn btn-amber" onclick="applyCalib()">✓ Применить</button>
        <button class="btn btn-blue"  onclick="doApi('/api/tare')">⊘ Тара</button>
        <button class="btn btn-green" onclick="doApi('/api/save')">📍 Зафиксировать вес как точку отсчёта</button>
      </div>
      <div style="font-size:13px;color:var(--text3);margin-top:10px;line-height:1.7">
        Подберите Cal.Factor так, чтобы показание<br>совпало с реальной массой эталонного груза.
      </div>
    </div>
  </div>
</div>

<!-- ═══════════════ TELEGRAM ═══════════════ -->
<div class="section" id="sec-tg">
  <div class="grid">
    <div class="card">
      <div class="card-title">✉ Telegram Bot</div>
      <div class="form-row"><label>Bot Token (получить у @BotFather)</label><input type="password" id="tg-token" placeholder="123456789:ABC..." autocomplete="off"></div>
      <div class="form-row"><label>Chat ID (узнать через @userinfobot)</label><input type="text" id="tg-chatid" placeholder="-100123456789"></div>
      <div class="form-row"><label>Интервал отчётов (мин, 0=откл, 360=6ч, 1440=раз в день)</label><input type="number" id="tg-report-int" min="0" max="10080" step="60" placeholder="360"></div>
      <div class="btn-row">
        <button class="btn btn-green" onclick="saveTelegram()">💾 Сохранить</button>
        <button class="btn btn-blue"  onclick="doApi('/api/tg/test')">✉ Тест</button>
      </div>
    </div>
    <div class="card">
      <div class="card-title">🔔 Триггеры уведомлений</div>
      <div style="font-size:11px;color:var(--text3);line-height:2.1">
        ✓ Резкое изменение веса (порог: <b id="tg-thresh" style="color:var(--amber)">-- кг</b>)<br>
        ✓ Роение — быстрая потеря веса<br>
        ✓ Кража — резкое изменение<br>
        ✓ Низкий заряд батареи (&lt; 3.5 В)<br>
        ✓ Восстановление соединения<br>
        ✓ Тестовое сообщение (кнопка Тест)
      </div>
      <div style="font-size:13px;color:var(--text3);margin-top:10px;padding-top:10px;border-top:1px solid var(--border)">
        Для работы Telegram нужен режим Wi-Fi <b style="color:var(--amber)">STA</b> (подключение к роутеру)
      </div>
    </div>
  </div>
</div>

<!-- ═══════════════ API ═══════════════ -->
<div class="section" id="sec-api">
  <div class="card" style="margin-bottom:10px">
    <div class="card-title">🔌 REST API эндпоинты</div>
    <div class="api-grid">
      <div class="api-item"><div class="api-method get">GET /api/data</div><div class="api-desc">Все показания (вес/темп/бат/статус)</div></div>
      <div class="api-item"><div class="api-method get">GET /api/config</div><div class="api-desc">Конфигурация (alertDelta, ema, sleep…)</div></div>
      <div class="api-item"><div class="api-method get">GET /api/log</div><div class="api-desc">Скачать лог CSV (опц. ?date=YYYY-MM-DD)</div></div>
      <div class="api-item"><div class="api-method get">GET /api/log/json</div><div class="api-desc">Лог в JSON (для Grafana/Home Assistant)</div></div>
      <div class="api-item"><div class="api-method get">GET /api/daystat</div><div class="api-desc">Суточная статистика (опц. ?date=)</div></div>
      <div class="api-item"><div class="api-method post">POST /api/tare</div><div class="api-desc">Тарировка весов</div></div>
      <div class="api-item"><div class="api-method post">POST /api/save</div><div class="api-desc">Сохранить текущий вес как эталон</div></div>
      <div class="api-item"><div class="api-method post">POST /api/settings</div><div class="api-desc">Изменить настройки JSON {alertDelta…}</div></div>
      <div class="api-item"><div class="api-method post">POST /api/ntp</div><div class="api-desc">Синхронизация времени NTP</div></div>
      <div class="api-item"><div class="api-method post">POST /api/reboot</div><div class="api-desc">Перезагрузить ESP32</div></div>
      <div class="api-item"><div class="api-method del">POST /api/log/clear</div><div class="api-desc">Очистить лог на SD/Flash</div></div>
      <div class="api-item"><div class="api-method post">POST /api/tg/settings</div><div class="api-desc">Сохранить Telegram token/chatId</div></div>
      <div class="api-item"><div class="api-method post">POST /api/tg/test</div><div class="api-desc">Тестовое сообщение в Telegram</div></div>
      <div class="api-item"><div class="api-method post">POST /api/calib/set</div><div class="api-desc">Установить calibFactor / offset</div></div>
      <div class="api-item"><div class="api-method post">POST /api/wifi/settings</div><div class="api-desc">Режим Wi-Fi + SSID/пароль роутера</div></div>
      <div class="api-item"><div class="api-method get">GET /api/backup</div><div class="api-desc">Скачать полный бэкап настроек (JSON)</div></div>
      <div class="api-item"><div class="api-method post">POST /api/backup/restore</div><div class="api-desc">Восстановить настройки из JSON бэкапа</div></div>
    </div>
  </div>
  <div class="card">
    <div class="card-title">👁 Live /api/data
      <button class="btn btn-blue" style="padding:2px 10px;font-size:12px" onclick="refreshApiView()">↺ Обновить</button>
    </div>
    <pre id="api-json" style="font-size:13px;color:var(--text2);line-height:1.6;overflow-x:auto;white-space:pre-wrap">Загрузка...</pre>
  </div>
</div>

<div class="toast" id="toast"></div>

<script>
// ── Globals ─────────────────────────────────────────────────────────
const REFRESH = 5000;
let _start = Date.now();
let _all = [];           // весь лог
let _curWeight = 0;      // текущий вес для мини-графика
let _periodH = 1;
let _serVisible = {w:true, t:true, b:true};
let _wizStep = 0;
let _wifiMode = 0;
let _csrfTok = '';       // обновляется через /api/data
// Данные для тултипов (по серии)
let _tipPts = {};
function esc(s){var d=document.createElement('div');d.textContent=String(s);return d.innerHTML;}
// Обёртка fetch с автоматическим X-CSRF-Token для POST
function apiFetch(url, opts){
  opts = opts || {};
  opts.headers = opts.headers || {};
  if ((opts.method||'GET').toUpperCase() !== 'GET' && _csrfTok){
    opts.headers['X-CSRF-Token'] = _csrfTok;
  }
  return fetch(url, opts);
}

// ── Nav ──────────────────────────────────────────────────────────────
function nav(id) {
  document.querySelectorAll('.section').forEach(s=>s.classList.remove('active'));
  document.querySelectorAll('.tab').forEach(t=>t.classList.remove('active'));
  document.getElementById('sec-'+id).classList.add('active');
  const idx = {main:0,chart:1,wifi:2,settings:3,calib:4,tg:5,api:6};
  document.querySelectorAll('.tab')[idx[id]].classList.add('active');
  if (id==='chart') renderCharts();
  if (id==='api')   refreshApiView();
  if (id==='settings'||id==='tg') loadConfig(true);
  if (id==='wifi') loadConfig(true);
  if (id==='calib') { fetchData(); }   // немедленно обновить cf-live, ofs-live, wiz-w
}

// ── Refresh bar ───────────────────────────────────────────────────────
const bar = document.getElementById('rbar');
function tickBar() {
  const pct = Math.max(0, 100-(Date.now()-_start)/REFRESH*100);
  bar.style.width = pct+'%';
  if (Date.now()-_start < REFRESH) requestAnimationFrame(tickBar);
}
tickBar();

// ── Clock ─────────────────────────────────────────────────────────────
(function tick() {
  const t=new Date(), p=n=>String(n).padStart(2,'0');
  document.getElementById('cur-time').textContent = p(t.getHours())+':'+p(t.getMinutes())+':'+p(t.getSeconds());
  setTimeout(tick, 1000);
})();

// ── Toast ─────────────────────────────────────────────────────────────
function toast(msg, err) {
  const el = document.getElementById('toast');
  el.textContent=msg; el.className='toast'+(err?' err':'')+' show';
  setTimeout(()=>el.classList.remove('show'), 3500);
}

// ── API ───────────────────────────────────────────────────────────────
function doApi(url) {
  const method = (url.includes('/data')||(url.includes('/log')&&!url.includes('/clear'))||url.includes('/config')||url.includes('/daystat')||url.endsWith('/backup')) ? 'GET' : 'POST';
  return apiFetch(url,{method}).then(r=>r.json()).then(d=>{
    toast(d.msg||(d.ok?'OK':'Ошибка'), !d.ok); return d;
  }).catch(()=>toast('Нет связи',true));
}

// ── Keepalive: продлить работу на 10 минут ────────────────────────────
function doKeepAlive() {
  apiFetch('/api/keepalive',{method:'POST'}).then(r=>r.json()).then(d=>{
    toast(d.msg||(d.ok?'Продлено':'Ошибка'), !d.ok);
    if (d.ok) fetch('/api/data').then(r=>r.json()).then(updDash).catch(()=>{});
  }).catch(()=>toast('Нет связи',true));
}
function _fmtMmSs(sec) {
  if (sec <= 0) return '0:00';
  const m = Math.floor(sec/60), s = sec%60;
  return m + ':' + (s<10?'0':'') + s;
}

// ── Fetch /api/data ───────────────────────────────────────────────────
// ESP32 WebServer синхронный — обрабатывает 1 запрос за раз. Параллельные fetch()
// от браузера сериализуются в случайном порядке, и медленный /api/log/json (стрим
// всего лога) может оказаться первым → дашборд не показывается пока лог не передан.
// Решение: chain'им запросы — /api/data → /api/daystat → /api/log/json.
// Дашборд рендерится сразу, лог тянется в фоне. Лог обновляем не чаще раз в 30 сек.
let _lastLogFetch = 0;
function fetchData() {
  fetch('/api/data').then(r=>r.json()).then(updDash).catch(()=>{})
    .then(()=>fetch('/api/daystat').then(r=>r.json()).then(updHive).catch(()=>{}))
    .then(()=>{
      const now = Date.now();
      // Throttle: первый раз грузим всегда, дальше — раз в 30 сек
      if (_lastLogFetch && (now - _lastLogFetch) < 30000) return;
      _lastLogFetch = now;
      loadLog();
    });
}

function updDash(d) {
  if (d && d.csrf) _csrfTok = d.csrf;
  if (d && d.credsDefault) showCredsWarning();
  if (d && d.fw) { const fv=document.getElementById('fw-ver'); if (fv && !fv.textContent) fv.textContent='v'+d.fw; }
  // Статус продления auto-sleep
  const kaEl = document.getElementById('ka-status');
  if (kaEl) {
    const left = parseInt(d && d.keepLeftSec || 0);
    if (left > 0) {
      kaEl.textContent = 'продлено, осталось ' + _fmtMmSs(left);
      kaEl.style.color = 'var(--green)';
    } else {
      kaEl.textContent = 'обычный режим';
      kaEl.style.color = 'var(--amber)';
    }
  }
  const w = parseFloat(d.weight)||0;
  _curWeight = w;
  setText('w-val', w.toFixed(2)+'<span class="val-unit">кг</span>', true);
  setText('w-ref', parseFloat(d.prev||0).toFixed(2));
  const dw = w-parseFloat(d.prev||0);
  const dwEl=document.getElementById('w-delta');
  dwEl.textContent=(dw>=0?'+':'')+dw.toFixed(2);
  dwEl.style.color=dw>0?'var(--green)':dw<0?'var(--red)':'var(--amber2)';
  // Дата фиксации (Unix timestamp → ДД.ММ.ГГГГ ЧЧ:ММ)
  const refDate = parseInt(d.prevDate||0);
  const rdEl = document.getElementById('w-ref-date');
  if (rdEl) {
    if (refDate > 0) {
      const dt = new Date(refDate * 1000);
      const dd = String(dt.getDate()).padStart(2,'0');
      const mm = String(dt.getMonth()+1).padStart(2,'0');
      const yyyy = dt.getFullYear();
      const hh = String(dt.getHours()).padStart(2,'0');
      const mi = String(dt.getMinutes()).padStart(2,'0');
      // Дни с момента фиксации (для контекста "уже X дней наблюдаю")
      const daysAgo = Math.floor((Date.now()/1000 - refDate) / 86400);
      rdEl.textContent = 'от ' + dd + '.' + mm + '.' + yyyy + ' ' + hh + ':' + mi
                        + (daysAgo > 0 ? ', ' + daysAgo + ' дн назад' : '');
    } else {
      rdEl.textContent = 'не зафикс.';
    }
  }
  // Дельта "за период" — сравнение с весом на момент прошлого TG-отчёта
  const lastRep = parseFloat(d.lastRep||0);
  const hasRep  = d.hasRep === true || d.hasRep === 'true';
  const wpdEl = document.getElementById('w-period-delta');
  const wppEl = document.getElementById('w-period-prev');
  if (wpdEl && wppEl) {
    if (hasRep) {
      const dp = w - lastRep;
      wpdEl.textContent = (dp>=0?'+':'')+dp.toFixed(2);
      wpdEl.style.color = dp>0?'var(--green)':dp<0?'var(--red)':'var(--amber2)';
      wppEl.textContent = lastRep.toFixed(2);
    } else {
      wpdEl.textContent = '—';
      wppEl.textContent = 'нет данных';
    }
  }
  setGauge('w-gauge','w-gpct', Math.min(100,w/80*100), '', '%');

  const t=parseFloat(d.temp), rtcT=parseFloat(d.rtcT||0);
  if (!isNaN(t)&&t>-90) {
    setText('t-val',t.toFixed(1)+'<span class="val-unit">°C</span>',true);
    setGauge('t-gauge','t-gpct', Math.min(100,Math.max(0,(t+20)/70*100)), t.toFixed(1), '°C');
  } else {
    setText('t-val','---<span class="val-unit">°C</span>',true);
    setGauge('t-gauge','t-gpct', 0, '---', '°C');
  }
  setText('rtc-val', rtcT.toFixed(1));

  const bv=parseFloat(d.batV||0), bp=parseFloat(d.batPct||0);
  setText('bat-v-big',bv.toFixed(2)+'<span class="val-unit">В</span>',true);
  setText('bat-pct-v',bp.toFixed(0)+'%');
  const bColor=bp<20?'var(--red)':bp<40?'var(--amber)':'var(--green)';
  document.getElementById('bat-gauge').style.background=bColor;
  setGauge('bat-gauge','bat-gpct',bp,'','%');

  // Status
  setDot('sr-dot',d.sensor,'ok','err'); setText('sr-val',d.sensor?'OK':'Ошибка');
  setDot('wf-dot',d.wifi,'ok','warn'); setText('wf-val',d.wifi?'Подключён':'AP режим');
  setText('wkc-val',d.wakeups||0);
  setText('cf-val',parseFloat(d.cf||0).toFixed(0));
  setText('ofs-val',d.offset||0);
  setText('dt-val',d.datetime||'--');
  setText('upt-val',d.uptime||'--');
  const heap=parseInt(d.heap||0);
  setText('heap-val',(heap/1024).toFixed(1)+' KB');
  setDot('heap-dot',heap>5000,'ok','warn');
  const sdKb=Math.round(parseInt(d.sdLog||0)/1024);
  const freeKb=Math.round(parseInt(d.sdFree||0)/1024);
  // Свободное место известно только при LittleFS (fallback); SD-библиотека его не отдаёт
  const freeKnown=!!d.sdFallback;
  const totalKb=freeKnown?sdKb+freeKb:0;
  const memPct=totalKb>0?Math.min(100,sdKb/totalKb*100):0;
  // Gauge памяти/лога
  const mColor=memPct>90?'var(--red)':memPct>70?'var(--amber)':'var(--green)';
  const memGauge=document.getElementById('mem-gauge');
  if(memGauge){memGauge.style.background=mColor;setGauge('mem-gauge','mem-gpct',memPct,memPct.toFixed(0),'%');}
  setText('mem-used',sdKb);setText('mem-total',freeKnown?totalKb:'?');
  // Предупреждение: ФС не смонтирована
  const fsOk=!!d.sdOk;
  const sdDot=document.getElementById('sd-dot');
  if(sdDot) sdDot.className='dot '+(fsOk?'ok':'err');
  if(fsOk) setText('sd-val',sdKb+' KB / своб. '+(freeKnown?freeKb+' KB':'н/д'));
  else setText('sd-val','⚠ ФС не доступна');

  // Calib live
  if (document.getElementById('cf-live')) setText('cf-live',parseFloat(d.cf||0).toFixed(0));
  if (document.getElementById('ofs-live')) setText('ofs-live',d.offset||0);
  if (document.getElementById('wiz-w')) setText('wiz-w',parseFloat(d.weight||0).toFixed(2));
  // Автозаполнение поля CF текущим значением, если пользователь ещё не вводил своё
  var cfInput=document.getElementById('calib-cf');
  if(cfInput&&cfInput.value===''&&d.cf) cfInput.value=Math.round(parseFloat(d.cf||0));
}

function updHive(d) {
  if (!d) return;
  const s={Vesna:'🌸 Весна',Leto:'☀ Лето',Osen:'🍂 Осень',Zima:'❄ Зима'};
  setText('hi-season',s[d.season]||d.season||'--');
  if (d.valid) {
    setText('hi-wrange',d.wMin.toFixed(2)+' / '+d.wMax.toFixed(2)+' кг');
    if (!isNaN(d.tMin)) setText('hi-trange',d.tMin.toFixed(1)+' / '+d.tMax.toFixed(1)+' °C');
    const dk=parseFloat(d.deltaKg||0);
    const de=document.getElementById('hi-delta');
    de.textContent=(dk>=0?'+':'')+dk.toFixed(2)+' кг';
    de.style.color=dk>0?'var(--green)':dk<0?'var(--red)':'var(--text2)';
    setText('hi-count',d.count||0);
    setText('hi-days',d.daysSinceStart||0);
  }
}

function setText(id,v,html) {
  const el=document.getElementById(id);
  if(!el)return;
  if(html) el.innerHTML=v; else el.textContent=v;
}
function setGauge(gid,lid,pct,val,unit) {
  const g=document.getElementById(gid),l=document.getElementById(lid);
  if(g) g.style.width=pct.toFixed(0)+'%';
  if(l) l.textContent=val+unit;
}
function setDot(id,ok,cOk,cBad) {
  const el=document.getElementById(id);
  if(el) el.className='dot '+(ok?cOk:cBad);
}

// ── Load log ──────────────────────────────────────────────────────────
function loadLog() {
  fetch('/api/log/json').then(r=>r.json()).then(data=>{
    _all = data;
    drawMini();
    if (document.getElementById('sec-chart').classList.contains('active')) renderCharts();
  }).catch(()=>{
    drawMini();
    if(document.getElementById('sec-chart').classList.contains('active')) renderCharts();
  });
}

// ── Mini chart ────────────────────────────────────────────────────────
function drawMini() {
  const svg=document.getElementById('mini-svg');
  if (!_all||_all.length<2) {
    const wt=_curWeight>0?_curWeight.toFixed(2)+' кг':'--';
    svg.innerHTML='<text x="450" y="270" text-anchor="middle" fill="#f5a623" font-size="42" font-weight="bold">'+wt+'</text>'+
      '<text x="450" y="320" text-anchor="middle" fill="#506040" font-size="26">Лог пуст — нет данных для графика</text>';
    return;
  }
  const pts=_all.slice(-120);
  _tipPts.mini=pts;
  drawLineSvg(svg,pts,'w','#f5a623',900,600,90,15,12,90,true);
}

// ── Chart page ────────────────────────────────────────────────────────
function setPeriod(h,btn) {
  _periodH=h;
  document.querySelectorAll('.period-btn').forEach(b=>b.classList.remove('active'));
  btn.classList.add('active');
  renderCharts();
}

function toggleSeries(s,btn) {
  _serVisible[s]=!_serVisible[s];
  btn.classList.toggle('active',_serVisible[s]);
  const wrap=document.getElementById('cwrap-'+s);
  if(wrap) wrap.style.display=_serVisible[s]?'':'none';
  renderCharts();
}

function filterPts() {
  if (!_periodH) return _all;
  const cutoff=Date.now()-_periodH*3600000;
  const f=_all.filter(d=>{
    const m=d.dt&&d.dt.match(/(\d{2})\.(\d{2})\.(\d{4})\s+(\d{2}):(\d{2}):(\d{2})/);
    if(m) return new Date(+m[3],+m[2]-1,+m[1],+m[4],+m[5],+m[6]).getTime()>=cutoff;
    return true;
  });
  // Возвращаем fallback если точек меньше 2 (drawLineSvg требует ≥2 точки)
  if (f.length < 2) {
    const n=_periodH===1?60:_periodH===6?360:_periodH*60;
    return _all.slice(-Math.min(n,_all.length));
  }
  return f;
}

function renderCharts() {
  if (!_all.length) {
    [{id:'chart-w',cy:240},{id:'chart-t',cy:240},{id:'chart-b',cy:240}].forEach(function(s){
      var svg=document.getElementById(s.id);
      if(svg) svg.innerHTML='<text x="450" y="'+s.cy+'" text-anchor="middle" fill="#506040" font-size="22">Нет данных</text>';
    });
    return;
  }
  const pts=filterPts();
  if (!pts.length) return;
  _tipPts={};

  if (_serVisible.w) {
    _tipPts.w=pts;
    const ws=pts.map(d=>parseFloat(d.w)).filter(v=>!isNaN(v));
    const mn=ws.reduce((a,b)=>a<b?a:b),mx=ws.reduce((a,b)=>a>b?a:b),av=ws.reduce((a,b)=>a+b,0)/ws.length;
    setText('c-wmin',mn.toFixed(2)); setText('c-wmax',mx.toFixed(2));
    setText('c-wavg',av.toFixed(2)); setText('c-pts',pts.length);
    drawLineSvg(document.getElementById('chart-w'),pts,'w','#f5a623',900,500,90,15,12,85,true);
  }
  if (_serVisible.t) {
    _tipPts.t=pts;
    const ts=pts.map(d=>parseFloat(d.t)).filter(v=>!isNaN(v)&&v>-90);
    if(ts.length){setText('c-tmin',ts.reduce((a,b)=>a<b?a:b).toFixed(1));setText('c-tmax',ts.reduce((a,b)=>a>b?a:b).toFixed(1));}
    drawLineSvg(document.getElementById('chart-t'),pts,'t','#56ccf2',900,500,90,15,12,85,true);
  }
  if (_serVisible.b) {
    _tipPts.b=pts;
    drawLineSvg(document.getElementById('chart-b'),pts,'b','#6fcf97',900,500,90,15,12,85,true);
  }
}

// ── Universal SVG line drawing ────────────────────────────────────────
function drawLineSvg(svg,pts,key,color,W,H,L,R,T,B,showAxes) {
  const pW=W-L-R, pH=H-T-B;
  const vals=pts.map(d=>parseFloat(d[key])).filter(v=>!isNaN(v)&&v>-90);
  if (vals.length<2) { svg.innerHTML=`<text x="${W/2}" y="${H/2}" text-anchor="middle" fill="#506040" font-size="22">Нет данных</text>`; return; }
  // Для температуры и батареи: игнорируем нули при расчёте шкалы (0 = датчик не работал / USB)
  let scaleVals=(key==='t'||key==='b')?vals.filter(v=>v>0.05):vals;
  if(scaleVals.length<2) scaleVals=vals;
  let mn=scaleVals.reduce((a,b)=>a<b?a:b),mx=scaleVals.reduce((a,b)=>a>b?a:b);
  // Добавляем 5% padding сверху/снизу чтобы линия не прилипала к краям
  const range=mx-mn||1;
  mn-=range*0.05; mx+=range*0.05;
  if(mx===mn){mn-=0.5;mx+=0.5;}
  const xS=i=>L+i/(pts.length-1||1)*pW;
  const yS=v=>T+pH-(v-mn)/(mx-mn)*pH;
  let html='';
  // grid — 6 горизонтальных линий для лучшей читаемости
  const yLines=6;
  for(let k=0;k<=yLines;k++){
    const v=mn+(mx-mn)*k/yLines,y=yS(v);
    html+=`<line x1="${L}" y1="${y.toFixed(1)}" x2="${W-R}" y2="${y.toFixed(1)}" stroke="#1a201a" stroke-width="1"/>`;
    if(showAxes) {
      const dec=key==='b'?2:key==='t'?1:3;
      html+=`<text x="${L-5}" y="${(y+9).toFixed(1)}" text-anchor="end" fill="#506040" font-size="26">${v.toFixed(dec)}</text>`;
    }
  }
  // x labels — 5-7 меток равномерно
  if(showAxes && pts.length>1){
    const xCount=Math.min(7,pts.length);
    for(let n=0;n<xCount;n++){
      const i=Math.round(n*(pts.length-1)/(xCount-1));
      const x=xS(i),lbl=pts[i].dt?esc(pts[i].dt.substring(11,16)):'';
      const a=n===0?'start':n===xCount-1?'end':'middle';
      html+=`<line x1="${x.toFixed(1)}" y1="${T}" x2="${x.toFixed(1)}" y2="${T+pH}" stroke="#181d18" stroke-dasharray="3,3" stroke-width="1"/>`;
      html+=`<text x="${x.toFixed(1)}" y="${H-B+28}" text-anchor="${a}" fill="#506040" font-size="24">${lbl}</text>`;
    }
    // date labels at edges — под осью X, не наложение на время
    const d0=pts[0].dt?esc(pts[0].dt.substring(0,10)):'';
    const d1=pts[pts.length-1].dt?esc(pts[pts.length-1].dt.substring(0,10)):'';
    if(d0) html+=`<text x="${L}" y="${H-B+52}" text-anchor="start" fill="#3d5030" font-size="18">${d0}</text>`;
    if(d1&&d1!==d0) html+=`<text x="${W-R}" y="${H-B+52}" text-anchor="end" fill="#3d5030" font-size="18">${d1}</text>`;
  }
  // axes
  if(showAxes){
    html+=`<line x1="${L}" y1="${T}" x2="${L}" y2="${T+pH}" stroke="#506040" stroke-width="1.5"/>`;
    html+=`<line x1="${L}" y1="${T+pH}" x2="${W-R}" y2="${T+pH}" stroke="#506040" stroke-width="1.5"/>`;
  }
  // area + line
  let area='',line='',li=-1;
  for(let i=0;i<pts.length;i++){
    const v=parseFloat(pts[i][key]);
    if(isNaN(v)||v<=-90) continue;
    if((key==='t'||key==='b')&&Math.abs(v)<0.05&&scaleVals!==vals) continue;
    const xx=xS(i).toFixed(1),yy=yS(v).toFixed(1);
    if(li<0){area=`M ${xx} ${T+pH}`;line=`M ${xx} ${yy}`;}
    else{area+=` L ${xx} ${yy}`;line+=` L ${xx} ${yy}`;}
    li=i;
  }
  if(li>=0){
    area+=` L ${xS(li).toFixed(1)} ${T+pH} Z`;
    html+=`<path d="${area}" fill="${color}18" stroke="none"/>`;
    html+=`<path d="${line}" fill="none" stroke="${color}" stroke-width="2"/>`;
    const lv=parseFloat(pts[li][key]);
    html+=`<circle cx="${xS(li).toFixed(1)}" cy="${yS(lv).toFixed(1)}" r="4" fill="${color}"/>`;
  }
  svg.innerHTML=html;
}

// ── Tooltip + интерактивный курсор (mouse + touch) ────────────────────
let _tipHideTimer = {};
function onTip(e,s){
  const pts=_tipPts[s];
  if(!pts||!pts.length) return;
  // Поддержка touch — берём первый палец
  let cX, cY;
  if (e.touches && e.touches.length) {
    cX = e.touches[0].clientX; cY = e.touches[0].clientY;
    if (e.cancelable) e.preventDefault();  // блокируем скролл страницы при скрабе по графику
  } else {
    cX = e.clientX; cY = e.clientY;
  }
  const key={w:'w',t:'t',b:'b',mini:'w'}[s];
  const color={w:'var(--amber)',t:'var(--blue)',b:'var(--green)',mini:'var(--amber)'}[s];
  const unit={w:' кг',t:' °C',b:' В',mini:' кг'}[s];
  // viewBox геометрия: mini=900x600, charts=900x500. L,R одинаковые.
  const W=900,L=90,R=15;
  const H=(s==='mini')?600:500;
  const T=12, B=(s==='mini')?90:85;
  const pW=W-L-R, pH=H-T-B;
  const svg=e.currentTarget, rect=svg.getBoundingClientRect();
  const relX=cX-rect.left, relY=cY-rect.top;
  // Преобразуем экранные координаты в viewBox-координаты
  // SVG с preserveAspectRatio="xMidYMid meet" может иметь вертикальные отступы — учтём
  const scale = Math.min(rect.width/W, rect.height/H);
  const svgW = W*scale, svgH = H*scale;
  const padX = (rect.width-svgW)/2, padY=(rect.height-svgH)/2;
  const svgX = (relX-padX)/scale;
  // Найти ближайшую точку
  let best=-1,bestD=9999;
  for(let i=0;i<pts.length;i++){const x=L+i/(pts.length-1||1)*pW,d=Math.abs(x-svgX);if(d<bestD){bestD=d;best=i;}}
  if(best<0||bestD>pW/pts.length*3){hideTip(s);return;}
  const p=pts[best], v=parseFloat(p[key]);
  // Найти Y для точки (в viewBox)
  const vals=pts.map(d=>parseFloat(d[key])).filter(x=>!isNaN(x)&&x>-90);
  let scaleVals=(key==='t'||key==='b')?vals.filter(x=>x>0.05):vals;
  if(scaleVals.length<2) scaleVals=vals;
  let mn=scaleVals.reduce((a,b)=>a<b?a:b), mx=scaleVals.reduce((a,b)=>a>b?a:b);
  const range=mx-mn||1; mn-=range*0.05; mx+=range*0.05;
  if(mx===mn){mn-=0.5;mx+=0.5;}
  const xVB = L+best/(pts.length-1||1)*pW;
  const yVB = !isNaN(v)&&v>-90 ? T+pH-(v-mn)/(mx-mn)*pH : T+pH/2;
  // Пересчёт обратно в экран
  const screenX = padX + xVB*scale;
  const screenY = padY + yVB*scale;
  // Курсор-линия и точка — позиция считается от родителя .chart-container, а не от SVG
  const containerEl = svg.closest('.chart-container');
  const cRect = containerEl ? containerEl.getBoundingClientRect() : rect;
  const ox = rect.left - cRect.left;  // смещение SVG внутри контейнера
  const oy = rect.top - cRect.top;
  const cur=document.getElementById('cur-'+s);
  const dot=document.getElementById('dot-'+s);
  if(cur){cur.style.left=(ox+screenX)+'px';cur.style.top=oy+'px';cur.style.height=rect.height+'px';cur.style.background=color;cur.style.display='block';}
  if(dot){dot.style.left=(ox+screenX)+'px';dot.style.top=(oy+screenY)+'px';dot.style.background=color;dot.style.borderColor='#fff';dot.style.boxShadow='0 0 6px '+color;dot.style.display='block';}
  // Tooltip
  const tip=document.getElementById('tip-'+s);
  tip.innerHTML=`<b style="color:${color}">${isNaN(v)||v<=-90?'--':v.toFixed(key==='b'?3:2)+unit}</b><br>${esc(p.dt||'')}`;
  tip.style.display='block';
  let tx=relX+12, ty=relY-58;
  if(tx+150>rect.width) tx=relX-160;
  if(ty<0) ty=relY+18;
  tip.style.left=tx+'px'; tip.style.top=ty+'px';
  // Авто-скрытие после касания
  if(_tipHideTimer[s]){clearTimeout(_tipHideTimer[s]);_tipHideTimer[s]=null;}
}
function hideTip(s){
  const el=document.getElementById('tip-'+s);if(el)el.style.display='none';
  const cur=document.getElementById('cur-'+s);if(cur)cur.style.display='none';
  const dot=document.getElementById('dot-'+s);if(dot)dot.style.display='none';
}
// Touch-end — скрываем через 2 сек, чтобы успеть прочесть значение
function onTipEnd(s){
  if(_tipHideTimer[s]) clearTimeout(_tipHideTimer[s]);
  _tipHideTimer[s] = setTimeout(()=>hideTip(s), 2000);
}

// ── Export ────────────────────────────────────────────────────────────
function getExpData() {
  let data=_all;
  const from=document.getElementById('exp-from').value;
  const to=document.getElementById('exp-to').value;
  if(from||to) data=data.filter(d=>{
    if(!d.dt) return true;
    const m=d.dt.match(/(\d{2})\.(\d{2})\.(\d{4})/);
    if(!m) return true;
    const ds=`${m[3]}-${m[2]}-${m[1]}`;
    return (!from||ds>=from)&&(!to||ds<=to);
  });
  const cols=[];
  if(document.getElementById('col-dt').checked)  cols.push({k:'dt',h:'Дата/время'});
  if(document.getElementById('col-w').checked)   cols.push({k:'w',h:'Вес, кг'});
  if(document.getElementById('col-t').checked)   cols.push({k:'t',h:'Темп, °C'});
  if(document.getElementById('col-bat').checked) cols.push({k:'b',h:'Батарея, В'});
  return {data,cols};
}

function exportCsv(){
  const {data,cols}=getExpData();
  let csv='\uFEFF'+cols.map(c=>c.h).join(';')+'\n';
  data.forEach(d=>{csv+=cols.map(c=>d[c.k]||'').join(';')+'\n';});
  dlBlob(new Blob([csv],{type:'text/csv;charset=utf-8'}),'beehive_log.csv');
}

function previewExport(){
  const {data,cols}=getExpData();
  let html=`<tr>${cols.map(c=>`<th>${esc(c.h)}</th>`).join('')}</tr>`;
  data.slice(-10).forEach(d=>{html+=`<tr>${cols.map(c=>`<td>${esc(d[c.k]||'')}</td>`).join('')}</tr>`;});
  document.getElementById('prev-table').innerHTML=html;
  document.getElementById('preview-wrap').style.display='block';
}

function exportExcel(){
  if(typeof XLSX==='undefined'){
    const s=document.createElement('script');
    s.src='https://cdnjs.cloudflare.com/ajax/libs/xlsx/0.18.5/xlsx.full.min.js';
    s.onload=_doExcel; s.onerror=()=>toast('Excel недоступен (нет интернета/AP режим)',true); document.head.appendChild(s);
  } else _doExcel();
}
function _doExcel(){
  const {data,cols}=getExpData();
  const rows=data.map(d=>{const r={};cols.forEach(c=>{r[c.h]=d[c.k]||'';});return r;});
  const ws=XLSX.utils.json_to_sheet(rows);
  const vals=data.map(d=>parseFloat(d.w)).filter(v=>!isNaN(v));
  const stat=[['Параметр','Значение'],['Записей',data.length],
    ['Мин вес',vals.length?vals.reduce((a,b)=>a<b?a:b).toFixed(2):''],
    ['Макс вес',vals.length?vals.reduce((a,b)=>a>b?a:b).toFixed(2):''],
    ['Среднее', vals.length?(vals.reduce((a,b)=>a+b,0)/vals.length).toFixed(2):'']];
  const ws2=XLSX.utils.aoa_to_sheet(stat);
  const days={};
  data.forEach(d=>{const k=d.dt?d.dt.substring(0,10):'?';if(!days[k])days[k]={mn:999,mx:-999,n:0};const v=parseFloat(d.w);if(!isNaN(v)){days[k].mn=Math.min(days[k].mn,v);days[k].mx=Math.max(days[k].mx,v);days[k].n++;}});
  const ws3=XLSX.utils.aoa_to_sheet([['Дата','Мин кг','Макс кг','Точек'],...Object.entries(days).map(([k,v])=>[k,v.mn.toFixed(2),v.mx.toFixed(2),v.n])]);
  const wb=XLSX.utils.book_new();
  XLSX.utils.book_append_sheet(wb,ws,'Данные');
  XLSX.utils.book_append_sheet(wb,ws2,'Статистика');
  XLSX.utils.book_append_sheet(wb,ws3,'Дневные итоги');
  XLSX.writeFile(wb,'beehive_log.xlsx');
}

function dlBlob(blob,name){const a=document.createElement('a');const u=URL.createObjectURL(blob);a.href=u;a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(u),5000);}
function dlSdDate(){const d=document.getElementById('exp-date-sd').value;if(!d){toast('Выберите дату',true);return;}window.open('/api/log?date='+d,'_blank');}

// ── Backup ─────────────────────────────────────────────────────────────
function downloadBackup(){window.open('/api/backup','_blank');}
function viewBackup(){
  fetch('/api/backup').then(r=>r.json()).then(d=>{
    const el=document.getElementById('backup-preview');
    el.textContent=JSON.stringify(d,null,2);
    el.style.display=el.style.display==='none'?'block':'none';
  }).catch(()=>toast('Нет связи',true));
}
function restoreBackup(inp){
  const file=inp.files[0];
  if(!file){return;}
  if(!file.name.endsWith('.json')){toast('Нужен .json файл',true);inp.value='';return;}
  const reader=new FileReader();
  reader.onload=function(e){
    let json;
    try{json=JSON.parse(e.target.result);}catch(ex){toast('Ошибка: не JSON',true);inp.value='';return;}
    if(json._type!=='BeehiveScale_backup'){toast('Неверный формат бэкапа',true);inp.value='';return;}
    if(!confirm('Восстановить ВСЕ настройки из бэкапа?\n(калибровка, WiFi, Telegram, параметры)\n\nТекущие настройки будут перезаписаны!')){inp.value='';return;}
    apiFetch('/api/backup/restore',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(json)})
      .then(r=>r.json()).then(d=>{toast(d.msg||'OK',!d.ok);if(d.ok)loadConfig();}).catch(()=>toast('Нет связи',true));
  };
  reader.readAsText(file);
  inp.value='';
}

// ── Load config ───────────────────────────────────────────────────────
// silent=true — без toast (используется при переключении вкладок)
function loadConfig(silent){
  fetch('/api/config').then(r=>r.json()).then(d=>{
    const setV=(id,v)=>{const el=document.getElementById(id);if(el&&v!==undefined)el.value=v;};
    setV('cfg-alert',d.alertDelta); setV('cfg-calib',d.calibWeight);
    setV('cfg-ema',d.emaAlpha);     setV('cfg-sleep',d.sleepSec);
    setV('cfg-bl',d.lcdBlSec);
    setV('cfg-autosleep',d.autoSleepSec);
    setV('cfg-sched',(d.schedTimes&&d.schedTimes.length>0)?d.schedTimes.join(' '):'');
    // Токен НЕ заполняем в input — он замаскирован звёздочками, а placeholder покажет статус
    if(d.tgTokenSet){document.getElementById('tg-token').placeholder='Токен задан (оставьте пустым чтобы не менять)';}
    setV('tg-chatid',d.tgChatId);
    if(d.tgReportInt!==undefined) setV('tg-report-int',d.tgReportInt);
    if(d.alertDelta) setText('tg-thresh',d.alertDelta+' кг');
    selWm(parseInt(d.wifiMode||0),true);
    if(d.wifiSsid) setV('wifi-ssid',d.wifiSsid);
    if(!silent) toast('Настройки загружены');
  }).catch(()=>{if(!silent) toast('Нет связи',true);});
}

// ── Save settings ─────────────────────────────────────────────────────
function saveSettings(){
  const body={};
  const g=(id)=>document.getElementById(id);
  const a=parseFloat(g('cfg-alert').value),c=parseFloat(g('cfg-calib').value);
  const e=parseFloat(g('cfg-ema').value),s=parseInt(g('cfg-sleep').value),b=parseInt(g('cfg-bl').value);
  const aut=parseInt(g('cfg-autosleep').value);
  if(!isNaN(a)) body.alertDelta=a;
  if(!isNaN(c)) body.calibWeight=c;
  if(!isNaN(e)) body.emaAlpha=e;
  if(!isNaN(s)) body.sleepSec=s;
  if(!isNaN(b)) body.lcdBlSec=b;
  if(!isNaN(aut)) body.autoSleepSec=aut;
  const sched=(g('cfg-sched').value||'').trim();
  body.schedTimes=sched.length>0?sched.split(/\s+/).filter(t=>/^\d{1,2}:\d{2}$/.test(t)):[];
  apiFetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)})
    .then(r=>r.json()).then(d=>toast(d.msg||'OK',!d.ok)).catch(()=>toast('Нет связи',true));
}

// ── Telegram ──────────────────────────────────────────────────────────
function saveTelegram(){
  const ri=parseInt(document.getElementById('tg-report-int').value||'360');
  const body={token:document.getElementById('tg-token').value,chatId:document.getElementById('tg-chatid').value,reportInt:isNaN(ri)?360:Math.max(0,Math.min(ri,10080))};
  apiFetch('/api/tg/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)})
    .then(r=>r.json()).then(d=>toast(d.msg||'OK',!d.ok)).catch(()=>toast('Нет связи',true));
}

// ── WiFi ──────────────────────────────────────────────────────────────
function selWm(mode, noUpdate){
  _wifiMode=mode;
  document.getElementById('wopt-ap').classList.toggle('sel',mode===0);
  document.getElementById('wopt-sta').classList.toggle('sel',mode===1);
  document.querySelectorAll('input[name="wm"]').forEach((r,i)=>r.checked=i===mode);
  const sb=document.getElementById('sta-block');
  if(sb) sb.style.display=mode===1?'block':'none';
}
function saveWifi(){
  const body={wifiMode:_wifiMode};
  if(_wifiMode===1){
    const ssid=document.getElementById('wifi-ssid').value.trim();
    const pass=document.getElementById('wifi-pass').value;
    if(!ssid){toast('Введите SSID роутера',true);return;}
    body.wifiSsid=ssid;
    if(pass.length>0) body.wifiPass=pass;
  }
  apiFetch('/api/wifi/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)})
    .then(r=>r.json()).then(d=>{
      toast(d.msg||'OK',!d.ok);
      if(d.ok) setTimeout(()=>toast('Устройство перезагружается…'),2500);
    }).catch(()=>toast('Нет связи',true));
}

// ── AP Password ───────────────────────────────────────────────────────
function checkPassStrength(inId,barId){
  const v=document.getElementById(inId).value;
  const bar=document.getElementById(barId);
  const len=v.length;
  const hasUpper=/[A-Z]/.test(v), hasNum=/[0-9]/.test(v), hasSym=/[^A-Za-z0-9]/.test(v);
  const score=Math.min(4,(len>=8?1:0)+(len>=12?1:0)+(hasUpper?1:0)+(hasNum?1:0)+(hasSym?1:0));
  const colors=['','var(--red)','var(--red)','var(--amber)','var(--green)'];
  bar.style.width=(score*25)+'%';
  bar.style.background=colors[score]||'var(--border)';
}

function saveApPass(){
  const np=document.getElementById('ap-pass-new').value;
  const cp=document.getElementById('ap-pass-confirm').value;
  if(np.length<8||np.length>23){toast('Пароль: 8–23 символа',true);return;}
  if(np!==cp){toast('Пароли не совпадают',true);return;}
  const body={apPass:np};
  apiFetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)})
    .then(r=>r.json()).then(d=>{
      toast(d.msg||'OK',!d.ok);
      if(d.ok){document.getElementById('ap-pass-new').value='';document.getElementById('ap-pass-confirm').value='';}
    }).catch(()=>toast('Нет связи',true));
}

function showCredsWarning(){
  var el=document.getElementById('creds-warn');
  if(el) el.style.display='block';
}

function saveWebAuth(){
  const u=document.getElementById('web-user-new').value.trim();
  const p=document.getElementById('web-pass-new').value;
  const c=document.getElementById('web-pass-confirm').value;
  if(p.length<6||p.length>31){toast('Пароль: 6-31 символ',true);return;}
  if(p!==c){toast('Пароли не совпадают',true);return;}
  if(!confirm('Сменить логин/пароль веб-интерфейса?\nБраузер потребует переавторизацию.')) return;
  const body={pass:p};
  if(u.length>0) body.user=u;
  apiFetch('/api/auth/password',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)})
    .then(r=>r.json()).then(d=>{
      toast(d.msg||'OK',!d.ok);
      if(d.ok){
        ['web-user-new','web-pass-new','web-pass-confirm'].forEach(id=>document.getElementById(id).value='');
        setTimeout(()=>location.reload(),1500);
      }
    }).catch(()=>toast('Нет связи',true));
}

function saveOtaPass(){
  const p=document.getElementById('ota-pass-new').value;
  if(p.length<6||p.length>31){toast('OTA пароль: 6-31 символ',true);return;}
  apiFetch('/api/auth/ota',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({pass:p})})
    .then(r=>r.json()).then(d=>{
      toast(d.msg||'OK',!d.ok);
      if(d.ok) document.getElementById('ota-pass-new').value='';
    }).catch(()=>toast('Нет связи',true));
}

// ── Calibration wizard ────────────────────────────────────────────────
const WIZ=[
  {l:'Пусто',   b:'<b>Шаг 1: Уберите все грузы.</b><br>Платформа весов должна быть абсолютно пустой. Нажмите «Далее».'},
  {l:'Тара',    b:'<b>Шаг 2: Тарировка.</b><br>Нажмите «Тарировать» — показание обнулится.<br>Текущий вес должен стать близким к 0.'},
  {l:'Груз',    b:'<b>Шаг 3: Поставьте эталонный груз.</b><br>Используйте гирю с известной массой (задана в Настройках).<br>Дождитесь стабилизации показания.'},
  {l:'Cal.F.',  b:'<b>Шаг 4: Подберите Cal.Factor.</b><br>В правой панели «Ручная калибровка» введите CF и нажмите «Применить».<br>Добивайтесь совпадения «Вес (wiz)» с реальной массой груза. Поле CF автозаполнено текущим значением.'},
  {l:'Готово',  b:'<b>✓ Калибровка завершена!</b><br>Уберите груз → Тарировка → «Сохранить эталон» на главной.'},
];
function updateWiz(){
  WIZ.forEach((_,i)=>{
    document.getElementById('ws'+i).className='wiz-step'+(i<_wizStep?' done':i===_wizStep?' active':'');
  });
  document.getElementById('wiz-body').innerHTML=WIZ[_wizStep].b;
  const btn=document.getElementById('wiz-btn');
  btn.textContent=_wizStep===1?'⊘ Тарировать':_wizStep===WIZ.length-1?'↺ Заново':'Далее →';
}
function wizNext(){
  if(_wizStep===1){doApi('/api/tare').then(()=>setTimeout(fetchData,1500));_wizStep++;updateWiz();return;}
  if(_wizStep===WIZ.length-1){wizReset();return;}
  _wizStep++;updateWiz();
}
function wizReset(){_wizStep=0;updateWiz();}

function applyCalib(){
  const cf=parseFloat(document.getElementById('calib-cf').value);
  const ofs=document.getElementById('calib-ofs').value;
  const body={};
  if(!isNaN(cf)) body.calibFactor=cf;
  if(ofs!=='') body.offset=parseInt(ofs);
  if(Object.keys(body).length===0){toast('Введите значение Cal.Factor',true);return;}
  apiFetch('/api/calib/set',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)})
    .then(r=>r.json()).then(d=>{toast(d.msg||'OK',!d.ok);if(d.ok)fetchData();}).catch(()=>toast('Нет связи',true));
}

// ── API viewer ────────────────────────────────────────────────────────
function refreshApiView(){
  fetch('/api/data').then(r=>r.json()).then(d=>{
    document.getElementById('api-json').textContent=JSON.stringify(d,null,2);
  }).catch(()=>{document.getElementById('api-json').textContent='Нет связи';});
}

// ── Dates init ────────────────────────────────────────────────────────
(function initDates(){
  const now=new Date();
  const fmt=d=>`${d.getFullYear()}-${String(d.getMonth()+1).padStart(2,'0')}-${String(d.getDate()).padStart(2,'0')}`;
  const today=fmt(now), weekAgo=fmt(new Date(now-7*86400000));
  ['exp-date-sd'].forEach(id=>{const el=document.getElementById(id);if(el)el.value=today;});
  document.getElementById('exp-from').value=weekAgo;
  document.getElementById('exp-to').value=today;
})();

// ── Auto refresh ───────────────────────────────────────────────────────
function autoRefresh(){
  _start=Date.now(); tickBar(); fetchData();
  setTimeout(autoRefresh,REFRESH);
}

// ── Init ──────────────────────────────────────────────────────────────
updateWiz();
fetchData();
setTimeout(autoRefresh,REFRESH);
</script>
</body></html>

)rawhtml";

// Настройки читаются/записываются через Memory.h (web_get_*/save_web_settings)

// Маскировка секретов: XXXX***YYYY — всегда 4 первых + 4 последних символа реального src,
// середина ровно 3 звёздочки. Безопасно для произвольно длинных строк (TG-токен ~46 байт).
static String _maskSecret(const char *src) {
  if (!src) return "";
  size_t len = strlen(src);
  if (len == 0) return "";
  if (len <= 8) return "****";
  char buf[16];
  snprintf(buf, sizeof(buf), "%c%c%c%c***%c%c%c%c",
           src[0], src[1], src[2], src[3],
           src[len-4], src[len-3], src[len-2], src[len-1]);
  return String(buf);
}

// ─── Uptime в читаемом виде ───────────────────────────────────────────────
static String _uptime() {
  unsigned long s = millis() / 1000UL;
  char buf[16];
  snprintf(buf, sizeof(buf), "%lud%02lu:%02lu:%02lu",
           s/86400, (s%86400)/3600, (s%3600)/60, s%60);
  return String(buf);
}

// _buildPage() удалён — страница полностью статическая, данные через AJAX

// ─── JSON ответ ───────────────────────────────────────────────────────────
static void _sendJson(bool ok, const String &msg) {
  StaticJsonDocument<128> doc;
  doc["ok"]  = ok;
  doc["msg"] = msg;
  String out; serializeJson(doc, out);
  _srv.send(ok ? 200 : 400, "application/json", out);
}

// ─── Маршруты ─────────────────────────────────────────────────────────────
static inline void _activity() {
  lastActivityTime = millis();
  if (_wa.onActivity) _wa.onActivity();
}
static inline void _keepalive() {
  // GET-поллинг НЕ сбрасывает таймер авто-сна — иначе deep sleep никогда не сработает
}

// Отправка PROGMEM-строки чанками (без копирования всего в heap)
static void _sendProgmemChunked(const char *pgm) {
  if (!pgm) { _srv.send(500, "text/plain", "No content"); return; }
  _srv.setContentLength(CONTENT_LENGTH_UNKNOWN);
  _srv.send(200, "text/html; charset=utf-8", "");
  size_t total = strlen_P(pgm);
  size_t sent = 0;
  // 4KB чанки (было 512) — на странице ~70КБ это уменьшает кол-во TCP-пакетов
  // и yield()-ов в ~8 раз. На слабом WiFi это сокращает загрузку HTML с 30+ сек до 3-5 сек.
  static char chunk[4096];  // static чтобы не жечь стек
  while (sent < total) {
    size_t n = min((size_t)sizeof(chunk), total - sent);
    memcpy_P(chunk, pgm + sent, n);
    _srv.sendContent(chunk, n);
    yield();
    sent += n;
  }
}

static void _handleRoot() {
  if (!_auth()) return;
  _activity();
  _sendProgmemChunked(PAGE_HTML);
}

// ─── /api/config  GET — начальные значения для форм настроек ─────────────
static void _handleConfig() {
  if (!_auth()) return;
  _keepalive();  // GET-поллинг — не сбрасывать таймер авто-сна
  StaticJsonDocument<640> doc;
  doc["alertDelta"]  = web_get_alert_delta();
  doc["calibWeight"] = web_get_calib_weight();
  doc["emaAlpha"]    = web_get_ema_alpha();
  doc["sleepSec"]    = (unsigned long)get_sleep_sec();
  doc["lcdBlSec"]    = (unsigned int)get_lcd_bl_sec();
  doc["autoSleepSec"] = (unsigned int)get_autosleep_sec();
  doc["wifiMode"]    = (int)get_wifi_mode();
  // НЕ использовать block scope для char-буферов + ArduinoJson!
  // ArduinoJson v6 для char* хранит указатель (zero-copy) — dangling pointer если буфер на стеке.
  // Оборачиваем в String() чтобы ArduinoJson скопировал содержимое.
  {
    char ss[33]; get_wifi_ssid(ss, sizeof(ss));
    doc["wifiSsid"] = String(ss);
  }
  {
    char tgTok[50], tgCid[16];
    get_tg_token(tgTok, sizeof(tgTok));
    get_tg_chatid(tgCid, sizeof(tgCid));
    doc["tgToken"]  = _maskSecret(tgTok);
    doc["tgChatId"] = String(tgCid);
    doc["tgTokenSet"] = (tgTok[0] != '\0');
    doc["tgReportInt"] = get_tg_report_interval_min();
  }
  {
    uint16_t times[8]; uint8_t cnt;
    get_sched_times(times, cnt);
    JsonArray arr = doc.createNestedArray("schedTimes");
    char tbuf[6];
    for (uint8_t i = 0; i < cnt; i++) {
      snprintf(tbuf, sizeof(tbuf), "%02d:%02d", times[i] / 60, times[i] % 60);
      arr.add(String(tbuf));
    }
  }
  String out; serializeJson(doc, out);
  _srv.send(200, "application/json", out);
}

static void _handleData() {
  if (!_auth()) return;
  static unsigned long _lastDataReq = 0;
  if (!_rate_limit(_lastDataReq, 1000UL)) return;
  _keepalive();  // поллинг — не сбрасывать подсветку
  if (_csrfToken[0] == '\0') _csrf_init();
  StaticJsonDocument<512> doc;
  doc["weight"]   = *_wd.weight;
  doc["ref"]      = *_wd.lastSavedWeight;
  doc["prev"]     = *_wd.prevWeight;
  doc["lastRep"]  = _wd.lastReportWeight ? *_wd.lastReportWeight : 0.0f;
  doc["hasRep"]   = _wd.hasLastReport ? *_wd.hasLastReport : false;
  doc["prevDate"] = (uint32_t)load_prev_weight_date();  // Unix timestamp фиксации
  doc["temp"]     = *_wd.tempC;
  doc["rtcT"]     = *_wd.rtcTempC;
  doc["sensor"]   = *_wd.sensorReady;
  doc["wifi"]     = *_wd.wifiOk;
  doc["datetime"] = *_wd.datetime;
  doc["uptime"]   = _uptime();
  doc["wakeups"]  = *_wd.wakeupCount;
  doc["cf"]       = *_wd.calibFactor;
  doc["offset"]   = *_wd.offset;
  doc["batV"]     = *_wd.batVoltage;
  doc["batPct"]   = *_wd.batPercent;
  // SD-статистика: LittleFS.usedBytes()/totalBytes() итерирует все файлы → медленно.
  // Кешируем на 10 сек чтобы поллинг /api/data был быстрым (~5мс вместо 100-500мс).
  static unsigned long _sdStatsLastMs = 0;
  static unsigned long _sdLogCache = 0, _sdFreeCache = 0;
  static bool _sdFallbackCache = false, _sdOkCache = false;
  if (_sdStatsLastMs == 0 || (millis() - _sdStatsLastMs) > 10000UL) {
    _sdLogCache      = (unsigned long)log_size();
    _sdFreeCache     = (unsigned long)log_free_space();
    _sdFallbackCache = log_using_fallback();
    _sdOkCache       = log_fs_ok();
    _sdStatsLastMs   = millis();
    if (_sdStatsLastMs == 0) _sdStatsLastMs = 1;
  }
  doc["sdLog"]      = _sdLogCache;
  doc["sdFree"]     = _sdFreeCache;
  doc["sdFallback"] = _sdFallbackCache;
  doc["sdOk"]       = _sdOkCache ? 1 : 0;
  doc["csrf"]         = _csrfToken;
  doc["credsDefault"] = credentials_is_default();
  doc["fw"]           = FW_VERSION;
  // Оставшееся время web-продления (секунды). 0 = продление не активно
  if (extendSleepUntilMs != 0) {
    long left = (long)(extendSleepUntilMs - millis());
    doc["keepLeftSec"] = (left > 0) ? (left / 1000) : 0;
  } else {
    doc["keepLeftSec"] = 0;
  }
#if defined(ESP32) || defined(ESP8266)
  doc["heap"]     = ESP.getFreeHeap();
#else
  doc["heap"]     = 0;
#endif
  String out; serializeJson(doc, out);
  _srv.send(200, "application/json", out);
}

static void _handleTare() {
  if (!_auth()) return;
  if (!_csrf_check()) return;
  _activity();
  if (_wa.doTare) { _wa.doTare(); _sendJson(true, "Тарировка выполнена"); }
  else _sendJson(false, "Нет обработчика");
}

static void _handleSave() {
  if (!_auth()) return;
  if (!_csrf_check()) return;
  _activity();
  if (_wa.doSave) { _wa.doSave(); _sendJson(true, "Эталон сохранён"); }
  else _sendJson(false, "Нет обработчика");
}

// POST /api/keepalive — продлить работу на 10 минут (отодвигает auto-sleep)
static void _handleKeepAlive() {
  if (!_auth()) return;
  if (!_csrf_check()) return;
  _activity();
  extendSleepUntilMs = millis() + 10UL * 60UL * 1000UL;
  if (extendSleepUntilMs == 0) extendSleepUntilMs = 1;  // 0 = "неактивно", избегаем коллизии при rollover
  _sendJson(true, "Продлено на 10 мин");
}

// Forward declaration — используется в авто-бэкапе при сохранении настроек
// masked=true: секреты замаскированы (для GET /api/backup), false: полные (для SD-файла)
static String _buildBackupJson(bool masked = false);

static void _handleSettings() {
  if (!_auth()) return;
  if (!_csrf_check()) return;
  if (_srv.method() != HTTP_POST) { _sendJson(false,"Только POST"); return; }
  _activity();
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, _srv.arg("plain"));
  if (err) { _sendJson(false,"Ошибка JSON"); return; }

  // Валидация входных данных
  float newAlert  = web_get_alert_delta();
  float newCalib  = web_get_calib_weight();
  float newAlpha  = web_get_ema_alpha();

  if (doc.containsKey("alertDelta")) {
    float val = doc["alertDelta"].as<float>();
    if (val >= 0.1f && val <= 10.0f) {
      newAlert = val;
    } else {
      _sendJson(false, "alertDelta должен быть от 0.1 до 10.0 кг");
      return;
    }
  }

  if (doc.containsKey("calibWeight")) {
    float val = doc["calibWeight"].as<float>();
    if (val >= 100.0f && val <= 50000.0f) {
      newCalib = val;
    } else {
      _sendJson(false, "calibWeight должен быть от 100 до 50000 г");
      return;
    }
  }

  if (doc.containsKey("emaAlpha")) {
    float val = doc["emaAlpha"].as<float>();
    if (val >= 0.05f && val <= 0.9f) {
      newAlpha = val;
    } else {
      _sendJson(false, "emaAlpha должен быть от 0.05 до 0.9");
      return;
    }
  }

  // Валидация расширенных настроек ДО записи
  uint32_t newSleepSec = 0; bool hasSleepSec = false;
  uint16_t newLcdBlSec = 0;  bool hasLcdBlSec = false;
  const char* newApPass = nullptr;
  if (doc.containsKey("sleepSec")) {
    uint32_t val = doc["sleepSec"].as<uint32_t>();
    if (val >= 30UL && val <= 86400UL) { newSleepSec = val; hasSleepSec = true; }
    else { _sendJson(false, "sleepSec: 30–86400"); return; }
  }
  if (doc.containsKey("lcdBlSec")) {
    uint16_t val = doc["lcdBlSec"].as<uint16_t>();
    if (val <= 3600) { newLcdBlSec = val; hasLcdBlSec = true; }
    else { _sendJson(false, "lcdBlSec: 0–3600"); return; }
  }
  if (doc.containsKey("autoSleepSec")) {
    uint32_t val = doc["autoSleepSec"].as<uint32_t>();
    if (val <= 86400UL) { set_autosleep_sec((uint16_t)val); }
    else { _sendJson(false, "autoSleepSec: 0–86400"); return; }
  }
  char apPassBuf[24];  // локальная — безопасно при параллельных запросах (пункт 20)
  apPassBuf[0] = '\0';
  if (doc.containsKey("apPass")) {
    const char* pass = doc["apPass"].as<const char*>();
    if (pass && strlen(pass) >= 8 && strlen(pass) <= 23) {
      strncpy(apPassBuf, pass, sizeof(apPassBuf) - 1);
      apPassBuf[sizeof(apPassBuf) - 1] = '\0';
      newApPass = apPassBuf;
    }
    else { _sendJson(false, "apPass: 8–23 символа"); return; }
  }

  // Все поля валидны — сохраняем в EEPROM
  save_web_settings(newAlert, newCalib, newAlpha);
  // Batch: ext settings — один commit вместо 3 отдельных
  if (hasSleepSec || hasLcdBlSec || newApPass) {
    set_ext_all(
      hasSleepSec ? newSleepSec : get_sleep_sec(),
      hasLcdBlSec ? newLcdBlSec : get_lcd_bl_sec(),
      newApPass
    );
  }
  if (doc.containsKey("schedTimes")) {
    JsonArray arr = doc["schedTimes"].as<JsonArray>();
    uint16_t times[8]; uint8_t cnt = 0;
    for (JsonVariant v : arr) {
      const char* s = v.as<const char*>();
      if (!s) continue;
      int h = 0, m = 0;
      if (sscanf(s, "%d:%d", &h, &m) == 2 && h >= 0 && h <= 23 && m >= 0 && m <= 59) {
        if (cnt < 8) times[cnt++] = (uint16_t)h * 60 + m;
      }
    }
    set_sched_times(times, cnt);
  }

  // Авто-бэкап на SD при изменении настроек
  log_save_backup(_buildBackupJson());

  _sendJson(true, "Сохранено");
}

static void _handleReboot() {
  if (!_auth()) return;
  if (!_csrf_check()) return;
  // Отправляем JSON целиком и даём TCP время выпихнуть данные до сокета клиента.
  // client().stop() здесь ЗАПРЕЩЁН — обрубит сокет до того как клиент прочитает body.
  _sendJson(true, "Перезагрузка...");
  _srv.client().flush();
  delay(200);
  ESP.restart();
}

// Обработчик NTP синхронизации
static void _handleNtp() {
  if (!_auth()) return;
  if (!_csrf_check()) return;
  _activity();
  if (_srv.method() != HTTP_POST) { _sendJson(false,"Только POST"); return; }

  Serial.println(F("[Web] NTP sync requested..."));

  if (ntp_sync_time()) {
    _sendJson(true, "Время синхронизировано");
  } else {
    _sendJson(false, "Ошибка синхронизации");
  }
}

static void _handleChart() {
  if (!_auth()) return;
  _activity();
  _sendProgmemChunked(PAGE_HTML);
}

static void _handleWifi() {
  if (!_auth()) return;
  _activity();
  _sendProgmemChunked(PAGE_HTML);
}

// ─── /api/tg/settings  POST — сохранить Telegram токен и chat_id ─────────
static void _handleTgSettings() {
  if (!_auth()) return;
  if (!_csrf_check()) return;
  _activity();
  if (_srv.method() != HTTP_POST) { _sendJson(false,"Только POST"); return; }
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, _srv.arg("plain"));
  if (err) { _sendJson(false,"Ошибка JSON"); return; }
  // Batch: собираем token и chatId, один commit через set_tg_all()
  const char* newToken = NULL;
  const char* newChatId = NULL;
  if (doc.containsKey("token")) {
    const char* t = doc["token"].as<const char*>();
    if (t && strlen(t) > 0 && strlen(t) < 50 && strchr(t, '*') == NULL) newToken = t;
    else if (t && strlen(t) == 0) newToken = "";
  }
  if (doc.containsKey("chatId")) {
    const char* c = doc["chatId"].as<const char*>();
    if (c && strlen(c) < 16) newChatId = c;
  }
  if (newToken || newChatId) set_tg_all(newToken, newChatId);
  if (doc.containsKey("reportInt")) {
    uint32_t v = doc["reportInt"].as<uint32_t>();
    // 0 = откл, минимум 60 мин, максимум 10080 (7 дней)
    if (v == 0 || (v >= 60 && v <= 10080)) set_tg_report_interval_min(v);
  }
  log_save_backup(_buildBackupJson());
  _sendJson(true, "Telegram настройки сохранены");
}

// ─── /api/tg/test  POST — отправить тестовое/приветственное сообщение ────
static void _handleTgTest() {
  if (!_auth()) return;
  if (!_csrf_check()) return;
  _activity();

  // Справка: что бот присылает и как настроить
  char info[1024];
  uint32_t rptMin = get_tg_report_interval_min();
  float alertKg = web_get_alert_delta();
  snprintf(info, sizeof(info),
    "🐝 <b>" FW_NAME " v" FW_VERSION "</b>\n\n"
    "<b>Что присылает:</b>\n"
    "• <b>Отчёт</b> — вес, температура (по расписанию или каждые %lu мин%s)\n"
    "• <b>Тревога</b> — при изменении веса на %.1f+ кг (пауза 30 мин)\n\n"
    "<b>Веб-панель:</b> http://192.168.4.1\n"
    "• Подключение к Wi-Fi роутеру\n"
    "• Интервал отчётов, порог тревоги\n"
    "• Расписание замеров (например 09:00 21:00)\n"
    "• Графики, экспорт CSV, калибровка",
    (unsigned long)(rptMin ? rptMin : 0),
    rptMin ? "" : ", выкл",
    alertKg);

  bool ok = tg_send_message(info);
  _sendJson(ok, ok ? "Сообщение отправлено" : "Ошибка отправки (проверьте token/chat_id)");
}

// ─── /api/calib/set  POST — установить cal.factor и offset ───────────────
static void _handleCalibSet() {
  if (!_auth()) return;
  if (!_csrf_check()) return;
  _activity();
  if (_srv.method() != HTTP_POST) { _sendJson(false,"Только POST"); return; }
  StaticJsonDocument<128> doc;
  DeserializationError err = deserializeJson(doc, _srv.arg("plain"));
  if (err) { _sendJson(false,"Ошибка JSON"); return; }
  bool changed = false;
  if (doc.containsKey("calibFactor") && _wa.doSetCalibFactor) {
    float cf = doc["calibFactor"].as<float>();
    if (cf >= 100.0f && cf <= 100000.0f) {
      _wa.doSetCalibFactor(cf);
      changed = true;
    } else { _sendJson(false,"calibFactor: 100–100000"); return; }
  }
  if (doc.containsKey("offset") && _wa.doSetCalibOffset) {
    long ofs = doc["offset"].as<long>();
    _wa.doSetCalibOffset(ofs);
    changed = true;
  }
  if (changed) { log_save_backup(_buildBackupJson()); _sendJson(true, "Калибровка обновлена"); }
  else _sendJson(false, "Нет данных для обновления");
}

// ─── /api/wifi/settings  POST — сохранить режим WiFi и credentials ──────
static void _handleWifiSettings() {
  if (!_auth()) return;
  if (!_csrf_check()) return;
  _activity();
  if (_srv.method() != HTTP_POST) { _sendJson(false,"Только POST"); return; }
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, _srv.arg("plain"));
  if (err) { _sendJson(false,"Ошибка JSON"); return; }
  if (!doc.containsKey("wifiMode")) { _sendJson(false,"Нет wifiMode"); return; }
  uint8_t mode = doc["wifiMode"].as<uint8_t>();
  if (mode > 1) { _sendJson(false,"wifiMode: 0 или 1"); return; }
  // Batch: один commit через set_wifi_all() вместо 3 отдельных
  const char *ssid = NULL;
  const char *pass = NULL;
  if (mode == 1) {
    ssid = doc["wifiSsid"].as<const char*>();
    pass = doc["wifiPass"].as<const char*>();
    if (!ssid || strlen(ssid) == 0) { _sendJson(false,"Введите SSID роутера"); return; }
    if (strlen(ssid) > 32) { _sendJson(false,"SSID слишком длинный (макс 32)"); return; }
    if (pass && strlen(pass) > 32) { _sendJson(false,"Пароль слишком длинный (макс 32)"); return; }
  }
  set_wifi_all(mode, ssid, pass);
  log_save_backup(_buildBackupJson());
  _sendJson(true, "WiFi настройки сохранены, перезагрузка...");
  _srv.client().flush();
  delay(200);
  ESP.restart();
}

static void _handleNotFound() {
  _srv.send(404, "text/plain", "Not found");
}

// ─── /api/auth/password  POST — смена admin login/пароля ─────────────────
// Body: { "user": "newlogin", "pass": "newpass" }  (user опционален)
static void _handleAuthPassword() {
  if (!_auth()) return;
  if (!_csrf_check()) return;
  if (_srv.method() != HTTP_POST) { _sendJson(false, "Только POST"); return; }
  _activity();
  StaticJsonDocument<192> doc;
  DeserializationError err = deserializeJson(doc, _srv.arg("plain"));
  if (err) { _sendJson(false, "Ошибка JSON"); return; }
  const char* u = doc["user"].as<const char*>();
  const char* p = doc["pass"].as<const char*>();
  if (!p || strlen(p) < 6 || strlen(p) > 31) {
    _sendJson(false, "Пароль: 6-31 символ");
    return;
  }
  if (u && (strlen(u) < 3 || strlen(u) > 23)) {
    _sendJson(false, "Логин: 3-23 символа");
    return;
  }
  set_admin_credentials(u, p);
  _sendJson(true, "Учётные данные обновлены. Переавторизация при следующем запросе.");
}

// ─── /api/auth/ota  POST — смена OTA пароля ──────────────────────────────
static void _handleAuthOta() {
  if (!_auth()) return;
  if (!_csrf_check()) return;
  if (_srv.method() != HTTP_POST) { _sendJson(false, "Только POST"); return; }
  _activity();
  StaticJsonDocument<128> doc;
  DeserializationError err = deserializeJson(doc, _srv.arg("plain"));
  if (err) { _sendJson(false, "Ошибка JSON"); return; }
  const char* p = doc["pass"].as<const char*>();
  if (!p || strlen(p) < 6 || strlen(p) > 31) {
    _sendJson(false, "OTA пароль: 6-31 символ");
    return;
  }
  set_ota_pass(p);
  _sendJson(true, "OTA пароль сохранён. Вступит в силу после перезагрузки.");
}

// ─── /api/log  GET — скачать CSV-лог (опционально: ?date=YYYY-MM-DD) ─────
static void _handleLog() {
  if (!_auth()) return;
  static unsigned long _lastLogCsvReq = 0;
  if (!_rate_limit(_lastLogCsvReq, 1000UL)) return;
  _activity();
  if (!log_exists()) {
    _srv.send(404, "text/plain", "Log not found");
    return;
  }
  String date = _srv.arg("date");  // "" если параметр не передан
  // Санитизация: только цифры, '-' и '.', длина ≤10 (защита от HTTP header injection)
  if (date.length() > 0) {
    if (date.length() > 10) date = date.substring(0, 10);
    for (unsigned int i = 0; i < date.length(); i++) {
      char ch = date[i];
      if (!isdigit(ch) && ch != '-' && ch != '.') {
        _srv.send(400, "text/plain", "Bad date");
        return;
      }
    }
  }
  if (date.length() == 0) {
    // Без фильтра — стримим весь файл напрямую
    File f;
#ifdef USE_SD_CARD
    if (log_using_fallback()) {
      f = LittleFS.open(LOG_FILE, "r");
    } else {
      f = SD.open(LOG_FILE, FILE_READ);
    }
#else
    f = LOG_FS.open(LOG_FILE, "r");
#endif
    if (!f) { _srv.send(500, "text/plain", "Cannot open log"); return; }
    _srv.sendHeader("Content-Disposition", "attachment; filename=\"beehive_log.csv\"");
    _srv.streamFile(f, "text/csv");
    f.close();
  } else {
    // С фильтром по дате — стримим чанками (chunked transfer) для экономии heap
    String fname = "beehive_" + date + ".csv";
    _srv.sendHeader("Content-Disposition", "attachment; filename=\"" + fname + "\"");
    _srv.setContentLength(CONTENT_LENGTH_UNKNOWN);
    _srv.send(200, "text/csv; charset=utf-8", "");
    {
      class ChunkStream : public Stream {
      public:
        WebServerCompat &srv;
        char buf[256];
        uint16_t pos;
        ChunkStream(WebServerCompat &s) : srv(s), pos(0) {}
        size_t write(uint8_t c) override {
          buf[pos++] = (char)c;
          if (pos >= sizeof(buf)) _flush_buf();
          return 1;
        }
        size_t write(const uint8_t *b, size_t s) override {
          size_t sent = 0;
          while (sent < s) {
            size_t n = (s - sent > 512) ? 512 : (s - sent);
            if (pos > 0) _flush_buf();
            srv.sendContent((const char*)(b + sent), n);
            sent += n;
            yield();  // WDT safe: не блокировать loop при стриме CSV
          }
          return s;
        }
        void _flush_buf() { if (pos > 0) { srv.sendContent(buf, pos); pos = 0; } }
        int available() override { return 0; }
        int read()      override { return -1; }
        int peek()      override { return -1; }
        void flush()    override { _flush_buf(); }
      } cs(_srv);
      log_stream_csv_date(cs, date);
      cs.flush();
    }
  }
}

// ─── /api/daystat  GET — суточная статистика (фичи 12, 17) ──────────────
static void _handleDayStat() {
  if (!_auth()) return;
  _keepalive();  // GET-поллинг — не сбрасывать подсветку
  // Дата из параметра или текущая из RTC
  String date = _srv.arg("date");
  // Санитизация: только цифры, '-' и '.', длина ≤10 (защита от инъекций)
  if (date.length() > 0) {
    if (date.length() > 10) date = date.substring(0, 10);
    for (unsigned int i = 0; i < date.length(); i++) {
      char ch = date[i];
      if (!isdigit(ch) && ch != '-' && ch != '.') {
        _srv.send(400, "text/plain", "Bad date");
        return;
      }
    }
  }
  if (date.length() == 0) date = *_wd.datetime;  // "DD.MM.YYYY HH:MM:SS" → берём первые 10
  if (date.length() > 10) date = date.substring(0, 10);
  if (date.length() < 10) {
    _sendJson(false, "Дата недоступна");
    return;
  }
  // Нормализация: YYYY-MM-DD → DD.MM.YYYY (единый формат для season/days/log_day_stat)
  if (date.length() == 10 && date.charAt(4) == '-') {
    // "YYYY-MM-DD" → "DD.MM.YYYY"
    date = date.substring(8, 10) + "." + date.substring(5, 7) + "." + date.substring(0, 4);
  }

  DayStat ds = log_day_stat(date);

  StaticJsonDocument<256> doc;
  doc["date"]   = date;
  doc["valid"]  = ds.valid;
  doc["wMin"]   = ds.valid ? ds.wMin : 0;
  doc["wMax"]   = ds.valid ? ds.wMax : 0;
  doc["tMin"]   = (ds.valid && ds.tMin < 1e8f) ? ds.tMin : (float)NAN;
  doc["tMax"]   = (ds.valid && ds.tMax > -1e8f) ? ds.tMax : (float)NAN;
  doc["count"]  = ds.count;

  // Фича 17: информация об улье
  // Сезон по месяцу (date уже нормализован к DD.MM.YYYY)
  int month = 0;
  if (date.length() >= 7) month = date.substring(3, 5).toInt();
  const char* season =
    (month >= 3 && month <= 5)  ? "Vesna" :
    (month >= 6 && month <= 8)  ? "Leto"  :
    (month >= 9 && month <= 11) ? "Osen"  : "Zima";
  doc["season"] = season;

  // Дней наблюдений: разница между текущей и первой датой лога
  // Обе даты в формате DD.MM.YYYY после нормализации
  {
    char firstDate[12];
    int days = 0;
    if (log_first_date(firstDate, sizeof(firstDate)) && date.length() >= 10) {
      int d1 = atoi(firstDate);      int m1 = atoi(firstDate + 3);  int y1 = atoi(firstDate + 6);
      int d2 = atoi(date.c_str());   int m2 = atoi(date.c_str()+3); int y2 = atoi(date.c_str()+6);
      long e1 = (long)y1*365 + y1/4 - y1/100 + y1/400 + (m1*306+5)/10 + d1;
      long e2 = (long)y2*365 + y2/4 - y2/100 + y2/400 + (m2*306+5)/10 + d2;
      days = (int)(e2 - e1);
      if (days < 0) days = 0;
    }
    doc["daysSinceStart"] = days > 0 ? days : (log_size() > 100 ? 1 : 0);
  }

  // Последнее значительное изменение — дельта текущий - опорный
  doc["deltaKg"] = *_wd.weight - *_wd.prevWeight;

  String out; serializeJson(doc, out);
  _srv.send(200, "application/json", out);
}

// ─── /api/log/clear  POST — очистить лог ─────────────────────────────────
static void _handleLogClear() {
  if (!_auth()) return;
  if (!_csrf_check()) return;
  _activity();
  log_clear();
  _sendJson(true, "Лог очищен");
}

// ─── /api/log/json  GET — лог в JSON ─────────────────────────────────────
// Стримит прямо в HTTP-ответ через ChunkStream — без аккумуляции 4КБ String в heap.
static void _handleLogJson() {
  if (!_auth()) return;
  static unsigned long _lastLogReq = 0;
  if (!_rate_limit(_lastLogReq, 1000UL)) return;
  _keepalive();
  _srv.setContentLength(CONTENT_LENGTH_UNKNOWN);
  _srv.send(200, "application/json", "");
  class JsonChunkStream : public Stream {
  public:
    WebServerCompat &srv;
    char buf[128];
    uint16_t pos;
    JsonChunkStream(WebServerCompat &s) : srv(s), pos(0) {}
    size_t write(uint8_t c) override {
      buf[pos++] = (char)c;
      if (pos >= sizeof(buf)) flush_buf();
      return 1;
    }
    size_t write(const uint8_t *b, size_t s) override {
      for (size_t i = 0; i < s; i++) write(b[i]);
      return s;
    }
    void flush_buf() { if (pos > 0) { srv.sendContent(buf, pos); pos = 0; } }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override { flush_buf(); }
  } cs(_srv);
  log_stream_json(cs, 50);
  cs.flush();
}

// ─── /api/backup  GET — полный бэкап настроек EEPROM ──────────────────────
static String _buildBackupJson(bool masked) {
  DynamicJsonDocument doc(768);
  doc["_type"] = "BeehiveScale_backup";
  doc["_ver"]  = FW_VERSION;

  // Калибровка
  doc["calibFactor"]  = *_wd.calibFactor;
  doc["offset"]       = *_wd.offset;
  doc["weight"]       = *_wd.lastSavedWeight;
  doc["prevWeight"]   = *_wd.prevWeight;
  doc["prevOffset"]   = load_prev_offset();

  // Настройки
  doc["alertDelta"]   = web_get_alert_delta();
  doc["calibWeight"]  = web_get_calib_weight();
  doc["emaAlpha"]     = web_get_ema_alpha();
  doc["sleepSec"]     = (unsigned long)get_sleep_sec();
  doc["lcdBlSec"]     = (unsigned int)get_lcd_bl_sec();
  {
    uint16_t times[8]; uint8_t cnt;
    get_sched_times(times, cnt);
    JsonArray arr = doc.createNestedArray("schedTimes");
    char tbuf[6];
    for (uint8_t i = 0; i < cnt; i++) {
      snprintf(tbuf, sizeof(tbuf), "%02d:%02d", times[i] / 60, times[i] % 60);
      arr.add(String(tbuf));
    }
  }

  // AP пароль
  // НЕ использовать (const char*) каст! ArduinoJson v6 для const char* хранит указатель
  // (zero-copy), а char* — копирует строку в пул. Каст → dangling pointer после }.
  char ap[24]; get_ap_pass(ap, sizeof(ap));
  doc["apPass"] = masked ? _maskSecret(ap) : String(ap);

  // Telegram
  char tok[50], cid[16];
  get_tg_token(tok, sizeof(tok));
  get_tg_chatid(cid, sizeof(cid));
  doc["tgToken"]  = masked ? _maskSecret(tok) : String(tok);
  doc["tgChatId"] = String(cid);
  doc["tgReportInt"] = get_tg_report_interval_min();

  // WiFi
  doc["wifiMode"] = (int)get_wifi_mode();
  char ss[33], wp[33];
  get_wifi_ssid(ss, sizeof(ss));
  get_wifi_sta_pass(wp, sizeof(wp));
  doc["wifiSsid"] = String(ss);
  doc["wifiPass"] = masked ? _maskSecret(wp) : String(wp);

  String out;
  serializeJson(doc, out);
  return out;
}

static void _handleBackup() {
  if (!_auth()) return;
  _keepalive();
  // Стримим чанками: один DynamicJsonDocument пишется в WiFiClient через
  // _srv.sendContent-обёртку, без промежуточного String (экономия ~2КБ heap).
  // Неманскированный вариант сохраняем на SD отдельным вызовом — один раз в heap.
  log_save_backup(_buildBackupJson(false));

  _srv.sendHeader("Content-Disposition", "attachment; filename=\"beehive_backup.json\"");
  _srv.setContentLength(CONTENT_LENGTH_UNKNOWN);
  _srv.send(200, "application/json", "");

  String json = _buildBackupJson(true);
  const size_t CHUNK = 256;
  size_t total = json.length();
  size_t sent = 0;
  while (sent < total) {
    size_t n = (total - sent > CHUNK) ? CHUNK : (total - sent);
    _srv.sendContent(json.c_str() + sent, n);
    sent += n;
    yield();
  }
}

// ─── /api/backup/restore  POST — восстановление из JSON бэкапа ───────────
static void _handleBackupRestore() {
  if (!_auth()) return;
  if (!_csrf_check()) return;
  _activity();
  if (_srv.method() != HTTP_POST) { _sendJson(false, "Только POST"); return; }

  DynamicJsonDocument doc(768);
  DeserializationError err = deserializeJson(doc, _srv.arg("plain"));
  if (err) { _sendJson(false, "Ошибка JSON"); return; }

  // Проверяем маркер бэкапа
  const char* type = doc["_type"] | "";
  if (strcmp(type, "BeehiveScale_backup") != 0) {
    _sendJson(false, "Неверный формат бэкапа");
    return;
  }

  int restored = 0;

  // Калибровка: собираем все поля и делаем ОДИН commit через save_calibration_block()
  // вместо 5 отдельных — экономия flash wear + WDT safe.
  {
    float cf = NAN; long ofs = INT32_MIN; float w = NAN; float pw = NAN; long po = INT32_MIN;
    if (doc.containsKey("calibFactor")) {
      float v = doc["calibFactor"].as<float>();
      if (v >= 100.0f && v <= 100000.0f) { cf = v; restored++; }
    }
    if (doc.containsKey("offset")) {
      ofs = doc["offset"].as<long>();
      if (ofs > -16777216L && ofs < 16777216L) restored++; else ofs = INT32_MIN;
    }
    if (doc.containsKey("weight")) {
      float v = doc["weight"].as<float>();
      if (v >= 0.0f && v <= 500.0f) { w = v; restored++; }
    }
    if (doc.containsKey("prevWeight")) {
      float v = doc["prevWeight"].as<float>();
      if (v >= 0.0f && v <= 500.0f) { pw = v; restored++; }
    }
    if (doc.containsKey("prevOffset")) {
      po = doc["prevOffset"].as<long>();
      if (po > -16777216L && po < 16777216L) restored++; else po = INT32_MIN;
    }
    save_calibration_block(cf, ofs, w, pw, po);
    // Применяем значения к runtime
    if (!isnan(cf) && _wa.doSetCalibFactor) _wa.doSetCalibFactor(cf);
    if (ofs != INT32_MIN && _wa.doSetCalibOffset) _wa.doSetCalibOffset(ofs);
    if (!isnan(w)) *_wd.lastSavedWeight = w;
    if (!isnan(pw)) *_wd.prevWeight = pw;
#if defined(ESP8266)
    ESP.wdtFeed();
#endif
  }

  // Настройки
  float ad = web_get_alert_delta(), cw = web_get_calib_weight(), ea = web_get_ema_alpha();
  if (doc.containsKey("alertDelta"))  { float v = doc["alertDelta"].as<float>();  if (v >= 0.1f && v <= 10.0f) { ad = v; restored++; } }
  if (doc.containsKey("calibWeight")) { float v = doc["calibWeight"].as<float>(); if (v >= 100.0f && v <= 50000.0f) { cw = v; restored++; } }
  if (doc.containsKey("emaAlpha"))    { float v = doc["emaAlpha"].as<float>();    if (v >= 0.05f && v <= 0.9f) { ea = v; restored++; } }
  save_web_settings(ad, cw, ea);
#if defined(ESP8266)
  ESP.wdtFeed();
#endif

  // Ext settings — batch: один commit вместо 3
  {
    uint32_t extSleep = get_sleep_sec();
    uint16_t extLcd   = get_lcd_bl_sec();
    const char* extAp = nullptr;
    if (doc.containsKey("sleepSec")) { uint32_t v = doc["sleepSec"].as<uint32_t>(); if (v >= 30 && v <= 86400) { extSleep = v; restored++; } }
    if (doc.containsKey("lcdBlSec")) { uint16_t v = doc["lcdBlSec"].as<uint16_t>(); if (v <= 3600) { extLcd = v; restored++; } }
    static char apPassRestore[24];
    if (doc.containsKey("apPass"))   { const char* p = doc["apPass"] | ""; if (strlen(p) >= 8 && strlen(p) <= 23 && strchr(p, '*') == NULL) { strncpy(apPassRestore, p, sizeof(apPassRestore)-1); apPassRestore[sizeof(apPassRestore)-1] = '\0'; extAp = apPassRestore; restored++; } }
    set_ext_all(extSleep, extLcd, extAp);
  }
  if (doc.containsKey("schedTimes")) {
    JsonArray arr = doc["schedTimes"].as<JsonArray>();
    uint16_t times[8]; uint8_t cnt = 0;
    for (JsonVariant v : arr) {
      const char* s = v.as<const char*>();
      if (!s) continue;
      int h = 0, m = 0;
      if (sscanf(s, "%d:%d", &h, &m) == 2 && h >= 0 && h <= 23 && m >= 0 && m <= 59) {
        if (cnt < 8) times[cnt++] = (uint16_t)h * 60 + m;
      }
    }
    set_sched_times(times, cnt); restored++;
  }

  // Telegram — batch: собираем поля, один commit через set_tg_all()
  {
    const char* newTkn = nullptr;
    const char* newCid = nullptr;
    if (doc.containsKey("tgToken"))  { const char* t = doc["tgToken"] | "";  if (strlen(t) > 0 && strchr(t, '*') == NULL) { newTkn = t; restored++; } }
    if (doc.containsKey("tgChatId")) { const char* c = doc["tgChatId"] | ""; if (strlen(c) > 0 && strlen(c) < 16) { newCid = c; restored++; } }
    if (newTkn || newCid) set_tg_all(newTkn, newCid);
  }
  if (doc.containsKey("tgReportInt")) { uint32_t v = doc["tgReportInt"].as<uint32_t>(); if (v == 0 || (v >= 60 && v <= 10080)) { set_tg_report_interval_min(v); restored++; } }

  // WiFi — batch: собираем поля, один commit через set_wifi_all()
  {
    bool hasWifi = false;
    uint8_t mode = get_wifi_mode();
    char ssid[33]; get_wifi_ssid(ssid, sizeof(ssid));
    char pass[33]; get_wifi_sta_pass(pass, sizeof(pass));
    if (doc.containsKey("wifiMode")) { uint8_t m = doc["wifiMode"].as<uint8_t>(); if (m <= 1) { mode = m; hasWifi = true; restored++; } }
    if (doc.containsKey("wifiSsid")) { const char* s = doc["wifiSsid"] | ""; if (strlen(s) > 0) { strncpy(ssid, s, 32); ssid[32] = '\0'; hasWifi = true; restored++; } }
    if (doc.containsKey("wifiPass")) { const char* p = doc["wifiPass"] | ""; if (strlen(p) > 0 && strchr(p, '*') == NULL) { strncpy(pass, p, 32); pass[32] = '\0'; hasWifi = true; restored++; } }
    if (hasWifi) set_wifi_all(mode, ssid, pass);
  }

  char msg[64];
  snprintf(msg, sizeof(msg), "Восстановлено %d параметров", restored);
  _sendJson(true, msg);
}

// ─── PUBLIC API ───────────────────────────────────────────────────────────
static bool _routesBound = false;

void webserver_init(WebData &data, WebActions &actions) {
  _wd = data;
  _wa = actions;

  // Идемпотентность: при повторном вызове после WiFi reconnect не регистрируем
  // handlers второй раз — ESP8266WebServer внутри хранит их в vector и дубли
  // приводят к двойным срабатываниям и утечкам памяти.
  _srv.stop();  // фикс пункта 7: перед begin() корректно закрыть предыдущий сокет
  if (!_routesBound) {
    _srv.on("/",             HTTP_GET,  _handleRoot);
    _srv.on("/api/data",     HTTP_GET,  _handleData);
    _srv.on("/api/tare",     HTTP_POST, _handleTare);
    _srv.on("/api/save",     HTTP_POST, _handleSave);
    _srv.on("/api/keepalive",HTTP_POST, _handleKeepAlive);
    _srv.on("/api/settings",   HTTP_POST, _handleSettings);
    _srv.on("/api/ntp",        HTTP_POST, _handleNtp);
    _srv.on("/api/reboot",     HTTP_POST, _handleReboot);
    _srv.on("/api/log",          HTTP_GET,  _handleLog);
    _srv.on("/api/daystat",      HTTP_GET,  _handleDayStat);
    _srv.on("/api/log/clear",    HTTP_POST, _handleLogClear);
    _srv.on("/api/log/json",     HTTP_GET,  _handleLogJson);
    _srv.on("/chart",            HTTP_GET,  _handleChart);
    _srv.on("/api/tg/settings",  HTTP_POST, _handleTgSettings);
    _srv.on("/api/tg/test",      HTTP_POST, _handleTgTest);
    _srv.on("/api/calib/set",    HTTP_POST, _handleCalibSet);
    _srv.on("/wifi",              HTTP_GET,  _handleWifi);
    _srv.on("/api/wifi/settings", HTTP_POST, _handleWifiSettings);
    _srv.on("/api/config",        HTTP_GET,  _handleConfig);
    _srv.on("/api/backup",          HTTP_GET,  _handleBackup);
    _srv.on("/api/backup/restore",  HTTP_POST, _handleBackupRestore);
    _srv.on("/api/auth/password",   HTTP_POST, _handleAuthPassword);
    _srv.on("/api/auth/ota",        HTTP_POST, _handleAuthOta);
    _srv.onNotFound(_handleNotFound);

    // Нужно явно запросить сохранение заголовка X-CSRF-Token — иначе server.header()
    // вернёт пустую строку (WebServer по умолчанию не буферизирует headers).
#if defined(ESP32)
    static const char *_csrfHdr[] = { "X-CSRF-Token" };
    _srv.collectHeaders(_csrfHdr, 1);
#else
    _srv.collectHeaders("X-CSRF-Token");
#endif
    _routesBound = true;
  }

  _csrf_init();
  _srv.begin();
  Serial.print(F("[WebServer] Started on port "));
  Serial.print(WEB_SERVER_PORT);
  Serial.print(F("  http://"));
  Serial.println(get_wifi_mode() == 0 ? WiFi.softAPIP() : WiFi.localIP());
}

void webserver_handle() {
  _srv.handleClient();
}

void webserver_stop() {
  _srv.stop();
  Serial.println(F("[WebServer] Stopped"));
}
