# chrhw — CropHealth hardware firmware

Two Wokwi-designed projects that run on real ESP32 boards and talk to
`chrserver` over Socket.IO:

| Project | Board | Role | Device id |
|---|---|---|---|
| `wokwi-esp32-project` | ESP32 DevKit V1 (+ ESP32-CAM over UART) | `esp_32` | `robot-01` |
| `wokwi-water-pump-c3` | ESP32-C3 Super Mini / DevKit V1 | `esp_c3_pump` | `pump-01` |

---

## 1. One firmware source, and self-contained Arduino IDE folders

The same firmware used to exist as several hand-copied files
(`sketch.ino`, `src/main.cpp`, `arduino-ide/<board>/*.ino`, …) and every edit had
to be repeated in each copy. That is gone. There are two rules now:

```
wokwi-esp32-project/
├── src/main.cpp                        <-- THE firmware (edit ONLY here)
├── include/config.h                    <-- THE configuration (edit ONLY here)
├── config.h, logo_bitmap.h             -> include-shims (Wokwi/PlatformIO build)
├── sketch.ino                          -> #include "src/main.cpp"
└── arduino-ide/esp32-dev-controller/   <-- opens directly in the Arduino IDE
    ├── esp32-dev-controller.ino        -> board settings + notes (no code)
    ├── main.cpp                        -> byte-identical copy of src/main.cpp
    ├── config.h                        -> GENERATED: config.machine.h + include/config.h
    ├── config.machine.h                <-- THIS board's Wi-Fi / pins  (edit here)
    ├── arc_math.h, logo_bitmap.h       -> copies
    └── README.md                       -> what the folder is, how to refresh it
```

* **Firmware code** lives in `src/main.cpp` only. `sketch.ino` (Wokwi/PlatformIO)
  includes it; the Arduino IDE folders carry a **copy** of it, because the IDE can
  only compile files inside the sketch folder.
* **Pins, tokens, limits, the SD wiring and the OTA identity** live in
  `include/config.h` only. Every value is `#ifndef`-guarded, so a build target can
  override just what differs (`include/config.local.h.example`; `config.local.h`
  is git-ignored, so credentials stay out of the repository).
* The Arduino IDE folders own their machine values (`config.machine.h`: Wi-Fi, and
  for the rover `CHRH_FORCE_GATEWAY_MODE 1`, i.e. the LAN
  `http://<gateway-ip>:8000` fallback) plus their `FW_TARGET` (see §8).
* After ANY firmware edit:

  ```bash
  ./scripts/sync-arduino-ide.sh     # refresh every Arduino IDE folder
  ./scripts/check-code-copies.sh    # prove nothing drifted (byte-compares the copies)
  ```

* `check-code-copies.sh` verifies all of that: no include may leave a sketch
  folder, `main.cpp` must equal `src/main.cpp` byte for byte, `setup()/loop()`
  must be defined in exactly one file per folder (never twice), the generated
  `config.h` must carry the real configuration, every `#if/#ifdef` must have its
  `#endif`, tokens/Wi-Fi must not be duplicated, OTA targets must be unique, and
  it warns when `firmware/firmware.bin` is older than the source (Wokwi runs the
  binary, not your source).

## 2. Build, simulate and flash

```bash
# PlatformIO (also what Wokwi VS Code runs, via wokwi.toml -> firmware/*.bin)
cd wokwi-esp32-project && pio run
cd wokwi-water-pump-c3 && pio run

# rebuild AND refresh the binary Wokwi actually executes
./scripts/refresh-firmware.sh all      # or: rover | pump
```

> Wokwi for VS Code runs the committed `firmware/firmware.bin`. After any
> firmware change run `./scripts/refresh-firmware.sh <project>` before starting
> the simulator, otherwise the simulator keeps running the old binary.

Real hardware: open the matching `arduino-ide/<board>/` **folder** in the Arduino
IDE and upload — it compiles the copy of `src/main.cpp` that
`scripts/sync-arduino-ide.sh` put inside it, so it is the same code you edited.

> **Partition scheme matters once OTA is in play (§8).** Pick a scheme with TWO
> app slots — "Default 4MB with spiffs", or "Minimal SPIFFS (1.9MB APP with OTA)".
> A single-slot scheme ("Huge APP") leaves nowhere to put an over-the-air image,
> and the board refuses the update with "not enough flash space" instead of half
> flashing itself.

Guards and helpers (all plain scripts, no PlatformIO needed for the first two):

```bash
./scripts/check-code-copies.sh   # proves every sketch still points at the ONE firmware source
./scripts/test-arc-math.sh       # unit-tests the avoidance decision table on this PC
./scripts/sim-obstacle.sh 24     # puts a "plant" 24 cm ahead of the simulated rover
./scripts/refresh-firmware.sh all  # rebuild + copy firmware/*.bin|elf that Wokwi actually runs
```

`check-code-copies.sh` also warns when `firmware/firmware.bin` is older than the
source, which is the classic "I changed the code but the simulator still behaves
the old way" trap — Wokwi executes the committed binary, not your source.

## 3. Rover safety and speed limits (new)

The rover used to write 200/180 PWM (~78 % duty) straight to the L298N enables
and to block the main loop with `delay()` while it ramped and reversed, so it
could not be stopped in time between closely planted crops. The firmware now
runs a **non-blocking motion engine** (`serviceMotors()` in `src/main.cpp`):

| Behaviour | Before | Now |
|---|---|---|
| Straight / turn PWM | 200 / 180 hardcoded | 110 / 95 defaults, panel-scalable, `MOTION_HARD_MAX_PWM 150` ceiling |
| Speed changes | instant jump | stepped ramp (10 PWM / 20 ms up, 25 PWM / 12 ms down) |
| Obstacle | stop below 15 cm (only 1× / 5 s scan) | the whole front arc is scanned 4× / 240 ms (one sensor per slot, never all three at once); slow from 70 cm, crawl below 45 cm, **steer around** below 30 cm, emergency brake at 12 cm — see section 4 |
| STOP | queued behind `delay()` | PWM cut immediately, bridge released |
| Losing the app / link | rover kept driving | drive dead-man failsafe 1.5 s (auto 3 s) and instant stop on socket loss |
| Turns | fixed 650/1000 ms bypass legs | legs scale with the panel's turn speed so the angle stays the same |

Speed limits come from the web panel / server (`motion_config`) and are stored
on the SD card, so the rover keeps obeying them after a reboot. Percentages are
of the firmware's own safe values: **the panel can only make the rover slower,
never faster**, and the firmware clamps again against `MOTION_HARD_MAX_PWM`.

## 4. Front sensor arc — go around, stop only if you really cannot (new)

The three HC-SR04 sit on the printed brackets: one **centre** sensor and the two
**side** sensors, whose brackets splay them outwards. With the beams angled that
way the three cones overlap into one continuous fan (a side sensor at 45° with
its 8° half cone already watches from 37° outwards), so there is **no blind spot
between the centre beam and either front corner**.

A reading on its own says nothing — it is taken *along that sensor's axis*. The
firmware therefore converts every sample (see `include/arc_math.h`):

```
lateral room  = reading x sin(angle + half cone)   -> how wide the gap is
forward reach = reading x cos(angle - half cone)   -> how soon it blocks us
```

A gap only counts as passable when it is wider than `ROVER_HALF_WIDTH_CM`
(13 cm) + `OBSTACLE_SIDE_MARGIN_CM` (4 cm) = **17 cm**.

### What the rover does with it

| Situation | Before | Now |
|---|---|---|
| Plant dead ahead, a side open | stop at 30 cm and wait | **steers to the wider side**, creeps past at crawl speed, then turns back onto the heading (auto) / keeps obeying the held drive button (manual) |
| Plant beside the front corner | stop at 30 cm | **shaves off**: keeps driving and nudges away from it |
| Both side gaps narrower than 17 cm | stop | stop and report `no-path` -> warning alert |
| Something inside 12 cm | stop | emergency brake — the only unconditional stop |

The rover therefore stops by itself only when there is genuinely nothing wide
enough to squeeze past. The manoeuvre is a small state machine:

```
CLEAR -> STEER-LEFT/RIGHT -> CREEP -> (autonomous: TURN-BACK) -> CLEAR
```

* turning out is time-boxed (`AVOID_MAX_TURN_MS`, 2.6 s); if the path still is
  not open the rover creeps forward and looks again — it never spins on the spot
  forever;
* two consecutive scans are needed before a manoeuvre starts and three clear
  scans before it ends (`AVOID_CONFIRM_SCANS` / `AVOID_CLEAR_SCANS`), which is
  the hysteresis against 60 ms twitching;
* the creep leg is the only place where the forward stop band is relaxed (down to
  the emergency ring) and it is only entered while a side gap is verified;
* **manual** driving gets the same steering for a held FORWARD command while
  *Auto-avoid steering* is ON in Settings. Switch it off and the rover brakes at
  the safety distance instead and the operator decides where it goes;
* **autonomous** mode always avoids: a running detour owns the wheels and hands
  control back to the waypoint navigation when it is done (the mission does not
  stop).

### The sensor angles are configuration, not guesses

The angles the brackets really hold are configuration
(`SENSOR_ANGLE_LEFT_DEG` / `SENSOR_ANGLE_RIGHT_DEG`, default **45°**) and can be
changed **from the web panel** (Settings -> Left/Right Sensor Angle, 25-80°) or
with a `motion_config` action. They are stored on the SD card next to the speed
limits, so re-bolting a bracket only needs the number changed — never a re-flash.

### Verifying it without hardware

```bash
./scripts/test-arc-math.sh          # unit-tests the decision table on this PC (26 checks)
./scripts/sim-obstacle.sh 24        # Wokwi: put a plant 24 cm dead ahead
./scripts/sim-obstacle.sh right 20  # Wokwi: plant only at the front-right corner
./scripts/sim-obstacle.sh --show    # what the simulated sensors currently see
./scripts/sim-obstacle.sh --reset   # back to the committed scene
```

A simulated HC-SR04 in Wokwi takes its distance from `diagram.json` (it does not
ray-cast), so `sim-obstacle.sh` edits exactly those numbers and leaves the rest of
the file byte-for-byte identical. The committed scene is front 49 / left 197 /
right 400 cm: the rover drives on at reduced speed with no manoeuvre — which is
also a good check that the planner does not invent obstacles.

## 5. Field map on the microSD card (new)

The rover used to receive the whole field map on every single reconnect (SD
support was compiled in but never used). Now:

1. the server computes a short content **revision** (`rev`, sha1 prefix) of the
   stored map and sends it inside the `field_map` payload;
2. the rover caches the map on SD (`/chrhw/fieldmap.json` + `fieldmap.meta`,
   written via temp-file + rename so a power cut cannot corrupt it) and keeps
   the revision in RAM;
3. right after connecting it sends `device_hello` **with that revision**;
4. the server compares the two and only sends `field_map` when they differ —
   legacy firmware that never sends a hello still gets the map after a 2.5 s
   grace window;
5. the rover answers `map_status` (rev, blocks, bytes, sd) and the dashboard
   shows *Field map (SD cache) → IN SYNC / NOT SYNCED*.

SD wiring (from `diagram.json`): **CS = GPIO5, SCK = GPIO33, MOSI = GPIO2,
MISO = GPIO36 (VP)**.

## 6. Socket.IO contract

**Rover/pump → server** (`message.upsert` unless noted)

| Event | Payload | Purpose |
|---|---|---|
| `device_hello` (event) | `deviceId, role, firmware, mapRev, mapBlocks, mapBytes, sd, driveSpeedPercent, turnSpeedPercent, sensorAngleLeftDeg, sensorAngleRightDeg, avoidAssist` | handshake: which map revision is cached + the arc geometry in use |
| `map_status` | `rev, name, blocks, bytes, sd, path, reason` | SD cache confirmation (`saved` / `unchanged` / `save_failed`) |
| `motion_config` | `reason, driveSpeedPercent, turnSpeedPercent, drivePwm, turnPwm, appliedPwm, intent, source, blockedBy, hardMaxPwm, obstacleStopCm, emergencyStopCm, driveFailsafeMs, sensorAngleLeftDeg, sensorAngleRightDeg, avoidAssist, avoidState, avoidDir, gapLeftCm, gapRightCm, frontCm` | config ack, arc manoeuvre, obstacle stop, dead-man failsafe. `reason`: `applied` / `unchanged` / `requested` / `avoiding` / `creep` / `turn-back` / `clear` / `no-path` / `emergency` / `obstacle` / `failsafe`. `blockedBy`: `plant-left` / `plant-right` / `plant-ahead` / `no-path` / `emergency` / `obstacle` / `failsafe` / `socket`. Sent on state CHANGES only — never once per scan |
| `sensors` | + `motionIntent, motionSource, appliedPwm, mapRev, mapCached, sdOk, firmware, sensorAngleLeftDeg, sensorAngleRightDeg, gapLeftCm, gapRightCm, avoidState, avoidDir, avoidAssist, roverHalfWidthCm, emergencyStopCm` | live telemetry for the dashboard (reuses the safety scan: no extra blocking sensor reads) |
| `location`, `mission_progress`, `mission_complete`, `camera_fault`, `irrigation` | unchanged | — |

**Server → rover**

| Action | Payload | Notes |
|---|---|---|
| `field_map` | full map + `rev` | only sent when the revision differs |
| `motion_config` | `{driveSpeedPercent, turnSpeedPercent, sensorAngleLeftDeg, sensorAngleRightDeg, avoidAssist}` (speeds 0-100, angles 0-80) | also pushed on connect and whenever the panel saves |
| `set_speed` | `{percent}` or `{driveSpeedPercent, turnSpeedPercent, sensorAngleLeftDeg, sensorAngleRightDeg, avoidAssist}` | single-value speed form, clamped 0-100 |
| `get_motion_status`, `get_map_status` | — | pull the current state |
| `drive`, `stop`, `pause_patrol`, `start_patrol`, `cap_photo`, `camera_capture_burst`, `autonomous_mission*`, `field_context` | unchanged | — |

## 7. Pump project

Same single-source layout (`sketch.ino` includes `src/main.cpp`; both Arduino IDE
folders carry a copy — see §1), and each board's pins/Wi-Fi/`FW_TARGET` live in
its own `config.machine.h`. Pump logic itself is deliberately fail-safe: relay
stays OFF at boot, manual by default, pump runs only on command or in auto mode,
and the relay is released before any OTA flash starts (§8).

## 8. Over-the-air (OTA) firmware updates (new)

The boards no longer need a USB cable for every change: the operator uploads a
compiled `.bin` to `chrserver` and presses **Update**, and the board downloads it
from the same server it already talks to, flashes itself and reboots.

**In the firmware** (`include/config.h`, per board):

| Symbol | Meaning |
|---|---|
| `FW_TARGET` | Identity of the board family this build is for: `rover`, `pump-c3`, `pump-devkit`. It is reported in `device_hello` and every `ota` command must match it. |
| `OTA_ENABLED` | `1` (default) accepts updates, `0` compiles the OTA code out completely. |

**How the update runs**

1. The panel queues an image for one target and the server sends this board
   `control_command` → `{ action: "ota", data: { target, version, md5, sha256, size, path } }`.
2. The board checks `target == FW_TARGET`. A mismatch is reported as
   `ota_status: ignored` and *nothing* is downloaded — a DevKit image can never
   land on the C3, and a pump image can never land on the rover.
3. It goes safe first: the rover stops its motors and hands over to manual
   (`SRC_MANUAL`, `autonomousPaused`), the pump releases the relay. Nothing moves
   or pumps during a flash.
4. `HTTPUpdate` downloads `serverBaseUrl + path?token=<device token>` and flashes
   the spare app slot, then the board reboots into the new image
   (`rebootOnUpdate(true)`).
5. The board reports progress as `ota_status` (`starting`, then `failed` with a
   reason, or `success`), and after the reboot its `device_hello` carries the new
   `FW_VERSION`/`PUMP_FW_VERSION`. **The server only treats the update as
   finished when that new version arrives** — a device's own "success" is not
   trusted, so a board that keeps rebooting into the old build is visible instead
   of silently "updated".

**What it needs**

* A partition scheme with **two app slots** (see §2). The build refuses early
  with "not enough flash space" rather than half-flashing.
* The device token of that board (`ROBOT_TOKEN` / `PUMP_TOKEN`) — it is sent as
  `?token=`, so the download route can hand a rover token only rover images and a
  pump token only pump images.
* The server address the board already uses: the hosted `CUSTOM_SERVER_URL` or the
  LAN gateway (`CHRH_FORCE_GATEWAY_MODE 1` on the rover). Nothing about OTA needs
  internet access from the field.
* **One USB flash per board, once.** A board running an older build has no
  `FW_TARGET` and no OTA code, so the *first* update must be flashed with a
  cable. Every update after that can come over the air.

**Trying it without hardware** — the decision logic is the same code the panel
calls, and the ESP32 build is the real toolchain:

```bash
cd wokwi-esp32-project && pio run     # compiles the OTA path for the real board
cd wokwi-water-pump-c3 && pio run
```

## 9. Circuit protection in the Wokwi diagrams (new)

Two boards died with `assert failed: __esp_system_init_fn_init_flash` **before any
application code ran**, so the fix is physical and it now lives *inside*
`diagram.json` in both projects — the paper guide (`chr-robot-protection-circuits.zip`)
and the diagram say the same thing.

Nothing existing was moved. The protection parts are new parts around the old
ones, and the connections that need a part **in series** were re-drawn through it:

```
D25/D26/D27/D14/D12/D13 -> 220R x6 -> L298N EN A / IN1 / IN2 / IN3 / IN4 / EN B
D15 -> 220R -> TRIG bus (j9)
HC-SR04 ECHO x3 -> 1k -> 2k -> D32 / D23 / VN          (2k also to GND, 5V -> 3.33V)
D33 -> 33R -> SD SCK
TX0 / RX0 -> 10k -> ESP32-CAM RX / TX
BMS P+ -> 5A fuse -> SS34 -> L298N 12V + buck VIN+     (470uF + 100nF on the rail)
ultrasonic VCC (j8) -> 5V rail (j2)                    (they were on 3V3)
ESP32-CAM VCC -> ldo33 3V3 (its own AMS1117, not the ESP32 3V3 pin)
```

| Project | before | after |
|---|---|---|
| `wokwi-esp32-project` | 38 parts / 79 connections | **115 parts / 155 connections** |
| `wokwi-water-pump-c3` | 17 parts / 21 connections | **43 parts / 40 connections** |

What went in (full tables are in each project README):

* **Power:** 5A fuse, SS34 reverse-polarity diode, 470uF+100nF (12V), 1000uF+100nF
  (ESP32 VIN), 10uF+100nF (3V3), 470uF+100nF (servo), 100nF at every module.
* **Signals:** 220R x6 on the L298N inputs, 220R on TRIG, 1k/2k divider on all
  three ECHOs, **10k pull-down on GPIO12** (the MTDI flash-voltage strap that
  was wired to `IN4` — this is the actual `init_flash` cause), 10k DHT pull-up,
  4.7k I2C pull-ups, 33R on SD SCK, 10k on both camera UART lines.
* **Motors:** 1N5819 x4 freewheel diodes (`OUT1..OUT4` -> +12V) in the rover and
  1N5819 across the pump. On the real robot also add **100nF across each motor
  terminal** and bring the motor ground and the logic ground together **only at
  `BMS P-`** (star ground) — neither can be drawn in the simulator, so both are
  text notes inside the diagrams.

New custom chips, **pins only / passive** (`cap`, `ecap`, `diode`, `fuse`,
`ldo33`): `.chip.c` source, `.chip.json` pin list and a prebuilt `.chip.wasm`
sit in each project root, `wokwi.toml` maps them, `scripts/build-all.*` compiles
them, and a new checker proves everything agrees:

```bash
cd wokwi-esp32-project
python3 scripts/check-wokwi-diagram.py      # also checks ../wokwi-water-pump-c3
#   OK - diagram, chip files and wokwi.toml agree
```

The green text block on the left of each diagram is the legend of everything
that was added. Wires added by the generator carry no route on purpose: Wokwi
draws their route on screen the first time you touch the diagram.

**Flash settings that still matter** (the crash is not only wiring): Upload speed
115200, Flash Frequency **40 MHz**, Flash Mode **DIO**, 4 MB, a partition scheme
with **two app slots** (needed by §8), and Erase-all-before-upload once, plus
`esptool erase_flash`.

## 10. History

* `0001-*.patch`, `chrhw-0001-*.patch` — historical firmware patches kept for
  reference (already applied).
* Front-arc / avoidance update (2026-10-06, in the safety patch): the two side
  brackets are now modelled with their real angles (`include/arc_math.h`), the
  rover steers around plants instead of stopping at them, the angles are
  panel-adjustable, and `scripts/{check-code-copies,test-arc-math,sim-obstacle}.sh`
  were added so behaviour and code-copies are verifiable before flashing.
* Arduino IDE folders are now **self-contained** (2026-10-07): the IDE can only
  compile files inside the sketch folder, so `main.cpp`, `arc_math.h`,
  `logo_bitmap.h` and a generated `config.h` live in each `arduino-ide/<board>/`
  folder, kept in step by `scripts/sync-arduino-ide.sh` and proven byte-identical
  by `scripts/check-code-copies.sh`. Before this, opening a folder in the IDE
  failed with "No such file or directory" because the sketch included `../../`.
* Over-the-air updates (2026-10-07): `FW_TARGET` + `OTA_ENABLED` in
  `include/config.h`, the `ota` command handler with the target check and the
  stop-before-flash safety, and `ota_status` reporting (§8). The first flash of
  each board still needs a cable.
* Circuit protection inside the diagrams (2026-10-07): the two boards that died
  in `init_flash` were a physical problem (GPIO12 strap, 5V ECHO, no bulk
  capacitance, no flyback diodes, shared ground), so the fuse, SS34, all
  capacitors, the ECHO dividers, the GPIO12 pull-down, the series resistors, the
  pull-ups and the flyback diodes are now part of `diagram.json` in both
  projects, together with five new passive custom chips and
  `scripts/check-wokwi-diagram.py` (§9).
* `AUDIT_NOTES_client_server_alignment.md` — client/server alignment audit,
  with a follow-up section for this safety/cache update.
