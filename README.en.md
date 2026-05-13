<div align="center">

<img src="docs/assets/logo.png" alt="BeehiveScale ESP32" width="200" height="200"/>

# BeehiveScale ESP32

**Smart bee hive scale with ESP32 — autonomous hive monitoring**

🌍 **Language:** [🇷🇺 Русский](README.md) · **🇬🇧 English**

![Version](https://img.shields.io/badge/version-v5.0.19-f5a623?style=flat-square)
![Platform](https://img.shields.io/badge/platform-ESP32--WROOM--32-blue?style=flat-square)
![Framework](https://img.shields.io/badge/framework-Arduino-00979d?style=flat-square)
![License](https://img.shields.io/badge/license-MIT-green?style=flat-square)

Weight · Temperature · Wi-Fi · Telegram · Charts · 26+ days autonomy

[📖 User Manual](manual.html) · [🔌 Wiring](pages/02_connection.html) · [📱 Telegram](pages/06_telegram.html) · [📋 CHANGELOG](CHANGELOG.md)

</div>

---

## 🐝 About

BeehiveScale is an open-source beekeeping scale project for monitoring hive condition without lifting it by hand. The device continuously measures weight, temperature, exposes data through built-in website and sends Telegram reports on schedule. Runs autonomously on 18650/21700 cell for several weeks.

**Who it's for:** beekeepers of any level, from hobbyists with single hive to apiaries of 100+ hives. Simple DIY project, assembled on breadboard in one evening. Complete instructions, wiring diagrams, ready-to-flash firmware.

**Historical note:** project started in 2024 on ESP8266 NodeMCU (v1.x–v4.x). In May 2026 ported to ESP32-WROOM-32 (v5.0.0+) — due to inability to implement stable deep sleep on ESP8266 in our circuit. ESP32 provides precise scheduling, powerful Wi-Fi, Bluetooth (for future features), and 3× more Flash for code.

---

## ✨ Features

- ⚖ **Precise weight measurement** — HX711 with EMA smoothing, spike filter, auto-fixation of stable readings. Accuracy ±10 g after calibration.
- 🌡 **Temperature** — DS18B20 (hive) + DS3231 RTC (air in case). Waterproof probe in hive.
- 🕐 **Precise time** — DS3231 RTC with CR2032 for years. Doesn't drift when powered off.
- 🌐 **Web panel** — built-in website with dark theme, charts, mobile responsive layout. Wi-Fi AP or home router connection (STA).
- 📅 **Archive with presets** — daily history for selected period (day/week/month/all/since last visit), anomaly markers, CSV download.
- 📲 **PWA** — install website as app on home screen Android/iOS, offline cache.
- 📱 **Telegram reports** — on schedule (e.g. 09:00 + 21:00). Works with any ISP thanks to Cloudflare Worker relay (bypasses `api.telegram.org` blocking).
- 💤 **Deep Sleep + scheduling** — ESP32 wakes at set time, measures, sends report, sleeps again. **20-44 days autonomy** on 21700.
- 🔋 **Battery monitoring** — voltage divider R1+R2 100k on GPIO34 ADC. Charge percent and low-battery alert at <10%.
- 📟 **8 LCD screens** — weight, delta Δ, battery, temperature, date/time, system status, CF menu, diagnostics.
- 🔘 **Button control** — MAIN (tare/calibration), MENU (navigation). Protection against accidental press in hive.
- 🩺 **Self-test** — diagnostic screen tests HX711, DS18B20, RTC, battery.

---

## 📷 Screenshots

<details>
<summary>Main page</summary>

Main: current weight, deltas (from fixed point + since last measurement), temperature, battery, system status, mini-chart, "Hive info" block (season, min/max weight/temp today, points today, observation days).

</details>

<details>
<summary>Archive tab</summary>

Archive: period presets (today / 3 days / week / month / all / **"Since last visit"** ⭐), days feed with anomaly markers (🔴 on weight drop greater than alert_delta), period summary, CSV download.

</details>

<details>
<summary>Telegram report</summary>

```
📊 Report: hive
🕐 Time: 08.05.2026 21:00:10
⚖️ Weight: 4.42 kg
🎯 Reference: 4.08 kg (fixed 05.05, 3 days ago)
🎯 From reference point: +0.34 kg
📈 Since last measurement: +0.02 kg
🌡 Temperature: 27.5 °C
🔋 Battery: 3.85 V (78 %)
```

</details>

---

## 🧩 Components (Bill of Materials)

| Item | Model | Quantity | Approx. price |
|---|---|---|---|
| Microcontroller | ESP32-WROOM-32 DevKit V1 (DOIT, USB-C) | 1 | ~$6 |
| Load cell ADC | HX711 | 1 | ~$1 |
| Load cell | Beam-type TAL220 (50 kg) | 1–4 | ~$3 each |
| Display | LCD 1602 + I²C backpack (PCF8574, 0x27) | 1 | ~$3 |
| Thermometer | DS18B20 (waterproof probe 1m) | 1 | ~$2 |
| RTC | DS3231 + CR2032 | 1 | ~$2 |
| Buttons | Tactile 6×6 mm | 2 | ~$0.5 |
| Power | TP4056 + MT3608 + 18650/21700 cell | 1 set | ~$5 |
| Resistors | 100 kΩ ×2 (divider), 4.7 kΩ (DS18B20 pullup), 10 kΩ (HX711 SCK pullup) | 4 | pennies |
| Enclosure | IP65 plastic ~85×65×30 mm | 1 | ~$4 |

**Total ~$25** per scale.

---

## 📌 ESP32 Pinout

```
ESP32-WROOM-32 DevKit V1
├─ D16 ─ HX711 DT          ┐
├─ D17 ─ HX711 SCK         │ + 10kΩ pullup → 3V3 ⚡ MANDATORY for sleep
├─ D21 ─ I²C SDA           ┐
├─ D22 ─ I²C SCL           ┘ LCD 0x27 + DS3231 0x68
├─ D33 ─ DS3231 SQW          ↳ opt. reserved for P3 (alarm wake)
├─ D4  ─ DS18B20 DATA       + 4.7kΩ pullup → 3V3
├─ D27 ─ MAIN button         RTC GPIO (wake-from-sleep)
├─ D26 ─ MENU button
├─ D34 ─ Battery divider    ADC1_CH6 (input only)
├─ VIN ─ 5V from MT3608/AS21
└─ 3V3 ─ HX711, DS3231, DS18B20
```

Full table: [`hardware/pinout.md`](hardware/pinout.md). SVG schematic: [`pages/02_connection.html`](pages/02_connection.html).

---

## 🚀 Firmware Installation

### Arduino IDE

```
Tools → Board → ESP32 Arduino → DOIT ESP32 DEVKIT V1
Tools → Partition Scheme → Huge APP (3MB No OTA / 1MB SPIFFS)  ⚠ MANDATORY!
Tools → Upload Speed → 921600
```

Libraries (via Library Manager):
- `arduino-esp32` 2.0.x / 3.0.x
- `LiquidCrystal_I2C` 1.1.2+
- `HX711` by bogde 0.7.5+
- `OneWire` 2.3+, `DallasTemperature` 3.9+
- `RTClib` by Adafruit 2.1+
- `ArduinoJson` **6.x** (not 7.x!)

Open → `BeehiveScale/BeehiveScale.ino` → Upload.

### PlatformIO

```bash
pio run -e esp32dev -t upload
```

`platformio.ini` pre-configured (huge_app partition, esp32dev environment).

---

## 🌐 Usage

1. **Connect power** through TP4056 (charge) + MT3608 (Boost 5V) → ESP32 VIN.
2. **LCD splash** "Vesy Pchelovod v5.0.19" appears.
3. **On phone** find Wi-Fi network `BeehiveScale`, password `12345678`.
4. **Open in browser** `http://192.168.4.1`. Login `admin` / `beehive`.
5. **Calibrate** via web → Calibration → place known weight → enter value → Save.
6. **Configure schedule** in Settings (e.g. 09:00 and 21:00).
7. **Enable Telegram** — configure Token (from @BotFather) and Chat ID (from @userinfobot).
8. **Install to home screen** — site is a PWA, bee icon appears among apps.
9. **Place in hive** — single 21700 charge lasts 20-26 days (with MT3608) or 40+ days (with HT7333 LDO).

Detailed: [📖 user manual](manual.html).

---

## ⚡ Power Consumption

| Configuration | Sleep | Autonomy 21700 (4500 mAh) |
|---|---|---|
| Baseline (AMS1117 + AS21) | ~10 mA | ~13 days |
| + HT7333 LDO + LEDs desoldered | ~7 mA | ~19 days |
| + 10kΩ pullup on HX711 SCK | ~5 mA | ~26 days |
| + MT3608 instead of AS21 | ~4 mA | ~33 days |
| + P-MOSFET for LCD off in sleep | ~2 mA | ~65 days 🎯 |

**Measurements verified on real hardware.** Pull-up on HX711 SCK gives +12 days autonomy from a single resistor for pennies.

---

## 🛠 Tech Stack

**Hardware:**
- ESP32-WROOM-32 (Tensilica Xtensa LX6 dual-core 240 MHz, Wi-Fi 802.11 b/g/n, Bluetooth 4.2 BLE, 4 MB Flash, 520 KB SRAM)
- HX711 (24-bit ADC for load cells, gain 128)
- LCD HD44780 16×2 via PCF8574 I²C-extender
- DS3231 (TCXO RTC, ±2 min/year)
- DS18B20 (1-Wire digital temperature, 12-bit)

**Firmware:**
- Arduino Framework on ESP32 core 2.0/3.0
- ArduinoJson 6.x
- bogde/HX711, DallasTemperature, RTClib, LiquidCrystal_I2C

**Cloud:**
- Cloudflare Workers (Telegram relay) — `beehive-relay.darkvolg.workers.dev`
- Bypass for blocked `api.telegram.org` in some Russian ISPs

**Storage:**
- LittleFS (internal ESP32 flash) for CSV measurement log
- EEPROM-emulation for calibration, passwords, schedule, reference weight

---

## 📚 Documentation

| Document | Description |
|---|---|
| [📖 manual.html](manual.html) | Full user manual — 11 tabs (components, pinout, firmware, web, schedule, Telegram, FAQ) |
| [🔧 pages/01_components.html](pages/01_components.html) | Detailed components list |
| [🔌 pages/02_connection.html](pages/02_connection.html) | Wiring diagram, SVG schematics |
| [⚙ pages/03_settings.html](pages/03_settings.html) | Firmware settings, EEPROM map |
| [🖥 pages/04_operation.html](pages/04_operation.html) | Button control, 8 LCD screens |
| [🌐 pages/05_web.html](pages/05_web.html) | Web interface, API endpoints, PWA |
| [📱 pages/06_telegram.html](pages/06_telegram.html) | Telegram setup via CF Worker |
| [🩺 pages/07_troubleshooting.html](pages/07_troubleshooting.html) | Diagnostics and FAQ |
| [📋 hardware/pinout.md](hardware/pinout.md) | Full pinout with wire colors |
| [📋 CHANGELOG.md](CHANGELOG.md) | Version history |
| [🎨 hardware/pcb_layout_v1.excalidraw](hardware/pcb_layout_v1.excalidraw) | PCB schematic (Excalidraw) |

> Note: documentation HTML files are primarily in Russian. English translation is on the roadmap. The README, code comments, and CHANGELOG are bilingual where possible.

---

## 🔮 Roadmap

- [x] Port from ESP8266 to ESP32 (v5.0.0)
- [x] Cloudflare Worker for Telegram (v5.0.0)
- [x] Web charts with touch cursor (v5.0.13)
- [x] Archive tab + PWA (v5.0.16)
- [x] "Since last visit" preset (v5.0.19)
- [ ] **Phase P3:** DS3231 alarm wake instead of timer wakeup (±2 min/year accuracy)
- [ ] **Phase P4:** PCB v2, IP65 enclosure, P-MOSFET for LCD off
- [ ] Solar panel 5W 6V + MPPT (CN3791)
- [ ] HTTPS on website (mTLS)
- [ ] GSM module for apiaries without Wi-Fi
- [ ] Charts in archive (currently summary only)
- [ ] English documentation (HTML pages translation)

---

## 📝 License

MIT — do whatever you want, just keep the link to the original. See [LICENSE](LICENSE).

---

## 🤝 Contributors

- **Gennady Yakubovsky** (@darkvolg) — project author, hardware, real apiary tests
- **Claude (Anthropic)** — co-author, ESP32 firmware port, web panel, documentation

Found this project helpful? Star ⭐ on GitHub. Found a bug or want to add a feature — issues and PRs welcome.

---

<div align="center">

🐝 **Made for beekeepers who want to know more about their bees**

[GitHub](https://github.com/darkvolg/BeehiveScale-ESP32) · [Issues](https://github.com/darkvolg/BeehiveScale-ESP32/issues)

</div>
