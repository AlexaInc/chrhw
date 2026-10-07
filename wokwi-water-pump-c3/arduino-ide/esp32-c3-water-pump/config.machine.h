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

// --- ESP32-C3 Super Mini (the physical pump board) -------------------------
#define PUMP_FW_VERSION "2026-10-06-ESP32-C3-SUPERMINI"

// Wi-Fi of THIS board.
#define WIFI_SSID "LAPTOP_92SEERD2_3485"
#define WIFI_PASSWORD "[8447Yq9"

// C3 Super Mini relay / LED / sensor pins.
#define RELAY_PIN 4
#define STATUS_LED_PIN 3
#define SOIL_PIN 0
