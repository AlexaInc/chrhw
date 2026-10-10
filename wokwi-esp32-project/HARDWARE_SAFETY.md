# Hardware safety notes — read before building the real rover

**This document and `diagram.json` are a conservative design review, not a certified schematic.** The exact ESP32 board revision, ESP32 module/flash type, battery cells/BMS, motor stall current, regulator modules and wiring gauges were not supplied. Do not treat the drawing as proof that a particular board is safe. See [`SAFETY_BOM.md`](SAFETY_BOM.md) for the grouped, diagram-counted component list and its real-hardware additions/placeholders.

## What the project changed

1. **GPIO12 is no longer an L298N input.** `IN4_PIN` is now GPIO22. GPIO12/MTDI is left unused by the application and shown with a 10 kΩ pull-down for the assumed classic ESP32 DevKit with 3.3 V flash. GPIO12 is sampled at reset on classic ESP32 and can affect the flash supply selection; a wrong strap can cause boot/flash failure. It is not, by itself, proof that the USB-UART programmer IC burned. Check the exact module: some ESP32-WROVER variants use 1.8 V flash and have different GPIO12 requirements.
2. **No direct 5 V sensor output is intended to reach an ESP32 GPIO.** The HC-SR04 ECHO lines have a 1 kΩ series resistor and a 2 kΩ resistor to ground, yielding about 3.33 V from a nominal 5.0 V ECHO signal. That is only a nominal calculation, not a guaranteed safe margin: check the maximum real ECHO voltage and exact ESP32 input/absolute-maximum limits, and use a safer divider ratio or level shifter if needed. The rain module is drawn on 3.3 V; AO goes to GPIO34 and DO goes to input-only GPIO35. The firmware still derives wetness and the existing `rainDrop`/`isRaining` fields from AO; DO is sampled for diagnostics only. Real AO and DO levels must stay within the exact ESP32 limits.
3. **L298N inputs default low.** The drawing uses 1 kΩ series resistors and 10 kΩ pull-downs for ENA, ENB and IN1–IN4. This reduces floating-input surprises at reset; it is not a substitute for checking the driver's logic limits or preventing an incorrectly wired 5 V/12 V output from reaching a GPIO.
4. **Servo rail is separated.** A second regulated 5.0 V buck feeds the servo; the logic buck feeds the ESP32 VIN/5 V rail, L298N logic supply, HC-SR04 sensors and the camera input shown. The DHT22 and OLED use the ESP32 3.3 V/common-ground pins; the ESP32 ground is tied to BMS P-, while motor-driver and servo returns go directly to the BMS return. On real hardware, keep the low-current logic return separate from the short, heavier motor/servo return path.
5. **UART0 is treated as a programming interface.** The camera UART has 470 Ω series resistors in the drawing, but unplug the camera RX/TX wires from GPIO1/GPIO3 before USB flashing. Series resistors do not make a shared UART safe against every contention case.
6. **L298N flyback protection is drawn as eight diode positions.** A bare L298 needs clamp diodes for both directions of current at each of its four outputs. Many L298N *modules* already include eight diodes. Inspect the exact module and its schematic; do not blindly parallel another set. Select diode current/voltage/thermal ratings from the motor stall current and driver datasheet. The `SS54*` text in Wokwi is a placeholder, not a confirmed part selection.
7. **Two DC Motor chips are now attached to the L298N outputs.** Motor A is connected across OUT1/OUT2; Motor B is connected across OUT3/OUT4. These custom chips are visual-only, passive placeholders: they do not model rotation, current draw, stall, back-EMF, brush noise or hardware safety. Their presence is not a motor simulation or real-motor validation.

## Real power wiring (verify every part marking)

- For a 3-series Li-ion pack, nominal voltage is about 11.1 V and full-charge voltage is 12.6 V. Connect cell taps to the exact BMS pads in the order specified by that BMS manufacturer. A wrong tap order can damage the BMS or cells.
- Put an appropriately rated fuse close to pack positive. The diagram's `5A*` is **not a recommendation**: size the fuse for the cells, BMS, wire gauge, connector and motor stall/inrush current. A fuse protects wiring against some over-current faults; it cannot make a miswired circuit safe.
- The reverse-polarity stage must be rated for the maximum current and heat. An SS34 is a 3 A part and can be undersized for a motor system; a correctly rated MOSFET ideal-diode stage is often preferable. The drawn diode is only a symbol/placeholder.
- Use each buck converter with adequate **continuous and transient** current, thermal margin and input-voltage rating. With the load disconnected, adjust and measure each output with a multimeter before connecting the ESP32. The main board's `VIN`/`5V` pin must receive regulated 5.0 V, never the raw 3S pack (up to 12.6 V).
- The ESP32-CAM power pad is marked `VCC` in the custom Wokwi chip and is connected to regulated 5 V in the drawing. On a real board, read the silkscreen and datasheet: feed its `5V` pin if it has an onboard regulator; do not feed 5 V to a `3V3` pin.
- Remove the L298N module's `5V-EN` jumper if the module instructions require that when supplying logic 5 V externally. Do not connect two regulator outputs together.
- The servo must use its own suitably rated 5 V supply. A 470 µF + 100 nF local capacitor is shown, but capacitance does not replace a supply rated for servo stall current.
- Add the shown local decoupling close to modules. For the two real brushed motors represented in the diagram, add one 100 nF ceramic directly across each motor's terminals, with short leads, to reduce brush noise. These two capacitors are real-hardware additions and are not drawn in `diagram.json`.
- A common signal ground is required, but high motor currents should return to the battery/BMS negative through a short, heavier path—not through the ESP32, USB cable or thin sensor-ground wiring.

## Rain-sensor voltage

The simulator chip is configured for 0–3.3 V output; AO feeds GPIO34 and the chip's active-high DO feeds input-only GPIO35. Firmware uses AO for wetness and samples DO for diagnostics; the existing telemetry remains AO-derived. Power the real rain module from 3.3 V **only if its exact module supports it**. If it requires 5 V, do not connect AO or DO directly to the ESP32: level-shift every output so neither pin exceeds the exact board limit, then calibrate AO against the actual module. GPIO35 has no internal pull-up, so an open-collector DO needs an external pull-up to 3.3 V. Real modules may make DO active-low or use a different threshold polarity; verify the module schematic/datasheet before relying on the diagnostic reading. GPIOs are not 5 V tolerant.

## Charging

The battery/charger custom chips are illustrative digital models, not working chargers or BMS protection simulations. Use a suitable, reputable **3S 12.6 V CC/CV charger** at a current allowed by the cell pack. Connect it only to the BMS charge port specified by that BMS manual (some boards have a separate C- terminal). Never use a generic router adapter directly as a cell charger, never bypass the BMS, and do not charge an unknown/mismatched pack unattended.

## USB flashing and preventing another board failure

- Do not connect USB and an external 5 V/battery-fed board at the same time unless that exact DevKit has documented power-path isolation. Back-feeding the USB VBUS rail can damage a computer port, USB-UART bridge or regulator.
- For uploads, remove the battery and disconnect motor power, the camera UART, and peripherals attached to boot-strap pins. This project uses GPIO2/GPIO5 for the SD interface and GPIO15 for ultrasonic TRIG; unplug the SD card/peripheral wires if bootloader entry is unreliable.
- If the USB-UART IC (for example, a CP2102/CH340-family device) is physically damaged, firmware cannot repair it. Repair/replace the bridge or use a known-good **3.3 V logic** USB-to-UART adapter: adapter TX -> ESP32 RX0/GPIO3, adapter RX -> ESP32 TX0/GPIO1, and common GND. Do not connect the adapter's 5 V/VCC pin to the ESP32 while another supply is present; often leave VCC disconnected entirely.
- To enter the ROM downloader on a typical DevKit: hold BOOT (GPIO0 low), pulse EN/RESET, start the upload, then release BOOT when the connection begins. Follow the exact board instructions.
- If the serial port appears but esptool cannot identify/read the target flash after all external wiring is removed and boot straps are correct, the ESP32 module or flash may be damaged. An external programmer does not fix a physically failed flash chip.
- Stop immediately if a board gets hot, smells, smokes, or the 3.3 V rail is outside its specified range. Do not keep retrying power.

## What Wokwi does not prove

The custom `cell3v7`, `bms3s`, `buck5v`, `dcadapter`, `charger3s`, `fuse`, `diode`, `cap`, `ecap`, `ldo33` and `dc-motor` chips are primarily visual/digital representations. The two motor chips are passive input-only placeholders; they do not model rotation, motor current, stall, back-EMF or brush noise. Wokwi's custom-chip model does not reproduce the real cell voltages, BMS trip thresholds, fuse melting, diode drops, resistor heating, capacitor ESR, regulator current limit, motor inrush, wire resistance or USB power-path behavior. A green simulation is not evidence that those protections work in hardware.

Before finalizing real values, collect the exact DevKit/module model, BMS model, battery/cell data, motor rated/stall current and regulator specs. Have someone experienced inspect the wiring and measure it before connecting another ESP32.
