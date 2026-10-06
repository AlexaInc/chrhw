// ---------------------------------------------------------------------------
// CHR-01 rover — Arduino IDE sketch.
//
// The firmware source of truth is ../../../src/main.cpp (the file PlatformIO
// compiles). This sketch includes it, so editing the firmware in ONE place
// updates the Wokwi simulation build and this Arduino IDE build together.
//
// Board settings (Arduino IDE):
//   Board            : "ESP32 Dev Module"
//   Partition Scheme : "Default 4MB with spiffs" (or any with >= 1.3MB app)
//   Upload Speed     : 921600
//   Monitor Speed    : 921600
//
// Configuration: pins/Wi-Fi/tokens/limits come from ../../../include/config.h.
// Put machine-specific values in a `config.local.h` file next to THIS sketch.
// ---------------------------------------------------------------------------
#include "../../src/main.cpp"
