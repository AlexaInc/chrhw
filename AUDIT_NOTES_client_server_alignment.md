# Firmware audit note (client/server alignment pass)

This note documents the outcome of auditing `chrhw` (ESP32-S3 rover +
ESP32-C3 water-pump controller) as part of a full audit-and-rewrite pass
across `chrclient` (mobile/web app) and `chrserver` (backend), whose goal
was to make sure the client never shows a capability the robot cannot
actually perform, and that every real capability is wired end-to-end.

**Conclusion: no firmware changes were required.**

What was verified against `wokwi-esp32-project/src/main.cpp` and
`wokwi-water-pump-c3/src/main.cpp`:

- The rover's only real data sources are GPS, temperature, the raindrop
  sensor, and the camera (photos) — confirmed no soil-moisture sensor,
  no battery/telemetry reporting, and no IMU/wheel-encoder speed
  feedback exist on this hardware build. The client was rewritten to
  never display soil moisture or "pump output" as if it came from the
  robot; that reading is only ever shown as sourced from the irrigation
  pump's own sensor.
- `drive` only reads `data.direction` — any extra fields (e.g. a speed
  value) are ignored by the firmware, so the client's manual-drive
  throttle slider was removed as a non-functional control rather than
  requiring a firmware change.
- `cap_photo` / `camera_capture_burst` both trigger the same
  `captureBothSides()` routine (always two photos) — the client's
  capture buttons were aligned to this exact behavior.
- `change_mode` / `manual_teleop` are intentionally **not** firmware
  commands. The server (`chrserver`) already translates them onto the
  primitives the firmware genuinely understands (`pause_patrol` /
  `start_patrol`), which is exactly what "manual driving" means on this
  hardware. No change needed here, in the server, or in the firmware.
- Every other action the rewritten client can send
  (`field_context`, `autonomous_mission`, `pause_patrol`,
  `start_patrol`, `stop`/`return_to_base`) maps 1:1 to a real,
  already-implemented firmware branch. Unknown actions already no-op
  safely server- and firmware-side.
- Pump actions used by the client (`pump_on`, `pump_off`,
  `stop_irrigation`, `pump_auto`, `set_irrigation_threshold`,
  `irrigate_block`) all match `wokwi-water-pump-c3/src/main.cpp`
  one-to-one; no change needed there either.

No source files in this repository were modified by this pass.


---

## Follow-up (2026-10-06): motion limits, SD cache, single firmware source

The "no firmware changes were required" conclusion above was about *matching
what the client showed*, and it still holds for the telemetry surface. The
2026-10-06 update adds capabilities the client and server now genuinely use, so
this note is extended rather than replaced:

- **Speed limits** — `set_speed` / `motion_config` are now real commands
  (`FleetConfig.driveSpeedPercent`, `turnSpeedPercent`, clamped 0-100 by the
  server and again by `MOTION_HARD_MAX_PWM` in the firmware). The dashboard
  shows the limit next to the PWM the rover reports it is actually using.
- **Field-map cache** — the rover stores the map on its SD card with a content
  revision and reports it in `device_hello`; the server only re-sends the map
  when the revision differs (legacy firmware still receives it after 2.5 s).
  `map_status` + `RobotStatus.fieldMap` expose *IN SYNC / NOT SYNCED*.
- **Safety telemetry** — `RobotStatus.motion` carries `appliedPwm`, `intent`,
  `source`, `blockedBy` (`obstacle` / `failsafe`) and `obstacleStopCm`, and an
  obstacle stop or a dead-man failsafe cut raises a real alert.
- **One source per project** — `sketch.ino` and the Arduino IDE sketches now
  `#include src/main.cpp`; configuration lives in `include/config.h`. The
  follow-up (front-arc update) adds `include/arc_math.h` next to it, so the
  geometry/decision maths is shared by the firmware AND by the PC unit test
  (`scripts/test-arc-math.sh`) instead of being duplicated.
- **Front-arc avoidance** — the rover no longer stops at every plant: it steers
  to the wider side, creeps past and (in autonomous mode) returns to the mission
  heading. The server/client side is purely informational: `motion_config`
  reasons `avoiding` / `creep` / `turn-back` / `clear` / `no-path` /
  `emergency`, `blockedBy` `plant-left` / `plant-right` / `plant-ahead`, and
  `avoidState`, `avoidDir`, `gapLeftCm`, `gapRightCm`, `frontCm`,
  `sensorAngleLeftDeg`, `sensorAngleRightDeg`, `avoidAssist` on both
  `motion_config` and the `sensors` telemetry. Alerts: `no-path` and
  `emergency` are warnings, `avoiding` / `creep` are info, and the firmware
  reports state CHANGES only so the alert stream stays quiet.
- **Sensor angles are settings** — `FleetConfig.sensorAngleLeftDeg`,
  `sensorAngleRightDeg` (clamped 0-80) and `avoidAssist` (bool) travel with the
  speed limits on `apply_config` / `set_speed` / `motion_config`, are persisted,
  and are pushed to the rover on connect.

Nothing in this update fabricates a reading the hardware cannot produce: every
new field is either a command echo or a value the firmware measured itself.
