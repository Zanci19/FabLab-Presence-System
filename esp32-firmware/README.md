# FabLab Presence System — ESP32-S3 Firmware

Port of the Node.js/HTML FabLab Presence System to the
**Sunton ESP32-8048S043** — an ESP32-S3 board with a 4.3″ IPS 800×480 display,
capacitive GT911 touch, 16 MB flash, and 8 MB OPI PSRAM.

The firmware replicates every screen from the original HTML project on the
physical display using **LVGL 8.3**, stores data in **LittleFS** (JSON), and
simultaneously serves the original HTML/CSS/JS frontend over WiFi so any
browser on the same network gets the full rich interface.

---

## Features

| Feature | Details |
|---|---|
| LVGL display UI | All 7 screens (Start, Intro, Clock, Greeting+Activity, Name-entry, Admin-pass, Admin panel) |
| Touch input | GT911 capacitive touch, full gesture support |
| NFC scanning | MFRC522 SPI reader; UID normalisation identical to the JS version |
| WiFi | Connects to your LAN; NTP time sync; serves HTTP on port 80 |
| Browser access | Exact HTML/CSS/JS frontend served from LittleFS at `http://<device-ip>/` |
| WebSocket push | NFC card taps on the hardware are pushed in real-time to browser clients |
| REST API | Same `/api/*` endpoints as the original Node.js server |
| Data persistence | Users and sessions stored in LittleFS as JSON (users.json / sessions.json) |
| Admin panel | Add card, delete card, view session log, export CSV |
| CSV export | Download via `GET /api/sessions/export` or auto-saved to `/export.csv` |
| CRT aesthetic | Black background, terminal font, orange/green/red colour scheme |

---

## Hardware required

| Component | Notes |
|---|---|
| Sunton ESP32-8048S043 | Any revision (C or N); 16 MB flash recommended |
| MFRC522 NFC module | Standard 3.3 V SPI breakout |
| 6 × Dupont wires | To connect MFRC522 to ESP32 header pins |
| USB-C cable | For flashing |

---

## Wiring

### MFRC522 → ESP32-S3 board header

```
MFRC522   ESP32-S3 GPIO
───────   ─────────────
3.3V    → 3V3
GND     → GND
SCK     → GPIO 12
MISO    → GPIO 13
MOSI    → GPIO 11
SDA/CS  → GPIO 10
RST     → GPIO 17  (or 3V3 via 10 kΩ pull-up to disable hard reset)
```

> **Note:** Do **not** connect MFRC522 to 5 V — it is a 3.3 V device.

The display, touch, and backlight are all on-board (no extra wiring needed).

---

## Software setup

### 1. Install PlatformIO

```bash
pip install platformio
```

or install the [PlatformIO VS Code extension](https://platformio.org/install/ide?install=vscode).

### 2. Install ESP32 Arduino platform

```bash
pio platform install espressif32
```

### 3. Set your WiFi credentials

Edit `src/config.h`:

```cpp
#define WIFI_SSID   "YourNetworkName"
#define WIFI_PASS   "YourNetworkPassword"
```

Alternatively (recommended), edit `data/settings.json` before flashing:

```json
{
  "wifiSSID": "YourNetworkName",
  "wifiPassword": "YourNetworkPassword",
  "adminPassword": "your-secret-admin-password"
}
```

### 4. Build and flash the firmware

```bash
cd esp32-firmware
pio run --target upload
```

### 5. Upload the LittleFS filesystem (HTML frontend + data files)

```bash
pio run --target uploadfs
```

> This uploads everything in the `data/` folder — HTML, JS, CSS, audio files,
> and the default JSON data files.

### 6. Open the serial monitor (optional)

```bash
pio device monitor
```

After boot you should see the device IP address printed, e.g.:
```
[WIFI] Connected — IP: 192.168.1.42
[API] HTTP server started on port 80.
[MAIN] Web UI available at http://192.168.1.42/
```

Open that URL in any browser on the same network to get the full HTML interface.

---

## First-use checklist

- [ ] Set WiFi credentials (see step 3 above)
- [ ] Change admin password (default: `admin`) in `data/settings.json`
- [ ] Flash firmware: `pio run --target upload`
- [ ] Flash filesystem: `pio run --target uploadfs`
- [ ] Power on, wait for WiFi connection, note the IP address on the serial monitor
- [ ] Register your NFC cards by tapping them on the reader

---

## Partition layout

```
nvs       4 KB    WiFi / NVS
otadata   8 KB    OTA metadata
app0      4 MB    Active firmware
app1      4 MB    OTA update slot
spiffs   ~8 MB    LittleFS (HTML, audio, JSON data)
```

---

## Clock font upgrade (optional)

LVGL's largest built-in font is **Montserrat 48 px**. On an 800×480 display the
clock digits are readable but not as large as the original HTML version.

To get a proper CRT-sized clock font (e.g. 120 px VT323):

1. Go to [https://lvgl.io/tools/fontconverter](https://lvgl.io/tools/fontconverter)
2. Upload `VT323-Regular.ttf` (from Google Fonts), size **120**, bpp **4**,
   range **0x20–0x7E** (basic ASCII)
3. Download the generated `lv_font_vt323_120.c` and place it in `src/`
4. In `src/ui.cpp` change the clock font line:
   ```cpp
   // Before:
   lv_obj_set_style_text_font(lbl_clock, &lv_font_montserrat_48, 0);
   // After:
   extern const lv_font_t lv_font_vt323_120;
   lv_obj_set_style_text_font(lbl_clock, &lv_font_vt323_120, 0);
   ```
5. Re-build: `pio run --target upload`

---

## Admin panel access

**On the physical display:** touch and hold the clock area for **5 seconds**.
A password screen will appear.

**Via browser:** the admin panel is embedded in the served HTML frontend.
Click the hidden admin button (same as the web version) and enter the password.

---

## Offline mode

If WiFi credentials are not set (or connection fails) the firmware still runs
fully offline:

- Clock uses internal RTC (time resets to epoch 0 after each power cycle — not
  a problem for local use, just set the time manually or add a DS3231 RTC module)
- All NFC, login/logout, and data storage work without network
- The HTTP server will not start (no browser access)

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| Black screen | No PSRAM / wrong board | Make sure `board_build.psram_type = opi` in platformio.ini |
| `MFRC522 not detected` | Wrong wiring / missing 3.3V | Check SPI wiring; verify 3V3 not 5V |
| `LittleFS mount failed` | First flash after partition change | Run `pio run --target uploadfs` |
| Clock shows 00:00:00 and `1970-01-01` | No NTP sync | Check WiFi credentials; device needs internet access |
| Browser shows 404 | `data/www/` not uploaded | Run `pio run --target uploadfs` |
| IntelliSense cannot find `Arduino.h`, `ESPAsyncWebServer.h`, etc. | VS Code C/C++ extension is not using PlatformIO build metadata | From `esp32-firmware/`, run `pio run -t compiledb` once, then reload VS Code (or use the committed `.vscode/` config in the repo root) |
| Touch not working | GT911 I2C address wrong | Try `TOUCH_I2C_ADDR 0x5D` in `config.h` (if INT pulled HIGH) |

---

## Project structure

```
esp32-firmware/
├── platformio.ini          PlatformIO build config
├── partitions.csv          Custom flash partition table (16 MB)
├── README.md               This file
├── src/
│   ├── lv_conf.h           LVGL 8.3 configuration
│   ├── config.h            Pin definitions, WiFi credentials, constants
│   ├── main.cpp            Arduino setup() / loop(), FreeRTOS tasks
│   ├── display.h/.cpp      RGB-LCD + GT911 touch driver
│   ├── ui.h/.cpp           All 7 LVGL screens (CRT aesthetic)
│   ├── storage.h/.cpp      LittleFS + ArduinoJson data layer
│   ├── nfc_reader.h/.cpp   MFRC522 SPI NFC reader
│   └── api_server.h/.cpp   AsyncWebServer REST API + WebSocket
└── data/
    ├── settings.json       Device settings (WiFi, admin password)
    ├── users.json          Registered NFC cards (empty at first flash)
    ├── sessions.json       Session log (empty at first flash)
    └── www/                Web frontend served over HTTP
        ├── index.html
        ├── script.js
        ├── style.css
        ├── settings.json
        └── audio/          OGG sound effects
```
