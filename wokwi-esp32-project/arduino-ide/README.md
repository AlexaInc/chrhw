# Arduino IDE upload sketches (two physical boards)

## 1) ESP32 DevKit controller

Open:

`esp32-dev-controller/esp32-dev-controller.ino`

`config.h` must remain in the same Arduino sketch folder/tab.

Arduino IDE settings:

- Board package: **esp32 by Espressif Systems**
- Board: **ESP32 Dev Module**
- Flash Size: **4MB**
- Partition Scheme: **Default 4MB with spiffs**
- Upload Speed: **460800** or **115200** if upload is unstable

Install these libraries using Library Manager:

- **WebSockets** by Markus Sattler / Links2004
- **ArduinoJson 6.x** by Benoit Blanchon
- **Adafruit GFX Library**
- **Adafruit SSD1306**
- **DHT sensor library** by Adafruit
- **Adafruit Unified Sensor**
- **TinyGPSPlus** by Mikal Hart

The controller parses GPS fixes and receives the current field-block context from the server. Image uploads use the mapped block's `plant` and `blockId`; `potato` is no longer hardcoded. Autonomous scans still use mission/block context, but a **manual `cap_photo` is never blocked**: it works without GPS or a mapped block, and the server resolves (or simply stores) the photo. `logo_bitmap.h` must remain in the same Arduino sketch folder/tab together with `config.h` — it holds the CropHealth logo used by the OLED boot animation and live status screen.

`WiFi`, `Wire`, `SPI`, `SD`, and `HTTPClient` come with the ESP32 board package.

## 2) AI Thinker ESP32-CAM

Open:

`esp32-cam-uart/esp32-cam-uart.ino`

Arduino IDE settings:

- Board: **AI Thinker ESP32-CAM**
- Upload Speed: **115200**
- Partition Scheme: **Huge APP (3MB No OTA/1MB SPIFFS)** (Default also fits)
- PSRAM: **Enabled**, if the menu is shown

No third-party camera library is required: `esp_camera.h` comes with the Espressif ESP32 board package.

### ESP32-CAM upload with an FTDI adapter

- FTDI 5V -> ESP32-CAM 5V
- FTDI GND -> ESP32-CAM GND
- FTDI TX -> ESP32-CAM U0R / GPIO3
- FTDI RX -> ESP32-CAM U0T / GPIO1
- ESP32-CAM GPIO0 -> GND **only while flashing**
- Press RESET, upload, then remove GPIO0-GND and reset again.

Use a stable 5V supply. Do not try to run the camera from a weak 3.3V output.

## Runtime UART wiring between boards

| ESP32 DevKit | ESP32-CAM |
|---|---|
| TX0 / GPIO1 | U0R / GPIO3 |
| RX0 / GPIO3 | U0T / GPIO1 |
| GND | GND |

Both use **921600 baud**. The DevKit sends a standalone `C`; the camera replies with:

`<IMG:jpeg-size>` followed immediately by the raw JPEG bytes.

The ESP32-CAM sketch deliberately sends no debug text over UART, because the same UART carries binary JPEG data. The DevKit's UART0 is also its USB serial port, so the Serial Monitor may show binary garbage during a photo transfer; that is expected.

## Important flashing rule

Disconnect the TX0/RX0 wires between the two boards while uploading either sketch. Reconnect them crossed as shown above after both uploads finish. Otherwise the other board or the USB-UART adapter can interfere with flashing.

## Server selection

In the DevKit sketch's `config.h`:

```cpp
#define CUSTOM_SERVER_HOST "crophealth.dpdns.org"
#define CUSTOM_SERVER_URL  "https://crophealth.dpdns.org"
```

With both lines defined, REST uses HTTPS and Socket.IO uses WSS on port 443. Comment out/delete **both** lines to use the original gateway fallback at gateway IP port 8000.

## Camera "Timeout: No response from ESP32-CAM on TX0/RX0" checklist

The DevKit triggers the CAM with a standalone `C` on UART0 and expects
`<IMG:size>` + JPEG back. The firmware now retries 3 times (25 ms idle gap
before each `C`), and the CAM answers `<IMG:0>` when its camera failed to
initialize. If you still get a timeout, it is a hardware/link problem:

1. **Power**: the ESP32-CAM needs a stable **5V** supply (300 mA+ spikes) and a
   **common GND** with the DevKit. Brownouts = silent CAM.
2. **Wiring (crossed)**: CAM `U0T (GPIO1)` → DevKit `RX0 (GPIO3)`, CAM
   `U0R (GPIO3)` → DevKit `TX0 (GPIO1)`.
3. **GPIO0 must NOT be grounded** on the CAM at runtime — grounded GPIO0 keeps
   it in flash mode and the sketch never runs.
4. **Disconnect the USB serial monitor** while capturing: UART0 is shared with
   the USB bridge, and an open monitor/board can corrupt the CAM's reply bytes.
5. Both sketches use **921600 baud** — keep them matched.
