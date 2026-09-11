# Car Dashboard Remastered

Digital car dashboard for **ESP32 CYD** (Cheap Yellow Display) with **LVGL 9.x**, Bluetooth Classic (SPP) and **ELM327** OBD2 live data.

Gauges stay at `00` until an ELM327 adapter is connected. After connection, values update in near real time from the vehicle ECU.

## Features

- Splash screen with fade-in animation
- Swipe navigation between views
- **Nanomode** — lambda, coolant (water) temperature, battery voltage
- **Trackmode** — boost, fuel rail pressure, IAT, oil temperature, lambda, battery, plus session max values
- **BLE list** — scan and connect to Bluetooth Classic OBD2 adapters (ELM327)
- **Track metrics** — QR code screen; scan it with your phone to open a responsive track-day **session summary** page served by the ESP32 itself (no internet needed)
- OBD2 polling on a FreeRTOS background task (UI stays responsive)
- Session max values that only increase while connected
- Serial logs of live and max gauge values

## Hardware

| Item | Notes |
| --- | --- |
| Board | ESP32 CYD (ESP32-D0WD, 4 MB flash, 320×240 TFT) |
| Display | ILI9341 via TFT_eSPI |
| Touch | XPT2046 resistive |
| OBD2 | ELM327 Bluetooth Classic (SPP), default PIN `1234` |

Touch pins used in the sketch:

| Signal | GPIO |
| --- | --- |
| CS | 33 |
| IRQ | 36 |
| CLK | 25 |
| MISO | 39 |
| MOSI | 32 |

## Dependencies

Install via Arduino Library Manager (or your usual ESP32 Arduino setup):

- [lvgl](https://github.com/lvgl/lvgl) **9.x**
- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) (configured for your CYD)
- [XPT2046_Touchscreen](https://github.com/PaulStoffregen/XPT2046_Touchscreen)

Bluetooth uses the built-in **BluetoothSerial** library from the ESP32 Arduino core (no NimBLE). Wi-Fi, `WebServer` and `DNSServer` (track metrics page) are also from the core.

### `lv_conf.h`

The project relies on a few settings in your `lv_conf.h` (usually `Arduino/libraries/lv_conf.h`):

| Setting | Value | Why |
| --- | --- | --- |
| `LV_COLOR_DEPTH` | `16` | RGB565, native to the CYD panel and to the image assets |
| `LV_USE_STDLIB_MALLOC` | `LV_STDLIB_CLIB` | LVGL allocates from the ESP32 heap instead of a fixed 64 KB static pool. With Wi-Fi + Bluetooth the static pool no longer fits in DRAM |
| `LV_USE_QRCODE` | `1` | QR widget on the track metrics screen |
| `LV_FONT_MONTSERRAT_10..48` | `1` | Fonts used by the views |

## Arduino IDE settings (CYD)

| Option | Value |
| --- | --- |
| Board | ESP32 Dev Module |
| Partition Scheme | **Huge APP (3MB No OTA / 1MB SPIFFS)** |
| Flash Size | **4MB** |
| Flash Mode | QIO |
| Upload Speed | **115200** (921600 often fails on CH340 CYD boards) |

Sketch size is large because of UI image assets plus the Wi-Fi stack (~84% of a 3 MB app partition). The default 1.3 MB partition will not fit.

### Upload tips

1. Close Serial Monitor before uploading.
2. Use Upload Speed **115200**.
3. If connect fails: hold **BOOT**, click Upload, release BOOT when `Connecting...` appears.
4. Prefer a data-capable USB cable and a direct USB 2.0 port.

## Project structure

```
car-dashboard-remastered/
├── car-dashboard-remastered.ino   # Bootstrap: display, touch, LVGL, BT
├── preview/                       # Preview das telas no navegador (python3 preview/preview.py)
└── src/
    ├── gateway/
    │   ├── bt_gateway.*          # Bluetooth Classic scan / connect / SPP I/O
    │   ├── bt_device_data.h      # Scan results and connection state
    │   ├── elm327_gateway.*      # ELM327 init + OBD2 PID polling
    │   └── vehicle_data.h        # Shared live gauge values
    ├── net/
    │   ├── track_server.*        # Wi-Fi AP + captive portal + /data JSON
    │   └── track_page.h          # Track-day summary page (self-contained HTML)
    └── ui/
        ├── ui.c / ui.h           # UI init and splash animation
        ├── ui_navigation.*       # Swipe between screens
        ├── ui_mock_data.*        # Gauge rendering, max tracking, serial log
        ├── views/
        │   ├── ui_splash.*
        │   ├── ui_nanomode.*
        │   ├── ui_trackmode.*
        │   ├── ui_blelist.*
        │   └── ui_wifi.*         # QR code + AP credentials
        └── assets/               # Screen backgrounds and splash, RGB565 (LVGL 9)
```

## Preview no PC

Antes de gravar no CYD dá para ver as telas no navegador:

```sh
python3 preview/preview.py
```

O preview decodifica os assets de `src/ui/assets/` e lê coordenadas, fontes e
formatações direto dos `.c` das views — detalhes em [`preview/README.md`](preview/README.md).

## Screens and navigation

```
splash (~2.5 s) → nanomode ↔ trackmode ↔ blelist ↔ wifi
                    ← swipe left / right →
```

Swipe is detected from press/release delta (resistive touch friendly). Horizontal drag of about **45 px** or more changes screen.

On **blelist**, tap a device to connect. The active connection is highlighted in green.

## Track metrics (Wi-Fi + QR code)

The **wifi** screen shows a QR code and the access point credentials. The page on the phone shows the **summary of the last session**: max boost / water / oil / IAT / rail, min and max lambda and battery, duration and sample count. Cards turn yellow/red on track-relevant thresholds.

**Bluetooth and Wi-Fi never run at the same time.** Measured on the CYD (ESP32-WROOM, 320 KB RAM, no PSRAM): Bluetooth Classic takes ~100 KB, LVGL ~30 KB and the Wi-Fi AP ~60 KB — together they leave ~10 KB, which is not enough to serve a single phone. So opening the wifi screen **suspends Bluetooth** (the ELM327 session ends), starts the AP, and leaving the screen does the reverse. Session max/min live outside the screens and the BT stack, so they survive the switch: drive with live gauges on the display, then open the QR in the pits to review the session on the phone.

1. Scan the QR with the phone camera. It encodes the Wi-Fi network (`WIFI:T:WPA;S:CarDash;P:trackday1;;`), so iOS and Android offer to join it directly.
2. Once joined, the captive portal opens the page (DHCP option 114 for iOS 14+ / Android 11+, HTTP probes as fallback). Android shows a *"Sign in to network"* notification instead of opening a browser; tap it. If nothing opens, browse to `http://192.168.4.1`.
3. Swipe away from the wifi screen to restore Bluetooth.

SSID and password live in `src/net/track_server.h`. The page is self-contained (no CDN, no external fonts) because the phone has no internet while on the AP.

`GET /data` returns:

```json
{"session":true,"active":false,"session_s":1534,"ended_s_ago":412,"samples":16920,
 "max":{"boost":1.42,"rail":151.3,"iat":57,"oil":124,"lambda":1.07,"batt":14.3,"water":97},
 "min":{"lambda":0.79,"batt":12.3}}
```

Things to know:

- The phone **loses mobile data** while connected to the ESP32's network. Android may ask whether to stay connected — say yes.
- The serial monitor logs heap at each boot stage (`[HEAP] ...`) and every AP event (`[TRACK] evento: ...`) — the first thing to read if a phone cannot connect.
- `TRACK_DEBUG_NO_BT 1` at the top of the sketch boots without Bluetooth, to test the Wi-Fi side in isolation.
- The browser preview (`preview/`) does not render the wifi screen.

## OBD2 gauges

| Gauge | Views | PID | Conversion |
| --- | --- | --- | --- |
| Lambda | nano, track | `0x44` / `0x34` / `0x24` (fallback chain) | `(A×256+B) / 32768` |
| Water temp (°C) | nano | `0x05` | `A − 40` |
| Battery (V) | nano, track | `0x42` | `(A×256+B) / 1000` |
| Boost (bar) | track | `0x0B` (MAP) | `(A − 101.325) / 100` |
| Comb pressure (bar) | track | `0x23` | `(A×256+B) / 10` |
| IAT (°C) | track | `0x0F` | `A − 40` |
| Oil temp (°C) | track | `0x5C` | `A − 40` (fallback: water + 10 °C after repeated failures) |

ELM327 init sequence: `ATZ`, `ATE0`, `ATL0`, `ATS0`, `ATH0`, `ATSP0`, then `0100` to confirm ECU response.

Polling runs in a FreeRTOS task. The UI reads `vehicle.gauges` and refreshes only when `data_rev` changes.

## Serial monitor

Baud rate: **115200**.

Example output:

```
[BT] Ready (Classic SPP master)
[BT] Found: OBDII (xx:xx:xx:xx:xx:xx) -45dBm
[BT] Connected to OBDII
[ELM327] Initializing...
[ELM327] Ready
[OBD2] Session started
[OBD2] boost=0.85 max=1.20 | comb=3.50 max=6.10 | iat=42 max=55 | oil=95 max=102 | lambda=0.987 max=1.050 | batt=13.8 max=14.1 | cool=90
```

Gauge logs print about once per second while a session is active.

## Usage

1. Flash the sketch with the settings above.
2. Swipe to **blelist**.
3. Wait for devices (scan starts when the screen is created).
4. Tap your ELM327 adapter.
5. Wait for `[ELM327] Ready` on serial (a few seconds).
6. Swipe to nanomode / trackmode — gauges update from live OBD2 data.

Without a connection, all gauges show `00`. Session max/min are kept in memory after the Bluetooth session ends (for the track metrics page) and reset when the next session starts.

## Notes and limitations

- Uses **Bluetooth Classic SPP**, not BLE. Most cheap ELM327 dongles are Classic.
- Default pairing PIN is `1234` (`bt_gateway.cpp`). Change it if your adapter uses another PIN.
- One PID is requested at a time; update rate depends on the adapter and ECU.
- Oil temperature PID `0x5C` is not supported on all vehicles (e.g. some VAG ECUs).
- UI assets are large; keep **Huge APP** partition selected.
- The web page is read-only and shows the last session only; nothing is stored across reboots.

## License

Add a license of your choice if you publish this repository.
