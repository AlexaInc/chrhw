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
