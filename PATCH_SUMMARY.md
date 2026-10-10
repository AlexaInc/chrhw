# Protected ESP32 Wokwi update — package notes

This is a local patched copy of `AlexaInc/chrhw`; nothing has been pushed to GitHub. Open `wokwi-esp32-project.code-workspace` in VS Code, or open the `wokwi-esp32-project` folder directly.

## Current revision

- Removed rover soil-moisture sensing and telemetry. The separate `wokwi-water-pump-c3` project retains its soil sensor and was not changed by this rover update.
- Restored both rain-module outputs: AO -> ESP32 GPIO34 and DO -> input-only GPIO35. AO continues to drive `rainDrop` / `isRaining` telemetry; DO is sampled for comparator diagnostics only. GPIO22 remains L298N IN4 and is not reused for DO.
- Retained the six L298N input protections (1 kΩ series + 10 kΩ pull-down per input), moved GPIO12 off the motor bridge, and routed the resistor-bank input traces orthogonally through clear gaps. Shared ground/supply rails remain acceptable.
- Kept the existing `chrserver` / `chrclient` message contract, firmware target, and rain field names; no backend/client code change is needed.
- Updated the project safety, custom-chip, flashing, and setup notes. The diagram is a conservative design aid, not a certified real-hardware schematic.

## Compatibility checked

The existing telemetry remains `Type: "sensors"`, with AO-derived `rainDrop` and `isRaining`; pump soil moisture remains a separate irrigation message. `DEVICE_ROLE="esp_32"` and `FW_TARGET="rover"` are unchanged.

## Validation on 2026-10-07

- PlatformIO ESP32 rover build: **success** — RAM 61,696 / 327,680 bytes (18.8%); flash 1,122,849 / 1,310,720 bytes (85.7%). The committed `firmware/firmware.bin` and `.elf` match the build output.
- Main Wokwi structural check: **pass** — 118 parts, 165 connections, 13 custom-chip types; chip manifests, JSON pinouts, WASM headers and `wokwi.toml` agree.
- Water-pump Wokwi structural check: **pass** — 43 parts, 40 connections, 5 custom-chip types.
- Explicit ESP32-to-resistor wire endpoints were checked against the Wokwi pin geometry; GPIO22's right-side route is accounted for.
- Firmware copy checks: **pass**; the separate pump's committed firmware binary is older than its source, but the pump project was intentionally left unchanged.
- Front-arc unit tests: **26 passed, 0 failed**. `git diff --check`: **pass**.
- No authenticated Wokwi render/lint or physical-board test was performed in this follow-up: `WOKWI_CLI_TOKEN` was not available. The cause of the previously damaged ESP32 boards remains unconfirmed.

## Hardware caution

The custom BMS, cells, charger, buck, fuse, diode and capacitor chips are visual/digital placeholders and do not reproduce real current, voltage, cutoff or thermal limits. Verify the exact board/module and datasheets, measure each rail before connection, and inspect the full notes in `wokwi-esp32-project/HARDWARE_SAFETY.md` before powering hardware.
