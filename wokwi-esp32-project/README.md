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
| `chip-r` | `r.chip.c` | `dist/r.chip.wasm` |
| `chip-gps` | `gps.chip.c` | `dist/gps.chip.wasm` |
| `chip-l98nmotorcontrl` | `l98nmotorcontrl.chip.c` | `dist/l98nmotorcontrl.chip.wasm` |
| `chip-espcam` | `espcam.chip.c` | `dist/espcam.chip.wasm` |

සෑම `.wasm` file එකකටම එකම basename එක සහිත `.json` file එක `dist/` තුළ තිබේ. `wokwi.toml` paths සියල්ල `/` භාවිතයෙන් සකසා ඇත.

### “Missing” chip/editor bug එක සඳහා කළ fix

- Custom-chip `.json` සහ `.wasm` යුගල `dist/` තුළ copy කර ඇත.
- `wokwi.toml` හි `[[chip]]` blocks හතරම නිවැරදි diagram names වලට map කර ඇත.
- Local routing එකට root files නොව `dist/*.chip.wasm` explicit paths භාවිත කර ඇත.

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

## Separate device credentials

Robot config එක `ROBOT_TOKEN` භාවිත කරයි. Server `.env` එකේ `ROBOT_TOKEN` එයට exactly match විය යුතුය. Pump project එකට වෙනම `PUMP_TOKEN` එකක් ඇත.

## වැදගත් සටහන්

- Serial baud rate `921600`; Wokwi serial monitor ඒ අනුව firmware මගින් initialize වේ.
- Simulation එකට internet/server access නොලැබුණොත් Wi-Fi/WebSocket/HTTP functions reconnect හෝ timeout විය හැක. එය custom-chip loading error එකක් නොවේ.
- `include/config.h` තුළ server authentication token එකක් තිබේ. Original online project එක public නම් token එක exposed වී තිබිය හැකි බැවින් production භාවිතයට පෙර token එක rotate කිරීම සුදුසුය.
- `WebAssembly.compile(): expected magic word ... found 7b 22...` වැනි error එකක් තවමත් ලැබුණොත් extension එක update/reload කර, VS Code තුළ open කර ඇත්තේ project root folder එකදැයි බලන්න. `dist/*.wasm` files delete වී නැති බවත් තහවුරු කරන්න.
- Wokwi CLI `lint` දැනට original diagram එකේ `wokwi-junction` parts 9 ගැන `unknown-part-type` ලෙස report කරයි. ඒ parts original online Wokwi project එකෙන්ම පැමිණි wire-junction helpers වන අතර browser simulator එක ඒවා භාවිත කරයි; custom-chip WASM/path error එකක් නොවේ.
- මෙහි command-line simulation එක run කිරීමට පුද්ගලික `WOKWI_CLI_TOKEN` එකක් අවශ්‍ය බැවින් token එක package එකට ඇතුළත් කර නැත. VS Code Wokwi extension එකෙන් ඔබගේ account/license භාවිත කර start කරන්න.

## Autonomous mapped-block patrol

The controller now accepts the server's `autonomous_mission` command (up to 512 waypoints), follows GPS lawnmower waypoints in selected-block order, and uses the front/left/right (~45°) ultrasonic sensors for obstacle avoidance. The camera servo stays at 90° while moving. At each scan waypoint the rover stops, turns the camera left, uploads one image, turns right, uploads one image, and returns to center before continuing. Upload metadata includes mission, patrol, scan-point and side.

Manual `drive`, `stop`, pause and resume commands override autonomous motion. Because this build has no IMU/compass or wheel encoders, low-speed GPS course and timed obstacle turns are best-effort outdoors; precise crop-row tracking needs additional heading/odometry hardware or RTK GPS.
