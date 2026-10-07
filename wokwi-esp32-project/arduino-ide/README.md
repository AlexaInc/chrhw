# Arduino IDE upload sketches (two physical boards)

> **These folders are SELF-CONTAINED — nothing is included from outside.**
> The Arduino IDE can only compile files that live inside the sketch folder, so
> `esp32-dev-controller/` holds the firmware itself (`main.cpp`, a byte-identical
> copy of `../../src/main.cpp`), the headers it needs (`arc_math.h`,
> `logo_bitmap.h`, `config.h`) and your own machine values
> (`config.machine.h`: Wi-Fi, pins).
>
> * **To change the firmware**: edit `wokwi-esp32-project/src/main.cpp`, then run
>   `bash scripts/sync-arduino-ide.sh` (it refreshes every Arduino IDE folder).
> * **To change this board's Wi-Fi / pins**: edit
>   `arduino-ide/esp32-dev-controller/config.machine.h`, then run the same script
>   (or just re-open the sketch — only `config.h` is rebuilt from it).
> * **Proof that nothing drifted**: `bash scripts/check-code-copies.sh` compares
>   the copies against their sources byte for byte.
>
> Open the folder (not a single file) in the Arduino IDE: `main.cpp` is compiled
> automatically, and the `.ino` tab only documents the board settings.

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

No third-party camera library is required: `esp_camera.h` and `img_converters.h` come with the Espressif ESP32 board package. This sketch captures **RGB565** (not camera hardware JPEG), then uses `frame2jpg()` to software-encode the UART/upload JPEG. It uses QVGA with PSRAM and QQVGA without PSRAM.

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

`<IMG:jpeg-size>` followed immediately by software-encoded JPEG bytes.

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

## OLED camera check at boot

The controller now requests one real frame before Wi-Fi setup. The OLED displays **Camera connected** only after the complete JPEG arrives; otherwise it displays **Camera NOT ready**. The live screen continues to show `CAM OK` or `CAM ERR` next to GPS status. This verifies camera initialization, RGB565-to-JPEG encoding, and the crossed UART link rather than showing a hardcoded connection message.

### Reading `<IMG:0>` diagnostics

With the current sketches, a failed capture is followed by a reason such as
`<CAMERR:INIT:0x105>`. The controller prints it as `reason=INIT:0x105`:

- `INIT` — `esp_camera_init()` could not detect/start the sensor. `0x105`
  (`ESP_ERR_NOT_FOUND`) normally means ribbon orientation/contact, wrong camera
  pin map, failed sensor, or inadequate 5V power. This cannot be repaired in
  software.
- `CAPTURE` — initialization succeeded, but `esp_camera_fb_get()` returned no
  frame. Check power stability/XCLK/ribbon.
- `ENCODE` — RGB565 was captured but software JPEG conversion failed, usually
  because of insufficient free memory. Ensure PSRAM is enabled and detected.
- `no diagnostic` — the ESP32-CAM is still running an older sketch; upload the
  updated `esp32-cam-uart.ino` to the camera board as well as the controller.

The standalone `C` visible immediately before a controller debug line is the
intentional UART capture command echoed by the DevKit USB serial connection; it
is not itself an error.

### `ENCODE:0x101` correction

`frame2jpg()` required a second contiguous JPEG output allocation and failed on
the physical board. The UART camera now uses `frame2jpg_cb()` like the working
CameraWebServer example. It encodes the same frame twice: a count-only pass to
produce `<IMG:size>`, followed by a chunked pass written directly to UART. This
avoids allocating a complete JPEG buffer alongside the RGB565 frame.
