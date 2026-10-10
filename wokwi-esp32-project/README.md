# CHR-01 ESP32 rover — Wokwi + VS Code

This folder is the standalone Wokwi project. **Open `wokwi-esp32-project` itself in VS Code** (or open the repository's `wokwi-esp32-project.code-workspace` file), not the parent monorepo folder. `diagram.json`, `wokwi.toml`, the firmware and all custom-chip files live here.

> **Safety:** this Wokwi drawing is not a validated PCB design. Custom battery, BMS, buck, charger, fuse, diode, capacitor and DC-motor models are visual/digital placeholders; they do not enforce real voltage, current, thermal, mechanical or cutoff limits. Read [`SAFETY_BOM.md`](SAFETY_BOM.md), [`HARDWARE_SAFETY.md`](HARDWARE_SAFETY.md) and [`FLASHING_TROUBLESHOOTING.md`](FLASHING_TROUBLESHOOTING.md) before connecting real hardware.

## Start the simulator

1. Install **Wokwi for VS Code** (`wokwi.wokwi-vscode`). Install PlatformIO only if you also want to rebuild the firmware.
2. Open this folder in VS Code.
3. Run **Developer: Reload Window** after installing the extension.
4. Run **Wokwi: Start Simulator** from the Command Palette.
5. If prompted, sign in to Wokwi and activate the extension license.

The committed `firmware/firmware.bin` and `.elf` let the simulator start without compiling. The simulator runs the `.bin`; rebuilding `src/main.cpp` alone does **not** automatically refresh that file.

### If a custom chip is missing in VS Code

The project has one authoritative copy of each chip triplet:

- `<name>.chip.json` — pinout/controls
- `<name>.chip.c` — Wokwi Chips API source
- `<name>.chip.wasm` — compiled simulator binary

All files are at the project root and mapped by `wokwi.toml`. Do not open only `diagram.json` or the parent repository as a standalone VS Code folder. First run:

```bash
python3 scripts/check-wokwi-diagram.py .
```

Then reload VS Code and start the simulator again. To rebuild the custom chips (Wokwi CLI required):

```bash
for f in *.chip.c; do
  name="${f%.chip.c}"
  wokwi-cli chip compile "$f" -o "$name.chip.wasm"
done
```

The `diagram.json` no longer uses non-standard `wokwi-junction` parts; multi-drop wires terminate on the real supply/ground/signal pins. Custom-chip JSON keys follow Wokwi's documented schema.

## Build firmware

Canonical firmware/configuration:

- `src/main.cpp` — PlatformIO firmware source
- `include/config.h` — shared pin map and defaults
- `sketch.ino` — Arduino/Wokwi entry point that includes the PlatformIO source

Build with PlatformIO:

```bash
pio run
```

Then refresh the Wokwi binary:

```bash
cp .pio/build/esp32dev/firmware.bin firmware/firmware.bin
cp .pio/build/esp32dev/firmware.elf firmware/firmware.elf
```

If you edit the shared firmware/config, refresh the self-contained Arduino IDE copy and run the repository consistency checks from the repository root:

```bash
bash scripts/sync-arduino-ide.sh
bash scripts/check-code-copies.sh
python3 wokwi-esp32-project/scripts/check-wokwi-diagram.py wokwi-esp32-project
```

## Safety changes in this revision

- L298N `IN4` moved from ESP32 GPIO12/MTDI to GPIO22; GPIO12 is no longer a motor-control signal. Six 1 kΩ series resistors and six 10 kΩ pull-downs hold the bridge inputs low during reset.
- The rover reads rain `AO` on GPIO34 for continuous wetness and samples the custom chip's active-high `DO` on GPIO35 for diagnostics. The simulated sensor is powered at 3.3 V; no rain fields or payload keys changed (`rainDrop`/`isRaining` remain AO-derived). GPIO22 remains L298N `IN4`, so DO does not share that motor pin. Soil-moisture sensing is intentionally not part of the rover; it remains in `wokwi-water-pump-c3`.
- The DHT22 and OLED share the ESP32's common ground and 3.3 V supply. Their bypass capacitors stay local to each module; motor/servo returns remain on the separate BMS negative path.
- Each HC-SR04 `ECHO` has a 1 kΩ series + 2 kΩ-to-ground divider in the drawing (about 3.33 V from a nominal 5 V echo). Recalculate at the sensor's maximum output and check the exact ESP32 input/absolute-maximum limits; use a safer divider ratio or level shifter if needed.
- Servo power has a separate 5 V buck rail; grounds share the battery/BMS negative return. Camera UART uses modest 470 Ω series resistors and must be unplugged from UART0 during USB flashing.
- The L298N clamp drawing shows upper and lower diodes for all four outputs. Check whether the real L298N *module* already has its eight diodes before adding more; select diode and fuse ratings from the real motor stall current, BMS and wire ratings.
- Two custom `DC Motor` chips are wired to L298N OUT1/OUT2 and OUT3/OUT4. They are visual-only, passive placeholders: they do not model rotation, current draw, stall, back-EMF, brush noise or hardware safety. Add a real 100 nF suppression capacitor directly across each brushed motor's terminals; the BOM lists this as a not-drawn hardware addition.
- Unsupported junction parts and trailing-space custom-chip pin names were removed/normalized to avoid editor/manifest parsing problems.

These are engineering safeguards, **not a guarantee that the board cannot be damaged**. The battery, motor, USB-UART chip and exact ESP32 module are not identified here, so fuse/diode/supply ratings still need to be matched to their datasheets.

## chrserver / chrclient telemetry compatibility

The rover keeps the existing `Type: "sensors"` message and field names for temperature, humidity, `rainDrop`, `isRaining`, and ultrasonic distances; only the unsupported rover `soilMoisture` value was removed. On 2026-10-07, the linked `chrserver` main branch explicitly strips rover `soilMoisture`, consumes `rainDrop` for rover rain handling, and receives actual soil moisture as a separate `Type: "irrigation"` pump message. The linked `chrclient` keeps `rainDrop` in `SensorsMessage` and `soilMoisture` only in `IrrigationMessage`. `DEVICE_ROLE="esp_32"` and `FW_TARGET="rover"` remain unchanged, so no server/client API change is needed.

- [chrserver sensor/irrigation handler](https://github.com/AlexaInc/chrserver/blob/b82e3ce6a4dd6babc34e15444a6236df445ba214/src/sockets/wsserver.ts)
- [chrserver rover message type](https://github.com/AlexaInc/chrserver/blob/b82e3ce6a4dd6babc34e15444a6236df445ba214/db/Sqlight.ts)
- [chrclient telemetry types](https://github.com/AlexaInc/chrclient/blob/5e35073a66012e884968e53db639bf1ed6a67220/src/types/messages.ts)
- [chrclient rover rain display](https://github.com/AlexaInc/chrclient/blob/5e35073a66012e884968e53db639bf1ed6a67220/src/screens/RobotScreen.tsx)

## සිංහලෙන් කෙටියෙන්

- VS Code වලින් open කරන්න ඕනේ `wokwi-esp32-project` folder එකමයි; parent folder එක නොවේ.
- `Wokwi: Start Simulator` තෝරන්න. Custom chip එකක් නොපෙනේ නම් `python3 scripts/check-wokwi-diagram.py .` ධාවනය කර VS Code reload කරන්න.
- GPIO12 (MTDI) එක L298N motor input එකෙන් ඉවත් කර GPIO22 භාවිතා කර ඇත. Rain sensor එක 3.3V වලින් පමණක් පෝෂණය කරන ලෙස diagram එක සකස් කර ඇත.
- Wokwi custom BMS/buck/fuse/diode/capacitor models සැබෑ current/voltage protections simulate කරන්නේ නැහැ. සැබෑ board එකට සම්බන්ධ කිරීමට පෙර `HARDWARE_SAFETY.md` බලන්න.
