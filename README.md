# Car Dashboard Remastered

Digital car dashboard for **ESP32 CYD** (Cheap Yellow Display) with **LVGL 9.x**, Bluetooth Classic (SPP) and **ELM327** OBD2 live data.

Gauges stay at `00` until an ELM327 adapter is connected. After connection, values update in near real time from the vehicle ECU.

## Features

- Splash screen with fade-in animation (currently **out of the build**, see *Splash*)
- Swipe navigation between views, sliding in 400 ms with ease-in-out, pushed to the display by DMA
- **Nanomode** — lambda, coolant (water) temperature, battery voltage
- **Trackmode** — boost, fuel rail pressure, IAT, oil temperature, lambda, battery, plus session max values (lambda has no max block: its peak is not a useful figure)
- **BLE list** — scan and connect to Bluetooth Classic OBD2 adapters (ELM327)
- **DTC** — read, list and clear diagnostic trouble codes through the ELM327, with a structural description of each code
- **Phone access** (currently **out of the build**, `UI_ENABLE_WEB 0`) — a second board, a **Wemos D1 mini**, runs the Wi-Fi access point and serves a web page with the gauge summary, a chart of the session history and DTC clearing; the CYD feeds it over a 3-wire serial link and shows the QR code (see *Phone access*)
- **Status bar** — Bluetooth icon while the ELM327 is connected
- OBD2 polling on a FreeRTOS background task (UI stays responsive), with a per-metric cadence so boost and fuel rail are not held up by coolant and oil
- Gauge readings eased between samples, so the panel reads continuously instead of stepping
- Session max values that only increase while connected
- Serial logs of live and max gauge values

## Hardware

| Item | Notes |
| --- | --- |
| Board | ESP32 CYD (ESP32-D0WD, 4 MB flash, 320×240 TFT) |
| Display | ILI9341 via TFT_eSPI |
| Touch | XPT2046 resistive |
| OBD2 | ELM327 Bluetooth Classic (SPP), default PIN `1234` |
| Phone portal (optional) | Wemos D1 mini (ESP8266), wired to the CYD's CN1 connector — see *Phone access* |
| Battery (optional) | 1S Li-ion 3.7 V (target: JYH LIP103450, 1800 mAh) + TP4056 USB-C charger, sensed on GPIO 35 through a divider |

Touch pins used in the sketch:

| Signal | GPIO |
| --- | --- |
| CS | 33 |
| IRQ | 36 |
| CLK | 25 |
| MISO | 39 |
| MOSI | 32 |

### Battery

The CYD has no battery sensing of its own. `src/power/battery.*` reads the cell through a voltage divider on **GPIO 35** (ADC1_CH7, input-only). The percentage is **not shown on screen** for now — the status bar carries only the Bluetooth icon — but the module keeps sampling and logging on the serial monitor, ready to be displayed again once the divider is mounted:

```
B+ (cell) ──[100 kΩ 1%]──┬──[100 kΩ 1%]── GND
                         │
                      GPIO 35  ──[100 nF]── GND        (GND common with the CYD)
```

**Without the divider there is nothing to read.** GPIO 35 floats (GPIOs 34–39 have no internal pulls) and reads noise near 0 V. The firmware detects that — `battery_valid()` returns false instead of reporting a fake `0%` — and the serial monitor says so once: `[BAT] sensor ausente: monte o divisor 100k/100k no GPIO 35`. Mount the divider and the reading starts working on its own; nothing to change in code. Presence uses hysteresis far from the noise floor: a Li-ion cell never puts less than ~1.25 V on the pin (2.5 V protection cutoff ÷ 2), so the sensor appears above 1000 mV and disappears below 600 mV.

The divider halves the cell voltage (4.2 V → 2.1 V), inside the ADC's linear range at 11 dB attenuation, and draws ~21 µA. `src/power/battery.*` averages 16 calibrated readings (`analogReadMilliVolts`) once a second, filters them, and maps voltage to charge with a Li-ion discharge curve rather than a straight line — a 3.0–4.2 V line would report ~60 % when ~20 % is left. A `[BAT] pino … -> celula … -> …%` line every 10 s helps calibrating: compare with a multimeter and set `BATTERY_CAL` in `battery.h` (voltage measured ÷ voltage read) to absorb resistor tolerance.

**Power path matters.** The TP4056's B+ is the *cell* voltage (3.0–4.2 V). The CYD makes its 3.3 V with an AMS1117 fed from the 5 V rail, which needs ≥ 4.4 V at its input — a bare cell on USB-C/VIN will brown out. Put a 5 V **boost** between the cell and the CYD (MT3608 set to 5.0–5.2 V *before* connecting it, or a TP4056 module with a built-in boost output). With the boost, the 5 V rail stays fixed regardless of charge, which is why the cell has to be measured directly. The TP4056's `CHRG`/`STDBY` pins are not wired, so the bar cannot tell "charging" from "full".

## Dependencies

Install via Arduino Library Manager (or your usual ESP32 Arduino setup):

- [lvgl](https://github.com/lvgl/lvgl) **9.x**
- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) (configured for your CYD)
- [XPT2046_Touchscreen](https://github.com/PaulStoffregen/XPT2046_Touchscreen)

Bluetooth uses the built-in **BluetoothSerial** library from the ESP32 Arduino core (no NimBLE).

The Wemos sketch (`wemos-portal/`) needs only the **ESP8266 Arduino core** (`esp8266:esp8266`,
board *LOLIN(WEMOS) D1 R2 & mini*); `ESP8266WiFi`, `ESP8266WebServer` and `WiFiUdp` come with it.

Bluetooth is started with `SerialBT.begin("CarDash", true, true)` — the third argument is
`disableBLE`. With it the core brings the controller up in `ESP_BT_MODE_CLASSIC_BT` and calls
`btMemRelease(BT_MODE_BLE)`, which returns both the controller and the host BLE memory to the heap.
This project only ever speaks Classic SPP (`esp_bt_gap_start_discovery` inquiry, Classic dongles).

### Why the Wi-Fi lives on a second board

The first version ran the web portal on the CYD itself. The CYD has no PSRAM, so Bluedroid, LVGL and
the Wi-Fi driver share ~320 KB of DRAM, and Wi-Fi and Bluetooth Classic share one 2.4 GHz radio.
Measured on the board, step by step:

1. The core's `WiFi.mode()` failed inside `esp_wifi_init()` with ~37 KB free (`Expected to init 4 rx
   buffer, actual is 0`), and every failed attempt leaked ~17 KB.
2. Bringing Wi-Fi up through ESP-IDF with a minimal config (no `arduino_events` / `async_udp` tasks,
   full teardown on failure, history in RTC memory) got it up — with **5.8 KB** left, too little for a
   phone to associate.
3. Screens created on demand, smaller task stacks and draw buffer brought that to **24.5 KB**: phones
   connected, but the link stayed intermittent with the ELM327 polling on the same radio.

So the radio moved to a chip that has nothing else to do. The ESP8266 on the Wemos has no Bluetooth
at all; the CYD keeps Bluetooth, gauges and polling and sends the data over a wire.

With the Wi-Fi gone from the CYD, most of the memory squeezing was undone in favour of smooth
transitions (see *Screens and navigation*): the screens are created at boot again, the backgrounds
are RGB565 again, and the display gets two 16-line DMA buffers. What stays is what costs nothing:
Classic-only Bluetooth and 4 KB stacks for the polling and connect tasks (both print their minimum
free stack on serial). With the web page out of the build: static DRAM 46.2 KB, flash 66 %.

`setup()` prints free heap and largest block at boot, after `bt_init`, after `ui_init` and at the
end, and the Bluetooth gateway prints it the moment SPP connects.

## Arduino IDE settings (CYD)

| Option | Value |
| --- | --- |
| Board | ESP32 Dev Module |
| Partition Scheme | **Huge APP (3MB No OTA / 1MB SPIFFS)** |
| Flash Size | **4MB** |
| Flash Mode | QIO |
| Upload Speed | **115200** (921600 often fails on CH340 CYD boards) |

Sketch size is large because of UI image assets (~66% of a 3 MB app partition). The default 1.3 MB partition will not fit.

### Upload tips

1. Close Serial Monitor before uploading.
2. Use Upload Speed **115200**.
3. If connect fails: hold **BOOT**, click Upload, release BOOT when `Connecting...` appears.
4. Prefer a data-capable USB cable and a direct USB 2.0 port.

## Project structure

```
car-dashboard-remastered/
├── car-dashboard-remastered.ino   # Bootstrap: display, touch, LVGL, BT
├── wemos-portal/                  # Second sketch, for the Wemos D1 mini (ESP8266)
│   ├── wemos-portal.ino           # AP, captive DNS, web server, serial link to the CYD
│   └── src/                       # web_page.h, history.*, captive_dns.*, link_proto.h
├── preview/                       # Preview das telas no navegador (python3 preview/preview.py)
└── src/
    ├── power/
    │   └── battery.*             # Li-ion sense on GPIO 35: ADC, presence, % curve
    ├── gateway/
    │   ├── bt_gateway.*          # Bluetooth Classic scan / connect / SPP I/O
    │   ├── bt_device_data.h      # Scan results and connection state
    │   ├── elm327_gateway.*      # ELM327 init, OBD2 PID polling, DTC read/clear
    │   ├── dtc_data.h            # DTC snapshot and operation state
    │   ├── dtc_describe.*        # Structural description of a DTC (SAE J2012)
    │   ├── vehicle_profile.*     # Per-car PIDs, poll cadence, boost maths
    │   └── vehicle_data.h        # Shared live gauge values
    ├── net/
    │   ├── link.*                # Serial link to the Wemos: sends state, takes DTC requests
    │   └── link_proto.h          # Wire protocol (identical copy in wemos-portal/src/)
    └── ui/
        ├── ui.c / ui.h           # UI init, UI_ENABLE_SPLASH, splash animation
        ├── ui_navigation.*       # Swipe between screens, ease-in-out transitions
        ├── ui_mock_data.*        # Gauge rendering, smoothing, max tracking, serial log
        ├── ui_statusbar.*        # Bluetooth icon on lv_layer_top()
        ├── views/
        │   ├── ui_splash.*       # out of the build while UI_ENABLE_SPLASH is 0
        │   ├── ui_nanomode.*
        │   ├── ui_trackmode.*
        │   ├── ui_blelist.*
        │   ├── ui_dtc.*          # read / list / clear trouble codes
        │   └── ui_qr.*           # QR code with the Wemos network (out of the build: UI_ENABLE_WEB 0)
        └── assets/               # Backgrounds and splash, RGB565 (LVGL 9)
```

## Preview no PC

Antes de gravar no CYD dá para ver as telas no navegador:

```sh
python3 preview/preview.py
```

O preview decodifica os assets de `src/ui/assets/` e lê coordenadas, fontes e
formatações direto dos `.c` das views — detalhes em [`preview/README.md`](preview/README.md).
Ele respeita o `UI_ENABLE_SPLASH`, desliza entre as telas com a mesma curva do
firmware, e na tela DTC encena a leitura e o apagamento com dados de exemplo.

O parser de DTC, o polling multi-PID, o perfil de veículo e o histórico da sessão têm teste no PC,
sem hardware:

```sh
cd preview && make -f test_dtc.mk
cd preview && make -f test_poll.mk         # respostas multi-PID do ELM327 e a escada de modos
cd preview && make -f test_profile.mk
cd preview && make -f test_history.mk
cd preview && make -f test_dns.mk          # ./dist/test_dns serve 5399  +  dig @127.0.0.1 -p 5399 x.y
cd preview && make -f test_link.mk         # protocolo, e as duas copias de link_proto.h iguais
cd preview && make -f test_wire.mk         # src/net/link.cpp real contra uma serial falsa
```

## Screens and navigation

```
nanomode ↔ trackmode ↔ etanol ↔ blelist ↔ dtc ↔ (qr) ↔ config
          ← swipe left / right →
```

`qr` only exists with `UI_ENABLE_WEB 1` (off while the Wemos board is out).

- **etanol** — ethanol percentage from PID `0x52`, with a 0–100 bar and the fuel class (GASOLINA / MISTURA / ETANOL).
- **config** — backlight brightness (PWM on `TFT_BL`, 5–100 %) and a switch that turns panel and touch 180° together, for a CYD mounted upside down. Both apply immediately and are kept in NVS (`src/display/display.cpp`); brightness is written on slider release, not on every step. Dragging a slider never counts as a screen swipe.

Swipe is detected from press/release delta (resistive touch friendly). Horizontal drag of about **45 px** or more changes screen. Screens slide in **400 ms with ease-in-out** (cubic-bezier 0.42, 0, 0.58, 1): `lv_screen_load_anim` only animates linearly and has no easing parameter, so `nav_goto()` sets the path on both running animations right after starting them (`lv_anim_get` + `lv_anim_set_path_cb`). Ease-in-out replaced ease-out because ease-out starts at full speed on the very first frame — exactly when the display is still busy sending that frame — and the initial jump was what read as a jolt.

What makes a slide smooth here is frame rate, and three things were taking it away:

- **Screen creation in the middle of the gesture.** For a while screens were built on demand (to make room for Wi-Fi on the CYD) — trackmode alone has dozens of objects to create at the instant of the swipe. All screens are created at boot again; a slide only draws.
- **Backgrounds in I4.** Indexed backgrounds saved 345 KB of flash but cost a palette lookup and an ARGB8888 conversion per line per frame. Measured on the host with the project's LVGL and real slides, RGB565 backgrounds render each slide frame ~26 % faster (0.051 vs 0.069 ms on the host; the ratio is what carries over). With the Wi-Fi gone, flash has room again, so they are **RGB565** (opaque: a straight line copy, never blended). Not ARGB8888, which would blend them pixel by pixel on every frame.
- **Rendering and sending one after the other.** A slide redraws the whole screen every frame, and at 55 MHz the SPI alone takes ~22 ms for 153.6 KB. The display now uses **DMA with two 16-line buffers** (`LCD_USE_DMA` in the `.ino`): `pushPixelsDMA` returns at once and LVGL draws the next band into the other buffer while the SPI sends this one. `tft.dmaWait()` runs before `setAddrWindow`, since changing the address window mid-transfer would corrupt the screen; `tft.startWrite()` keeps the display's chip select low for good (touch is on the other SPI host). The refresh period is 16 ms instead of LVGL's default 33, set on the display's refresh timer in the `.ino` rather than in the shared `lv_conf.h`. If the panel ever shows bands or misplaced blocks, `LCD_USE_DMA 0` brings back the blocking single-buffer path.

On the host, three full laps through the screens end with exactly the same memory in use, and an AddressSanitizer build reports nothing, including fast swipes that land mid-animation.

On **blelist**, tap a device to connect. The active connection is highlighted in green.

### Splash

`#define UI_ENABLE_SPLASH 0` in `src/ui/ui.h` takes the splash out of the build: `ui_splash.c`, its image asset and the fade animation are wrapped in `#if UI_ENABLE_SPLASH`, so the code stays in the tree but nothing reaches the binary (verified: no splash symbols in the ELF). Boot goes straight to nanomode. Set it to `1` to bring it back; it can also be forced from the build with `-DUI_ENABLE_SPLASH=1`.

### Status bar

A single Bluetooth icon on LVGL's `lv_layer_top()` — drawn over every screen, never intercepting touch — shown top-right while the ELM327 is connected and hidden otherwise. No background: the top of every screen is black (the gradient strip was removed from the nanomode background). With margin 0 the Montserrat 12 symbol ink ends at row 13 and trackmode's top-right gauge digits (Montserrat 28) start at row 15, so they never overlap.

### DTC

The **dtc** screen reads, lists and clears diagnostic trouble codes. The ELM327 work happens in the polling task (it owns the serial link); the screen only requests and displays.

- **Read** (only when the user taps **Ler** — opening the screen never starts a read): MIL status and count (`01 01`), stored (`03`), pending (`07`) and permanent (`0A`) codes, plus the freeze frame (`02`) of the first stored code. The response parser handles CAN (count byte per ECU) and K-line (three pairs per line) — detected with `ATDPN` — and multi-frame answers.
- **Clear** (mode `04`) asks for confirmation first: it also wipes the freeze frame and the readiness monitors, and should be done with the ignition on and the engine off. The result is shown on screen — accepted (`44`), refused (`7F 04 xx`; NRC `0x22` usually means the engine is running) or no response — and the codes are read again right after. It is only enabled when there is something to clear; permanent codes do not go away with mode `04`.
- Each code gets a **structural description** from `dtc_describe.c` (SAE J2012): system, SAE-generic vs manufacturer-specific, powertrain subsystem, and the misfiring cylinder for `P0300`–`P0312`. There is no text table — the ELM327 only returns the code, and a table would cost 40–60 KB of flash.
- DTC reads are on demand only; there is no periodic read, to keep the radio quiet.

### Phone access

> **Out of the build.** `#define UI_ENABLE_WEB 0` in `src/ui/ui.h` takes the **qr** screen out of the
> swipe and the binary and leaves the serial link to the Wemos uninitialised; the code and the
> `wemos-portal/` sketch stay in the tree. Set it to `1` (or build with `-DUI_ENABLE_WEB=1`) to bring
> it back — both variants compile. The preview honours the flag.

```
 ELM327 ──Bluetooth──▶ CYD ──── serial 115200 ────▶ Wemos D1 mini ──Wi-Fi──▶ phone
                      gauges, polling              AP, web page, history
                      ◀──── read / clear DTC ─────
```

**Wiring** (3.3 V logic on both sides, no level shifter):

| CYD — connector CN1 | Wemos D1 mini |
| --- | --- |
| GPIO 27 (TX) | D7 / GPIO13 (RX) |
| GPIO 22 (RX) | D8 / GPIO15 (TX) |
| GND | GND |

GPIO 27 and 22 are the only free pins on the CYD: the display takes 2, 12–15 and 21, touch 25, 32,
33, 36 and 39, the battery sense 35, and Serial2's default pins would land on the RGB LED. Power the
Wemos from **5 V** (its 5V pin or USB), not from the CYD's 3.3 V: Wi-Fi draws 300–400 mA peaks and
would brown out the CYD's regulator. D8/GPIO15 must be low at the Wemos boot; its on-board 10 kΩ
pull-down wins over the ESP32's weak RX pull-up. The Wemos keeps its USB port for flashing: the serial
is moved to D7/D8 with `Serial.swap()` only after boot, and its debug log goes to D4 (`Serial1`).

**Protocol** (`link_proto.h`, the same file in both sketches; `test_link` checks they are identical).
One text line per frame, `<type><content>*<CC>\n`, `CC` = XOR of type+content in two upper-case hex
digits; a line that fails the check is dropped whole, so a flipped bit on the wire is a lost update,
never a wrong number on the phone (all 87 single-bit flips of a sample frame are caught).

| Frame | Direction | Content | When |
| --- | --- | --- | --- |
| `S` | CYD → Wemos | JSON of `/api/state`, served verbatim | every 250 ms |
| `G` | CYD → Wemos | sample for the history chart, integers at a fixed scale, `-` for no reading | every 250 ms |
| `D` | CYD → Wemos | JSON of `/api/dtc` | when it changes, every 15 s anyway (a frame lost on the wire is not lost for good), and when the Wemos (re)appears |
| `W` | Wemos → CYD | SSID, password, URL, phones connected | every 2 s |
| `R` / `X` | Wemos → CYD | read / clear trouble codes | on a button in the page |

`R` and `X` go through the same checks as the on-device DTC screen: only with the ELM327 ready and
idle, and clear only when there is a stored or pending code. Otherwise a request made with the
adapter disconnected would sit in the gateway and fire on the next connection — a clear included.
The CYD's frames are written as a stream, with the XOR computed piece by piece; `test_wire` compiles
the real `link.cpp` against a fake serial and checks every frame the way the Wemos does (checksum,
valid JSON, NaN as `null`, the DTC list sent only on change, the request guards, corrupted and
oversized lines ignored). A state frame is ~360 bytes and a worst-case DTC list (36 codes) ~4.6 KB,
within the Wemos buffers (1 KB and 5 KB).

**On the phone.** The **qr** screen shows the Wemos network as the Wemos announces it, as a QR code in
the standard `WIFI:T:WPA;S:…;P:…;;` form that Android and iPhone cameras read natively; with no `W`
for 3 s it says the Wemos is not answering and hides the code. The SSID and password are derived from
the Wemos MAC, so the QR stays valid across reboots. The Wemos answers every DNS `A` lookup with its
own address (`captive_dns.c`) and every unknown path with a 302 to the page, so the phone's
connectivity check opens the page by itself. The page is served from PROGMEM and is fully
self-contained (no CDN — the access point has no internet). If the CYD stops talking, `/api/state`
answers `"cyd":false` and the page shows *painel sem resposta* instead of stale numbers as if live.

The Wemos keeps its own history from the `G` frames — 300 samples × 2 s, packed as `int16` — and
starts it over when a new session begins on the CYD, but keeps it when the session ends: parked is
when you want to look back.

**One request at a time.** The web server serves one client at a time and closes every connection,
and its listen backlog is small. The page therefore never has more than one request open: it asks
for `/api/state` 350 ms after the previous answer arrived (the CYD sends a new state every 250 ms,
so the phone follows the panel at roughly 2–3 updates per second), fetches `/api/history` every
5 s in the same chain, and queues the DTC buttons into it. Every request has a timeout, failures back
off (up to 5 s), and *sem conexão* only appears after three failures in a row. Polling stops while
the phone screen is off. A queued DTC action that fails is retried, but anything older than 15 s is
dropped, so a confirmed *Apagar* can never run minutes later. Tested against a server that delays up
to 1.2 s and drops 15 % of requests: one open request at most, no false *sem conexão*, every click
delivered in 3–6 s.

**Flashing the Wemos:** open `wemos-portal/wemos-portal.ino`, board *LOLIN(WEMOS) D1 R2 & mini*, upload
over USB. Or: `arduino-cli compile -u -p <port> -b esp8266:esp8266:d1_mini wemos-portal`.

## OBD2 gauges

| Gauge | Views | PID | Conversion |
| --- | --- | --- | --- |
| Lambda | nano, track | `0x44` / `0x34` / `0x24` (fallback chain) | `(A×256+B) / 32768` |
| Water temp (°C) | nano | `0x05` | `A − 40` |
| Battery (V) | nano, track | `0x42` | `(A×256+B) / 1000` |
| Boost (bar) | track | `0x0B` (MAP) − `0x33` (baro) | `(MAP − baro) / 100` |
| Comb pressure (bar) | track | `0x23`, fallback `0x59` | `(A×256+B) / 10` |
| IAT (°C) | track | `0x0F` | `A − 40` |
| Oil temp (°C) | track | `0x5C` | `A − 40` (unavailable when the DME does not publish it) |

ELM327 init sequence: `ATZ`, `ATE0`, `ATL0`, `ATS0`, `ATH0`, protocol from the
vehicle profile, then `0100` to confirm ECU response, `ATDPN` and the supported
PID discovery.

### Poll cadence

The adapter answers one PID per exchange, so every gauge in a round costs the
others. They do not all deserve the same rate: `vp_metric_period()` gives boost
and fuel rail **every** round, battery and IAT every **4th**, coolant and oil
every **8th**. With the full six-metric plan a typical round drops from six
questions to two plus the lambda slot, so the two gauges that follow the throttle
roughly double their refresh rate. Lambda keeps its own slot every round.

The polling task runs at FreeRTOS priority **2**, above the Arduino `loopTask`
(priority 1) that carries LVGL and, with the portal up, the HTTP serving. It
spends nearly all its time blocked in `vTaskDelay` inside `read_response`, so it
costs no CPU — it just stops waiting behind the UI to make the next exchange.

### Smoothing

A few readings per second written straight to the labels makes the panel step
from value to value and look stuck, even though it is showing everything the car
has to give. `ui_mock_data.c` eases each displayed value toward the last reading
on the 16 ms tick (`SMOOTH_ALPHA`, time constant ≈130 ms): same information, continuous
reading. The first sample of a session snaps instead of ramping, so a hot engine
does not spend seconds climbing from zero. Session maxima are **not** smoothed —
a peak is a moment that happened, not a quantity that varies. Because rendering
now runs at 60 Hz rather than only on new data, each label caches its last string
and skips `lv_label_set_text` when nothing changed.

## Vehicle profile

`src/gateway/vehicle_profile.{h,c}` holds what changes from car to car: the
ELM327 protocol command and, per gauge, the acceptable PID sources in order of
preference. The active profile is **BMW 120i F20 LCI (B48)**; build with
`-DVEHICLE_PROFILE_GENERIC` for the previous generic behaviour.

| Profile | Protocol | Notes |
| --- | --- | --- |
| BMW 120i F20 LCI (B48) | `ATSP6` — ISO 15765-4 CAN 11 bit, 500 kbps (OBD pins 6/14) | fixed protocol skips the `ATSP0` search, which costs seconds on F-series |
| Generic (OBD2) | `ATSP0` | lets the adapter probe |

At session start the gateway reads the supported-PID bitmaps (`0100`, `0120`,
`0140`) and polls **only** what the ECU declares. Two consequences:

- a PID the car does not publish no longer costs one round per cycle forever;
- oil temperature is no longer estimated from water temperature. The old
  `water + 10 °C` fallback put an invented number on the gauge; now the gauge
  stays empty, which is the truth. If your car does not answer `0x5C`, the value
  exists in BMW's UDS (header `6F1`, DME target `12`, service `0x22`) and the DID
  has to be discovered on the car itself — there is no guess in the code.

Boost uses barometric pressure (`0x33`) when the ECU publishes it, so the reading
stays honest at altitude; without it the firmware falls back to 101.325 kPa
(sea level), as before.

The serial log prints the decision on connect:

```
[ELM327] Perfil BMW 120i F20 LCI (B48): 6 PIDs, barometro lido
```

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
3. Wait for devices (scan starts when the screen is created, at boot).
4. Tap your ELM327 adapter.
5. Wait for `[ELM327] Ready` on serial (a few seconds).
6. Swipe to nanomode / trackmode — gauges update from live OBD2 data.

Without a connection, all gauges show `00`. Max values reset when the Bluetooth session ends.

The phone page is out of the build (`UI_ENABLE_WEB 0` in `src/ui/ui.h`). With it set to `1` and the
Wemos wired and flashed, swipe to **qr**, point the camera at the code, and the page opens.

## Notes and limitations

- Uses **Bluetooth Classic SPP**, not BLE. Most cheap ELM327 dongles are Classic.
- Default pairing PIN is `1234` (`bt_gateway.cpp`). Change it if your adapter uses another PIN.
- One PID is requested at a time; update rate depends on the adapter and ECU, which is the real ceiling
  on how "live" the gauges can be — not the radio.
- The web page needs the Wemos D1 mini: on the CYD alone, Wi-Fi and Bluetooth Classic did not fit
  together in memory nor share the radio well enough (see *Why the Wi-Fi lives on a second board*).
- The two boards sit centimetres apart on 2.4 GHz. Bluetooth's frequency hopping normally steers
  around the Wi-Fi channel; if the link misbehaves, move the boards a few centimetres apart.
- Clearing trouble codes is reachable from the web page. The access point is WPA2 with a per-device
  password and is always on, so anyone with that password (or a photo of the QR) can reach it while
  the car is on; the CYD still only accepts a clear with the ELM327 ready and codes to clear.
- The CYD's audio amplifier (LTK8002D, SPEAK connector) is fed from GPIO 26. The project plays no
  sound, but `setup()` drives GPIO 26 LOW as its first step: left floating, the amplifier input picks
  up noise from the display SPI and Bluetooth, amplifies it continuously and gets hot. The amplifier
  stays powered (a few mA quiescent); if it still runs hot with GPIO 26 tied and nothing on SPEAK,
  the cause is in the hardware.
- Oil temperature PID `0x5C` is not supported on all vehicles (e.g. some VAG ECUs).
- UI assets are large; keep **Huge APP** partition selected.
- UI text is ASCII-only: LVGL's built-in Montserrat fonts have no accented glyphs.

## License

Add a license of your choice if you publish this repository.
