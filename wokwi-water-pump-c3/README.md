# ESP32-C3 SuperMini Water Pump — external power, no USB

This project starts from the user's latest Wokwi project `476516556808428545`. Its existing parts, positions, and wire routes are preserved. Only the external buck-converter section and related wiring/text were appended.

## Exact physical controller

Physical board: **ESP32-C3 SuperMini 4MB** from the supplied datasheet.

Wokwi does not provide that exact compact PCB, so simulation uses `board-esp32-c3-devkitm-1`. It has the same ESP32-C3 architecture and the firmware uses GPIO numbers that exist on the SuperMini:

- GPIO4 -> relay IN
- GPIO3 -> status LED
- GPIO0 -> soil-moisture analog signal
- 5V/GND -> external regulated board power

## USB-free power wiring

A **second 12V-to-5V buck converter is required** for the pump controller. One physical buck cannot power both the separate robot and pump assemblies.

Router adapter/load and buck input:

```text
Router adapter +12V -> fuse -> relay COM
Router adapter +12V -> pump-system buck VIN+
Router adapter GND  -> pump negative
Router adapter GND  -> pump-system buck VIN-
Relay NO            -> pump positive
Relay NC            -> unconnected
```

Buck and logic:

```text
Buck OUT+ adjusted to 5.0V -> ESP32-C3 SuperMini 5V
Buck OUT+ adjusted to 5.0V -> relay VCC
Buck OUT-                  -> ESP32-C3 GND
Buck OUT-                  -> relay GND
ESP32-C3 GPIO4             -> relay IN
```

Before connecting the C3, measure and adjust buck output to 5.0V. The datasheet says external 3.3–6V may be applied at the 5V pin and warns that USB and external power must not be connected simultaneously. Therefore disconnect external 5V before plugging in USB for firmware upload.

The JQC3F-05VDC-C marking confirms a 5V relay coil, but not the module trigger polarity. Firmware defaults to active-low (`RELAY_ACTIVE_LOW true`). Change it to `false` if physical testing shows inverted operation.

## Arduino IDE upload

Open:

`arduino-ide/esp32-c3-water-pump/esp32-c3-water-pump.ino`

Keep `config.h` in the same sketch folder. Settings:

- Board: ESP32C3 Dev Module
- Flash: 4MB
- USB CDC On Boot: Enabled
- Upload mode: USB

Install WebSockets by Links2004 and ArduinoJson 6.x. Upload using USB with external 5V disconnected. After upload, unplug USB, connect regulated external 5V, and press RESET.

## Server identity

- role: `esp_c3_pump`
- device id: `pump-01`
- `PUMP_TOKEN` must match server `.env` `PUMP_TOKEN`

## Safety

Verify the old router adapter output is 12V DC, verify polarity, and ensure its current rating meets the pump startup current. Add a fuse. Never put 12V on any ESP32-C3 pin. Keep water away from exposed mains adapters and electronics.

## ESP32-C3 SuperMini serial monitor

Wokwi and the physical SuperMini need different Serial routing. The default `wokwi` PlatformIO environment keeps `Serial` on TX/RX because the diagram connects those pins to `$serialMonitor`; enabling USB CDC here causes only ROM boot text to appear. The separate `supermini-usb` environment enables `ARDUINO_USB_MODE=1` and `ARDUINO_USB_CDC_ON_BOOT=1` for a physical board's native USB. Use `pio run -e wokwi` for Wokwi and `pio run -e supermini-usb -t upload` for the physical board. In Arduino IDE select **ESP32C3 Dev Module**, **USB CDC On Boot: Enabled**, and **USB Mode: Hardware CDC and JTAG**. For normal external-power operation, do not connect USB simultaneously; use a 3.3V UART adapter on TX/RX if logs are needed.

## Pump operating modes

The C3 supports explicit `pump_on`, `pump_off`, and `pump_auto` commands. Manual ON/OFF disables automatic control; AUTO uses the configured moisture threshold with hysteresis. The pump has its own `PUMP_TOKEN`, separate from the robot credential. `CUSTOM_SERVER_HOST` and `CUSTOM_SERVER_URL` are configured in `config.h`; comment both out to restore gateway-IP port-8000 fallback.
