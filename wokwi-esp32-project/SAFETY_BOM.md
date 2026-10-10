# Safety-related component list (BOM)

This is the safety/power and ESP32-interface BOM for the current rover diagram, not a complete list of every sensor or mechanical part. The diagram's custom battery, BMS, buck, charger, fuse, diode, capacitor and motor chips are visual/digital placeholders; they are not electrical or safety simulations. Quantities below are counted from `diagram.json` after adding two motor placeholders.

**Status key:** “Drawn” means a symbol/model is present in the Wokwi diagram. It does not mean a real part has been selected or validated. “Not drawn / real addition” identifies hardware required or recommended beyond the drawing.

## 1. Battery, charging and regulated rails

| Qty. | Component/value shown | Purpose | Real-hardware selection notes |
|---:|---|---|---|
| 3 cells (one 3S pack) | 3.7 V nominal Li-ion cells; about 11.1 V nominal / 12.6 V full | Rover energy source. **Drawn** as three `cell3v7` models. | Use matched, compatible cells with documented continuous and pulse discharge ratings. Follow the exact BMS cell-tap order; do not infer it from the picture. |
| 1 | 3S BMS (`bms3s` model) | Cell protection/balancing placeholder. **Drawn.** | Select a BMS for the exact chemistry, cell count, charge/discharge current, balancing and protection thresholds. The custom chip does none of this. |
| 1 | Inline battery-positive fuse; `5A*` is only the diagram placeholder | Limits some wiring over-current faults. **Shown as a fuse model; real fuse not specified.** | Install close to pack positive. Size from cell/BMS limits, wire/connector ratings, and measured/known motor stall and inrush current. **Do not treat 5 A as a recommendation.** |
| 1 | Reverse-polarity stage; diagram labels an SS34 | Helps prevent damage from a reversed supply. **Drawn as a diode placeholder.** | The SS34 marking refers to a 3 A-class part and can be undersized here. Select a correctly rated part with thermal margin; a suitable MOSFET ideal-diode stage may be preferable. |
| 1 input + 1 charger | Diagram shows a 12 V adapter model feeding a 12.6 V 3S CC/CV charger model | Illustrates the charging path. **Both are visual models only.** | Use a proper 3S 12.6 V CC/CV charger at a cell-approved charge current, connected to the BMS charge port specified by its manufacturer. Use the adapter-plus-boost arrangement only if the actual charger is designed for that input. Never connect a generic adapter directly to cells/BMS. |
| 2 | Regulated 5.0 V buck rails: one logic rail and one separate servo rail | Keeps servo current transients away from the logic supply. **Both `buck5v` models are drawn.** | Each regulator needs adequate continuous and transient current, input-voltage rating and thermal margin. Measure each unloaded output before connecting boards. Do not parallel the outputs. |
| 1 recommended, not drawn | DC-rated master disconnect/removable battery connector | Makes the pack easy to isolate for service/charging. | Not represented as a switch in the diagram. Choose DC voltage and load-current ratings for the actual pack and motor transient. |
| As required, not specified | Battery, motor, servo and logic wiring/connectors | Carries pack and return currents. | Use wire/connector ratings appropriate to the worst-case current and temperature. Route motor/servo returns directly to the battery/BMS return; do not carry them through USB or thin ESP32 ground wiring. |

## 2. L298N bridge, motor outputs and reset-state control

| Qty. | Component/value shown | Purpose | Real-hardware selection notes |
|---:|---|---|---|
| 1 | L298N dual H-bridge/module | Drives two independent brushed DC motors. **Drawn as `chip3`.** | Confirm the exact module schematic, logic-supply arrangement, current/thermal limits and `5V-EN` jumper instructions. Remove the jumper only as required by that module when supplying logic 5 V externally. |
| 2 | DC motors: Motor A on OUT1/OUT2; Motor B on OUT3/OUT4 | Makes both L298N channels visible. **Drawn as two custom `DC Motor` chips, visual-only.** | Choose motors only after confirming voltage and rated/stall current against the driver, battery, BMS, fuse, regulator and wiring. The custom model does not simulate rotation, current, stall, back-EMF, brush noise or safety behavior. |
| 8 positions (4 upper + 4 lower) | `SS54*` clamp-diode placeholders, one upper and one lower clamp for each OUT1–OUT4 | Flyback paths for a **bare** L298. **All eight positions are drawn.** | Many L298N modules already have eight diodes. Inspect the actual module schematic and do not blindly add a duplicate set. Select diode current, reverse-voltage and thermal ratings using motor stall current and the driver's datasheet; `SS54*` is not a validated selection. |
| 2, not drawn | 100 nF ceramic suppression capacitor, one directly across each real brushed motor's terminals | Reduces brush-noise spikes at the motor. | Fit close to each motor with short leads; use a suitable voltage-rated ceramic. These are real-hardware additions, not simulated by the motor chips. |
| 6 | 1 kΩ series resistors on ENA, IN1, IN2, IN3, IN4 and ENB | Limits/isolates each ESP32 control signal. **Drawn.** | Confirm logic-level and resistor-power suitability for the exact board/driver. Series resistors are not level shifters or guaranteed fault protection. |
| 6 | 10 kΩ pull-downs on the same six L298N inputs | Holds the bridge inputs low during reset/startup. **Drawn.** | Verify the driver input thresholds and wiring. They reduce floating-input risk but do not make an unsafe or faulty bridge fail-safe. |
| 1 | 10 kΩ pull-down from GPIO12/MTDI to ground | Preserves the intended boot-strap state on the assumed classic ESP32/3.3 V-flash board. **Drawn.** GPIO12 is not used as a motor-control signal; L298N IN4 remains GPIO22. | Confirm the exact ESP32 module/flash voltage and strap requirements first; variants can differ. Do not treat this pull-down as universal. |

## 3. ESP32-facing signal conditioning shown

| Qty. | Component/value shown | Purpose | Important caveat |
|---:|---|---|---|
| 3 | 1 kΩ series + 2 kΩ to ground, one divider per HC-SR04 ECHO | Reduces each nominal 5 V echo before it reaches the ESP32. **Drawn for three sensors.** | The ratio gives about **3.33 V** from exactly 5.0 V. Recalculate at the module's maximum ECHO voltage and verify the exact ESP32 input/absolute-maximum limits; this nominal value is not a safety certification and may need a safer divider ratio or level shifter. |
| 1 | 220 Ω series resistor on the shared HC-SR04 TRIG line | Signal conditioning. **Drawn.** | GPIO15 is a boot-strap pin; this resistor does not remove strap-related boot behavior. |
| 2 | 470 Ω series resistors on camera UART RX/TX | Reduces contention/current on the UART lines. **Drawn.** | Not a level shifter and not a guarantee against 5 V. Disconnect the camera UART from GPIO1/GPIO3 while USB flashing. |
| 1 | 33 Ω series resistor on microSD SCK | Signal-edge conditioning. **Drawn.** | Not an over-voltage protector. |
| 1 | 10 kΩ DHT22 data pull-up to 3.3 V | Sets the sensor data line high. **Drawn.** | Check whether the real sensor/module already has a pull-up before adding another. |
| 2 | 4.7 kΩ I²C pull-ups (SDA and SCL) to 3.3 V | Defines the I²C bus idle state. **Drawn.** | Check OLED/other modules for onboard pull-ups; parallel resistors change the effective value. |
| 0 shown; add if needed | Rain-module AO/DO level shifting or dividers | Keeps GPIO34/AO and GPIO35/DO within ESP32 limits if a real rain board needs 5 V. | The diagram powers its custom rain model at 3.3 V; real modules vary. If a module must run at 5 V, level-shift **both** outputs. If DO is open-collector, provide a correctly placed pull-up to 3.3 V as required by that module. |

## 4. Rail bulk and local bypass capacitors

All capacitor chips below are **visual placeholders**; the diagram does not simulate capacitance, ESR, ripple or transients.

| Qty. | Value | Placement shown | Real-hardware caveat |
|---:|---|---|---|
| 1 | 470 µF electrolytic + 100 nF ceramic | 12 V / motor-supply rail | Select voltage, ripple-current and temperature ratings for the maximum pack voltage and transients; observe electrolytic polarity. |
| 1 | 1000 µF electrolytic + 100 nF ceramic | Logic 5 V rail | Choose voltage/ripple/temperature ratings for the rail and load transients. |
| 1 | 10 µF electrolytic + 100 nF ceramic | 3.3 V rail | Confirm polarity and regulator/board requirements. |
| 1 | 470 µF electrolytic + 100 nF ceramic | Separate servo 5 V rail | Capacitance does not replace a regulator rated for servo stall current. |
| 10 | 100 nF ceramics | Local bypass at 3 HC-SR04 modules, OLED, microSD, DHT22, rain module, GPS module, ESP32-CAM and ESP32 DevKit | Fit close to each module's supply/ground pins where needed; check for existing onboard bypass. Choose voltage and dielectric ratings for the rail and environment. |
| **14 total drawn** | **100 nF ceramics** | Four rail capacitors plus ten module-local capacitors above | These are separate from the two motor-terminal suppression capacitors, which are **not drawn**. |

## Before buying or wiring

The diagram does not identify the exact ESP32 module, L298N board, cells/BMS, motors, regulator modules, rain board, wire gauge or connector. Verify their datasheets and ratings, measure the regulated rails before attaching electronics, check for onboard diodes/pull-ups, and have the real wiring inspected before powering another board. A Wokwi pass is not evidence of hardware safety.
