# Car Dashboard Remastered

Digital car dashboard for **ESP32 CYD** (Cheap Yellow Display) with **LVGL 9.x**, Bluetooth Classic (SPP) and **ELM327** OBD2 live data.

Gauges stay at `00` until an ELM327 adapter is connected. After connection, values update in near real time from the vehicle ECU.

## Features

- Splash screen with fade-in animation
- Swipe navigation between views
- **Nanomode** — lambda, coolant (water) temperature, battery voltage
- **Trackmode** — boost, fuel rail pressure, IAT, oil temperature, lambda, battery, plus session max values
- **BLE list** — scan and connect to Bluetooth Classic OBD2 adapters (ELM327)
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

Bluetooth uses the built-in **BluetoothSerial** library from the ESP32 Arduino core (no NimBLE).

## Arduino IDE settings (CYD)

| Option | Value |
| --- | --- |
| Board | ESP32 Dev Module |
| Partition Scheme | **Huge APP (3MB No OTA / 1MB SPIFFS)** |
| Flash Size | **4MB** |
| Flash Mode | QIO |
| Upload Speed | **115200** (921600 often fails on CH340 CYD boards) |

Sketch size is large because of UI image assets (~81% of a 3 MB app partition). The default 1.3 MB partition will not fit.

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
    └── ui/
        ├── ui.c / ui.h           # UI init and splash animation
        ├── ui_navigation.*       # Swipe between screens
        ├── ui_mock_data.*        # Gauge rendering, max tracking, serial log
        ├── views/
        │   ├── ui_splash.*
        │   ├── ui_nanomode.*
        │   ├── ui_trackmode.*
        │   └── ui_blelist.*
        └── assets/               # SquareLine-exported images (LVGL 9)
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
splash (~2.5 s) → nanomode ↔ trackmode ↔ blelist
                    ← swipe left / right →
```

Swipe is detected from press/release delta (resistive touch friendly). Horizontal drag of about **45 px** or more changes screen.

On **blelist**, tap a device to connect. The active connection is highlighted in green.

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

Without a connection, all gauges show `00`. Max values reset when the Bluetooth session ends.

## Notes and limitations

- Uses **Bluetooth Classic SPP**, not BLE. Most cheap ELM327 dongles are Classic.
- Default pairing PIN is `1234` (`bt_gateway.cpp`). Change it if your adapter uses another PIN.
- One PID is requested at a time; update rate depends on the adapter and ECU.
- Oil temperature PID `0x5C` is not supported on all vehicles (e.g. some VAG ECUs).
- UI assets are large; keep **Huge APP** partition selected.

## License

Add a license of your choice if you publish this repository.
