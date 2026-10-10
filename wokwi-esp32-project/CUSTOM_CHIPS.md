# Local Wokwi custom chips

Wokwi loads each local custom part through a `[[chip]]` entry in `wokwi.toml`. Keep the manifest name aligned with a diagram type (`chip-<name>`), and keep the `.chip.json` pinout beside the `.chip.wasm` binary. Each model has its C source beside it.

| Diagram type | Pinout / source / binary | Model limitation |
|---|---|---|
| `chip-r` | `r.chip.json`, `r.chip.c`, `r.chip.wasm` | Rain amount/threshold slider; analog output limited to the simulated 0–3.3 V range. Digital output is present; the rover reads it on GPIO35 for diagnostics only. |
| `chip-gps` | `gps.chip.*` | Emits canned NMEA data; no real GPS/power behavior. |
| `chip-l98nmotorcontrl` | `l98nmotorcontrl.chip.*` | Simplified digital L298N logic; no motor current, voltage drop, temperature, stall or diode model. |
| `chip-dc-motor` | `dc-motor.chip.*` | Passive, input-only two-pin visual placeholder. Two instances are connected to OUT1/OUT2 and OUT3/OUT4. Does not model rotation, current, stall, back-EMF, brush noise or any safety behavior. |
| `chip-espcam` | `espcam.chip.*` | UART camera protocol simulator; not an ESP32-CAM CPU or power model. |
| `chip-cell3v7` | `cell3v7.chip.*` | Diagram battery cell placeholder; the voltage slider is not a real analog battery source. |
| `chip-bms3s` | `bms3s.chip.*` | Pinout/logic placeholder; does not balance cells or trip on over-current/under-voltage. |
| `chip-buck5v` | `buck5v.chip.*` | Digital rail placeholder; does not regulate voltage or limit current. Used twice in the drawing (logic and servo rails). |
| `chip-dcadapter` | `dcadapter.chip.*` | Visual adapter placeholder. |
| `chip-charger3s` | `charger3s.chip.*` | Visual charger placeholder; does not perform CC/CV charging. Never charge a real pack through the simulated chip. |
| `chip-cap` | `cap.chip.*` | Visual two-pin capacitor only; no capacitance, ESR or transient simulation. |
| `chip-ecap` | `ecap.chip.*` | Visual polarized capacitor only; no electrical capacitor model. |
| `chip-diode` | `diode.chip.*` | Visual diode only; no forward drop, reverse leakage or current limit. |
| `chip-fuse` | `fuse.chip.*` | Visual fuse only; it never opens under overload. |
| `chip-ldo33` | `ldo33.chip.*` | Digital HIGH placeholder at `3V3`; no regulator/dropout/thermal/current-limit model. Kept in the library but not used in the current diagram. |

## The DC Motor placeholder

`dc-motor.chip.json` gives the editor-visible chip name **DC Motor** and the two terminals `M+`/`M-`. `motorA` is wired across L298N OUT1/OUT2; `motorB` is wired across OUT3/OUT4. The C source declares passive input pins only; it deliberately has no motor physics or output behavior.

The included `dc-motor.chip.wasm` is a valid no-op two-pin WASM adapted from the project's existing passive two-pin chip binary by changing only its embedded pin names in-place. Node's WebAssembly validator accepts the module. A Wokwi CLI/compiler and authenticated Wokwi render/runtime check were not available for this update, so this is **not** claimed as a Wokwi-render-validated motor simulation. Recompile from `dc-motor.chip.c` with the Wokwi CLI when available and inspect the project in VS Code/Wokwi before relying on the visual result.

## Labels and visual styling in VS Code

The chip's name and terminals are editor-visible. Wokwi's documented custom-chip JSON supports `name`, `author`, `pins`, and optional `controls`/`display`, but does not define an arbitrary body-color or custom-face styling property. Keep using readable names, pin labels, diagram text and wire colors rather than unsupported JSON keys. See the [Wokwi custom-chip JSON schema](https://docs.wokwi.com/chips-api/chip-json).

## Rebuild every WASM binary

From this project folder, install Wokwi CLI and run:

```bash
for f in *.chip.c; do
  name="${f%.chip.c}"
  wokwi-cli chip compile "$f" -o "$name.chip.wasm"
done
python3 scripts/check-wokwi-diagram.py .
```

The structural checker validates custom-chip manifest paths, JSON schema keys, named pins, diagram endpoints and WebAssembly magic bytes. It cannot validate electrical behavior or guarantee the VS Code extension's Wokwi account/license is active.
