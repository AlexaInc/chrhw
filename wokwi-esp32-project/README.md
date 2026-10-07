# ESP32 Agri Robot — Wokwi VS Code Project

Original/latest Wokwi project downloaded before editing: <https://wokwi.com/projects/476291712621785089>

The user's latest `diagram.json` positions and routed wires are retained. Missing local custom-chip logic/build files and the complete Arduino/PlatformIO firmware were added around that diagram.

The robot can run without USB: 3S pack -> BMS -> buck adjusted to 5.0V -> ESP32 VIN/5V rail. USB is needed only while uploading/debugging and must not be connected at the same time as an externally back-fed 5V source unless the board has proper power-path isolation.

මෙම folder එක VS Code එකෙන් open කර **Wokwi: Start Simulator** run කිරීමට අවශ්‍ය files, custom-chip WASM files සහ precompiled ESP32 firmware ඇතුළත් කර ඇත.

## ඉක්මනින් simulation එක start කිරීම

1. VS Code හි `File > Open Folder...` මගින් **මෙම `wokwi-esp32-project` folder එකම** open කරන්න.
2. Recommended extensions දෙක install කරන්න:
   - **Wokwi for VS Code** (`wokwi.wokwi-vscode`)
   - **PlatformIO IDE** (`platformio.platformio-ide`)
3. Extensions install වීම සම්පූර්ණ වනතුරු ඉඳලා `Ctrl+Shift+P` → **Developer: Reload Window** කරන්න.
4. `Ctrl+Shift+P` ඔබා **Wokwi: Start Simulator** තෝරන්න.
5. Wokwi license/account prompt එකක් ලැබුණොත් sign in කර license activate කරන්න.

Firmware එක නැවත build නොකරත් simulation එක start වීමට `firmware/firmware.bin` සහ `firmware/firmware.elf` දැනටමත් තිබේ. ඒ නිසා මුලින් `pio run` කිරීම අනිවාර්ය නොවේ.

### Windows හි `pio is not recognized` error එක

PlatformIO IDE extension එක install වී ඇති නමුත් CLI එක PATH එකේ නොමැති විට මෙම error එක ලැබේ. Updated VS Code build task එක Windows තුළ පහත bundled executable එක direct භාවිත කරයි:

```text
%USERPROFILE%\.platformio\penv\Scripts\platformio.exe
```

Extension එක install/reload කළ පසු `Ctrl+Shift+B` නැවත run කරන්න. අවශ්‍ය නම් PowerShell තුළ direct test එකක් ලෙස:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run
```

එම file එක නොමැති නම් PlatformIO installation එක තවම complete වී නැත: Extensions තුළ **PlatformIO IDE** install කර, VS Code status/progress අවසන් වනතුරු ඉඳලා **Developer: Reload Window** කරන්න.

## Source code වෙනස් කර build කිරීම

Main firmware source:

- `src/main.cpp` — PlatformIO compile කරන file එක
- `include/config.h` — pins, Wi-Fi සහ server settings
- `sketch.ino` / root `config.h` — original Wokwi download එකේ reference copies

VS Code එකේ `Ctrl+Shift+B` මගින් firmware build කළ හැක. Build එකෙන් පසු Wokwiට භාවිත කරන precompiled files refresh කිරීමට:

### Windows PowerShell

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-all.ps1
```

### Linux/macOS

```bash
bash ./scripts/build-all.sh
```

Full-build script එකට `pio` සහ `wokwi-cli` දෙකම PATH එකේ තිබිය යුතුය. Wokwi CLI: <https://github.com/wokwi/wokwi-cli/releases>

## Custom chips

මෙහි custom chips 4ක් තිබේ:

| Diagram type | Source | Binary used by VS Code |
|---|---|---|
| `chip-r` | `r.chip.c` | `r.chip.wasm` |
| `chip-gps` | `gps.chip.c` | `gps.chip.wasm` |
| `chip-l98nmotorcontrl` | `l98nmotorcontrl.chip.c` | `l98nmotorcontrl.chip.wasm` |
| `chip-espcam` | `espcam.chip.c` | `espcam.chip.wasm` |

Protection parts added in round 9 (`cap`, `ecap`, `diode`, `fuse`, `ldo33`) are
custom chips too - they only declare their pins, so the diagram can show and
wire them. Their `.chip.c` sources build the same way (`wokwi-cli chip compile`
or `scripts/build-all.sh`).

සෑම `.wasm` file එකකටම එකම basename එක සහිත `.json` file එක project root තුළ තිබේ. `wokwi.toml` එම root binaries භාවිත කරයි.

### “Missing” chip/editor bug එක සඳහා කළ fix

- Custom-chip `.json` සහ `.wasm` යුගල project root තුළ එකම copy එකක් ලෙස තබා ඇත.
- `wokwi.toml` හි `[[chip]]` blocks diagram names වලට සහ root binaries වලට map කර ඇත.
- පරණ duplicate `dist/` copies ඉවත් කර project එක සරල කර ඇත.

`r.chip.json` තුළ original project එකෙන් පැමිණි custom `body` SVG property එකක් ඇත. Latest Wokwi CLI එක ඒ property එක ගැන **validation warning** එකක් දෙයි, නමුත් WASM compile එක සාර්ථකය. ඇතැම් VS Code diagram-editor versions වල custom SVG වෙනුවට generic breakout drawing එක පෙන්විය හැක; එය chip simulation logic failure එකක් නොවේ.

## Build verification

මෙම package සකස් කරන විට:

- ESP32 firmware PlatformIO මගින් **successfully compiled** කරන ලදී.
- Custom chips 4ම Wokwi CLI 0.27.1 මගින් **successfully compiled** කරන ලදී.
- සියලු WASM files වල WebAssembly magic header (`00 61 73 6d`) verify කරන ලදී.
- `diagram.json` custom-chip names සහ `wokwi.toml` names එකිනෙකට match වන බව verify කරන ලදී.
- Firmware usage: RAM 14.7%, Flash 76.1%.

## Robot power wiring — 3S battery pack, BMS, buck and L298N

Diagram එකේ original sensor/motor wires move කර නැහැ. Power subsystem එක වම් පැත්තට වෙනම add කර ඇත: `cell1..3`, `bms1`, `buck1`.

Physical wiring:

1. Matching 3.7V Li-ion cells 3ක් **series** කරන්න: pack nominal 11.1V, full charge 12.6V.
2. Exact BMS-board manual එක අනුව cell taps `B-`, `B1`, `B2`, `B+` connect කරන්න. Wrong sequence එක BMS/cells damage කළ හැක.
3. Load සහ charger දෙකම BMS `P+` / `P-` හරහා පමණක් connect කරන්න.
4. `P+` -> fuse -> L298N `12V`; `P-` -> L298N `GND`.
5. ඒ P+/P- දෙකම buck converter input `IN+`/`IN-` ට දෙන්න.
6. Multimeter එකෙන් buck output එක **5.0V** ලෙස adjust කරලා පසුව ESP32 `VIN/5V`, ESP32-CAM `5V`, සහ logic/sensor 5V rail එකට දෙන්න. සියලු logic grounds common කරන්න.
7. L298N `5V-EN` jumper එක **remove** කරලා buck 5V එක L298N `5V` logic terminal එකට දෙන්න. Jumper installed නම් external buck 5V සහ onboard-regulator 5V එකට tie කරන්න එපා.

Raw 3S voltage ESP32 `VIN/5V` හෝ ESP32-CAM `5V` pin වලට direct දෙන්න එපා. Buck converter එකේ continuous current rating එක ESP32, ESP32-CAM, sensors සහ relay peaks සඳහා ප්‍රමාණවත් විය යුතුය; practical target එක අවම වශයෙන් quality 5V/2A–3A supply එකකි.

## Circuit protection — round 9 (why the boards kept burning)

Two ESP32 boards died with `assert failed: __esp_system_init_fn_init_flash`
before any application code ran. The causes are physical, not software, and the
diagram now shows the fix instead of only describing it. Nothing that was
already wired was moved: the protection parts were added around the existing
parts and the connections that need a part *in series* were re-drawn through
that part.

### What was added to `diagram.json`

| New part(s) | Value | Where it sits | Why |
|---|---|---|---|
| `fuse1` | 5A blade | BMS `P+` -> L298N `12V` | stops a short from cooking the pack/wires |
| `d1` | SS34 Schottky | after the fuse | reverse-polarity protection |
| `c12e` + `c12c` | 470uF + 100nF | 12V rail (after `d1`) | motor-current dips and brush noise |
| `c5e` + `c5c` | 1000uF + 100nF | ESP32 `VIN` / 5V rail | the brownout/reboot fix when a motor starts |
| `c3v3e` + `c3v3c` | 10uF + 100nF | ESP32 `3V3` pin | keeps the 3.3V rail clean |
| `c_srve` + `c_srvc` | 470uF + 100nF | at the servo | servo inrush - otherwise the ESP32 resets |
| `c_u1..3`, `c_lcd`, `c_sd`, `c_dht`, `c_rain`, `c_gps`, `c_cam`, `c_esp` | 100nF each | VCC-GND of every module | one bypass cap per module |
| `r_m1..r_m6` | 220 ohm | ESP32 -> `EN A`, `IN1..IN4`, `EN B` | protects the ESP32 pin if a driver input shorts |
| `r_trig` | 220 ohm | `D15` -> shared TRIG line | same, on the trigger line |
| `r_d1a/b`, `r_d2a/b`, `r_d3a/b` | 1k + 2k | every HC-SR04 `ECHO` | 5V ECHO -> 3.33V at the GPIO (no divider = dead pin) |
| `r_gpio12` | 10k pull-down | `D12` -> GND | **GPIO12 is the MTDI flash-voltage strap** - if it is high at boot the ESP32 re-runs `init_flash` and dies |
| `r_dhtpu` | 10k | DHT22 `SDA` -> VCC | DHT22 pull-up |
| `r_i2c1`, `r_i2c2` | 4.7k | OLED `SDA`/`SCL` -> VCC | I2C pull-ups |
| `r_sd` | 33 ohm | `D33` -> SD `SCK` | series resistor on the SPI clock |
| `r_cam1`, `r_cam2` | 10k | `TX0`/`RX0` <-> ESP32-CAM | series protection on the camera UART |
| `d_f1..d_f4` | 1N5819 | L298N `OUT1..OUT4` -> +12V | freewheel diodes for the motor coils |
| `ldo1` | AMS1117-3.3 | 5V rail -> ESP32-CAM `VCC` | the camera gets its own 3.3V supply |

### Connections that were re-drawn (old wire removed, two new ones added)

```
D25/D26/D27/D14/D12/D13 -> r_m1..r_m6 -> L298N EN A / IN1 / IN2 / IN3 / IN4 / EN B
D15 -> r_trig -> TRIG bus (j9)
ultrasonic1/2/3 ECHO -> 1k -> 2k -> D32 / D23 / VN     (2k also to GND)
D33 -> r_sd -> SD SCK
TX0 / RX0 -> r_cam -> ESP32-CAM RX / TX
BMS P+ -> fuse1 -> d1 -> L298N 12V and buck VIN+   (caps hang on d1 cathode)
ultrasonic VCC (j8) -> 5V rail (j2)   [they were on 3V3; the ECHO divider assumes 5V]
ESP32-CAM VCC -> ldo1 3V3 (its own regulator, not the ESP32 3V3 pin)
ESP32-CAM CAMVCC -> +5V  (was 3V3)
```

### Things the simulator cannot show

`diagram.json` has no motor-terminal or ground-rail primitives, so these two are
text notes inside the diagram and must be done on the real robot:

* **100nF across each motor terminal** (in addition to the four `d_f1..d_f4`).
* **Star ground**: the motor ground and the logic ground must meet at `BMS P-`
  only - never on a shared rail.

### How to look at it

1. Open this `wokwi-esp32-project` folder in VS Code and press `F1` ->
   **Wokwi: Start Simulator**.
2. The green text block on the left is the legend of every part that was added
   for protection, in the same order as the table above.
3. Wires the script added have an empty route on purpose, so Wokwi draws their
   route on screen the first time you move one - that is normal.
4. The `cap`, `ecap`, `diode`, `fuse` and `ldo33` parts are custom chips. The
   prebuilt `*.chip.wasm` files are in the project root and `wokwi.toml` maps
   them; if a part shows as *Missing*, the `.wasm` file was not copied next to
   `diagram.json`.

### 9. ටිකකින් (Sinhala summary)

* `init_flash` crash එකට ඇත්ත හේතුව: **GPIO12 (L298N IN4) strapping pin එක** - දැන් `r_gpio12` 10k pull-down එකක් තියෙනවා.
* HC-SR04 **ECHO 3ටම 1k+2k divider** - 5V කෙලින්ම GPIO එකට ගියොත් pin එක මැරෙනවා.
* **fuse + SS34 + 470uF/1000uF/100nF caps + 1N5819 flyback diodes** - ඕවා නැතුව motor start වෙද්දී ESP32 reboot වෙනවා.
* **220Ω series resistors** හයක් L298N inputs වලට, **10k GPIO12 pull-down**, **4.7k I2C**, **33Ω SD SCK**, **10k CAM UART**.
* විස්තර ටික Sheet 3 එකේ - `chr-robot-protection-circuits.zip`.

## Separate device credentials

Robot config එක `ROBOT_TOKEN` භාවිත කරයි. Server `.env` එකේ `ROBOT_TOKEN` එයට exactly match විය යුතුය. Pump project එකට වෙනම `PUMP_TOKEN` එකක් ඇත.

## වැදගත් සටහන්

- Serial baud rate `921600`; Wokwi serial monitor ඒ අනුව firmware මගින් initialize වේ.
- Simulation එකට internet/server access නොලැබුණොත් Wi-Fi/WebSocket/HTTP functions reconnect හෝ timeout විය හැක. එය custom-chip loading error එකක් නොවේ.
- `include/config.h` තුළ server authentication token එකක් තිබේ. Original online project එක public නම් token එක exposed වී තිබිය හැකි බැවින් production භාවිතයට පෙර token එක rotate කිරීම සුදුසුය.
- `WebAssembly.compile(): expected magic word ... found 7b 22...` වැනි error එකක් තවමත් ලැබුණොත් extension එක update/reload කර, VS Code තුළ open කර ඇත්තේ project root folder එකදැයි බලන්න. root `*.chip.wasm` files delete වී නැති බවත් තහවුරු කරන්න.
- Wokwi CLI `lint` දැනට original diagram එකේ `wokwi-junction` parts 9 ගැන `unknown-part-type` ලෙස report කරයි. ඒ parts original online Wokwi project එකෙන්ම පැමිණි wire-junction helpers වන අතර browser simulator එක ඒවා භාවිත කරයි; custom-chip WASM/path error එකක් නොවේ.
- මෙහි command-line simulation එක run කිරීමට පුද්ගලික `WOKWI_CLI_TOKEN` එකක් අවශ්‍ය බැවින් token එක package එකට ඇතුළත් කර නැත. VS Code Wokwi extension එකෙන් ඔබගේ account/license භාවිත කර start කරන්න.

## Autonomous mapped-block patrol

The controller now accepts the server's `autonomous_mission` command (up to 512 waypoints), follows GPS lawnmower waypoints in selected-block order, and uses the front/left/right (~45°) ultrasonic sensors for obstacle avoidance. The camera servo stays at 90° while moving. At each scan waypoint the rover stops, turns the camera left, uploads one image, turns right, uploads one image, and returns to center before continuing. Upload metadata includes mission, patrol, scan-point and side.

Manual `drive`, `stop`, pause and resume commands override autonomous motion. Because this build has no IMU/compass or wheel encoders, low-speed GPS course and timed obstacle turns are best-effort outdoors; precise crop-row tracking needs additional heading/odometry hardware or RTK GPS.

## Camera diagnostic protocol

A no-frame reply now includes `<IMG:0><CAMERR:STAGE:code>`. The controller logs
`INIT`, `CAPTURE`, or `ENCODE`, which separates physical sensor initialization
faults from raw-frame and RGB565 software-JPEG failures. If it reports `no
diagnostic`, upload the current camera sketch to the ESP32-CAM board.

### `ENCODE:0x101` correction

`frame2jpg()` required a second contiguous JPEG output allocation and failed on
the physical board. The UART camera now uses `frame2jpg_cb()` like the working
CameraWebServer example. It encodes the same frame twice: a count-only pass to
produce `<IMG:size>`, followed by a chunked pass written directly to UART. This
avoids allocating a complete JPEG buffer alongside the RGB565 frame.

### AI Thinker raw-camera compatibility

The physical UART camera sketch mirrors the proven Espressif CameraWebServer
raw configuration: `PIXFORMAT_RGB565`, `FRAMESIZE_240X240`, 20 MHz XCLK, one
frame buffer and `CAMERA_GRAB_WHEN_EMPTY`.

### Socket reconnect after `field_map`

`field_map` is connection-time configuration, not a motor action. The
controller now consumes it without forwarding `FIELD_MAP` to `controlMotors()`.
Socket event JSON capacity is sized from the payload (8-48 KB) instead of
allocating 96 KB for every event, avoiding WiFi/WebSocket heap starvation.
Disconnect logs include WiFi state and free heap for diagnosis.

### Known-good Socket.IO handshake restored

The connection path now matches Wokwi project 476291712621785089: Engine.IO 4
with `role` and `token` query parameters only. The experimental EIO3 fallback
was removed because the server reported `forced close`. `deviceId` is omitted;
the server already defaults this role to `robot-01`.


### Bounded autonomous mission transfer

The ESP32 WebSockets library accepts frames up to 15 KB. The production DB's
216-waypoint active mission serialized to 35,737 bytes, so the server's automatic
mission restore closed the ESP32 transport immediately after every connection.
The server now sends `autonomous_mission_begin`, 32-waypoint
`autonomous_mission_chunk` events, and `autonomous_mission_end`. The controller
assembles these directly into its fixed waypoint array and never moves until the
full route is validated. The actual 216-waypoint route was reconstructed exactly
in 7 chunks; the largest complete Socket.IO event was 5,428 bytes.
