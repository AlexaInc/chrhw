# chrhw — CropHealth hardware firmware

Two Wokwi-designed projects that run on real ESP32 boards and talk to
`chrserver` over Socket.IO:

| Project | Board | Role | Device id |
|---|---|---|---|
| `wokwi-esp32-project` | ESP32 DevKit V1 (+ ESP32-CAM over UART) | `esp_32` | `robot-01` |
| `wokwi-water-pump-c3` | ESP32-C3 Super Mini / DevKit V1 | `esp_c3_pump` | `pump-01` |

---

## 1. One firmware source per project (no more drifting copies)

The same firmware used to exist as several hand-copied files
(`sketch.ino`, `src/main.cpp`, `arduino-ide/<board>/*.ino`, …) and every edit
had to be repeated in each copy. That is gone:

```
wokwi-esp32-project/
├── src/main.cpp                                  <-- THE firmware (edit here)
├── include/config.h                              <-- THE configuration (edit here)
├── config.h, logo_bitmap.h                       -> include-shims
├── sketch.ino                                    -> #include "src/main.cpp"
└── arduino-ide/esp32-dev-controller/
    ├── esp32-dev-controller.ino                  -> #include "../../src/main.cpp"
    ├── config.h                                  -> this board's Wi-Fi + server routing
    └── logo_bitmap.h                             -> shim
```

* **Firmware code** lives in `src/main.cpp` only. `sketch.ino` (Wokwi/Arduino)
  and `arduino-ide/<board>/*.ino` (real boards) `#include` it.
* **Pins, tokens, limits and the SD wiring** live in `include/config.h` only.
  Every value is `#ifndef`-guarded, so a build target can override just what
  differs (see `include/config.local.h.example` — `config.local.h` is
  git-ignored, so credentials stay out of the repository).
* The Arduino IDE sketch folders keep only their machine-specific overrides
  (Wi-Fi credentials, and for the rover `CHRH_FORCE_GATEWAY_MODE 1`, i.e. the
  original LAN `http://<gateway-ip>:8000` fallback).
* `./scripts/check-code-copies.sh` verifies all of that after every edit: every
  sketch must include the one source, no sketch may carry firmware code of its
  own, every board config must include `include/config.h`, tokens/Wi-Fi must not
  be duplicated, and it warns when `firmware/firmware.bin` is older than the
  source (Wokwi runs the binary, not your source).
* Because the Arduino IDE sketch compiles that same `src/main.cpp`, the firmware
  you upload to the real board and the firmware the simulator runs are the same
  code — verified by building both paths: PlatformIO/Wokwi and the
  `arduino-ide/esp32-dev-controller` sketch land on exactly the same size
  (RAM 18.7 %, flash 83.9 %, 1100181 bytes).

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

Real hardware: open the matching `arduino-ide/<board>/` sketch in the Arduino
IDE and upload — it compiles the same `src/main.cpp` you just edited.

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

Same single-source layout (`sketch.ino` and both Arduino IDE sketches include
`src/main.cpp`; only the per-board pins/Wi-Fi/token live in the sketch-folder
`config.h`). Pump logic itself is unchanged — it is deliberately fail-safe:
relay stays OFF at boot, manual by default, pump runs only on command or in
auto mode.

## 8. History

* `0001-*.patch`, `chrhw-0001-*.patch` — historical firmware patches kept for
  reference (already applied).
* Front-arc / avoidance update (2026-10-06, in the safety patch): the two side
  brackets are now modelled with their real angles (`include/arc_math.h`), the
  rover steers around plants instead of stopping at them, the angles are
  panel-adjustable, and `scripts/{check-code-copies,test-arc-math,sim-obstacle}.sh`
  were added so behaviour and code-copies are verifiable before flashing.
* `AUDIT_NOTES_client_server_alignment.md` — client/server alignment audit,
  with a follow-up section for this safety/cache update.
