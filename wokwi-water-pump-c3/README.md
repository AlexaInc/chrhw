# ESP32 DevKit V1 Water Pump Controller

This is the ESP32 DevKit V1 version of the existing pump controller. It keeps
the server-compatible identity `esp_c3_pump` so no server/client change is
required.

## Pin wiring

| ESP32 DevKit V1 | Device |
|---|---|
| GPIO25 | Relay module IN |
| GPIO34 | Soil sensor analog AO |
| GPIO2 | Status LED (on-board LED where available) |
| 5V/VIN | Regulated 5.0 V from pump-controller buck |
| GND | Relay GND, sensor GND and buck OUT- common ground |

GPIO34 is intentionally used because it is an ADC1 pin and therefore works
while ESP32 Wi-Fi is active. Never apply more than 3.3 V to GPIO34.

## Pump power wiring

```text
12 V supply + -> fuse -> relay COM
relay NO       -> pump +
pump -         -> 12 V supply -
relay NC       -> not connected

12 V supply +  -> 5 V buck VIN+
12 V supply -  -> 5 V buck VIN-
buck OUT+ 5.0V -> ESP32 VIN/5V and relay VCC
buck OUT-      -> ESP32 GND, relay GND and sensor GND
```

Do not power the pump from the ESP32 or its 5 V pin. Confirm the buck output is
5.0 V before connecting the ESP32. Add a fuse sized for the pump and wiring.
Keep water away from exposed electronics and mains wiring.

## Relay module

The firmware defaults to an active-low relay (`RELAY_ACTIVE_LOW true`). If the
relay turns on when it should be off, set this to `false` in `config.h` and
upload again. When GPIO25 controls relay IN, remove any unrelated input jumper;
do not remove relay-board coil/optocoupler jumpers unless its own manual calls
for separate supplies.

## Soil sensor

- Sensor VCC: use 3.3 V if its AO can otherwise exceed 3.3 V.
- Sensor AO -> GPIO34.
- Sensor GND -> common GND.
- Calibrate the raw wet/dry values in `soilPercent()` for the real sensor and soil.

## Arduino IDE

Open:

`arduino-ide/esp32-devkit-v1-water-pump/esp32-devkit-v1-water-pump.ino`

Keep `config.h` in the same folder. Select:

- Board: **DOIT ESP32 DEVKIT V1** (or **ESP32 Dev Module**)
- Upload speed: 115200 or 921600
- Serial Monitor: 115200

Install:

- WebSockets by Links2004
- ArduinoJson 6.x

Edit `WIFI_SSID`, `WIFI_PASSWORD`, server settings and `PUMP_TOKEN` in
`config.h` before uploading. The token must match the server `PUMP_TOKEN`.

## Direct active-low relay mode

This build follows the tested module behavior directly: GPIO25 LOW turns the
relay ON and GPIO25 HIGH turns it OFF. `RELAY_ACTIVE_LOW` is `true`, and the
firmware also uses explicit LOW/ON and HIGH/OFF output levels.

```text
GPIO25 -> relay IN
5V      -> relay VCC
GND     -> relay GND
```

The relay IN terminal was observed around 4.4 V when released. ESP32 GPIO is
not 5-V tolerant, so a transistor/3.3-V-compatible relay module remains the
recommended permanent interface. Do not use the direct connection if 4.4 V is
present at the ESP32 end of the GPIO25 wire.
