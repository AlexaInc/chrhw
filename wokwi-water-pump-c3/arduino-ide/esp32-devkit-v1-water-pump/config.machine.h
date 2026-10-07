#pragma once
// ---------------------------------------------------------------------------
// THIS file is yours: the machine values of THIS board only.
//
// Everything else (pins you did not override, tokens, speed limits, the SD
// wiring, GPS fallback, ...) comes from the shared config, which
// scripts/sync-arduino-ide.sh copies into ./config.h together with this file.
// Anything you define here wins over the shared value (all of them are
// #ifndef-guarded), and a git-ignored ./config.local.h still beats both.
//
// After editing this file run:   bash scripts/sync-arduino-ide.sh
// ---------------------------------------------------------------------------

// --- ESP32 DevKit V1 (the bench/testing pump board) ------------------------
#define PUMP_FW_VERSION "2026-10-03-ESP32-DEVKIT-V1"
// This board's OTA identity: upload the DevKit image under the SAME target.
#define FW_TARGET "pump-devkit"

// Wi-Fi of THIS board.
#define WIFI_SSID "LAPTOP_92SEERD2_3485"
#define WIFI_PASSWORD "[8447Yq9"

// DevKit V1 pins (GPIO34 is ADC1 input-only - safe with Wi-Fi active).
#define RELAY_PIN 19
#define STATUS_LED_PIN 2
#define SOIL_PIN 34
