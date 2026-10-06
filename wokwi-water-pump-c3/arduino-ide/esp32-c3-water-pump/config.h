#pragma once
// ESP32-C3 Super Mini build. Only the values that DIFFER from the shared
// firmware configuration live here; everything else comes from
// ../../include/config.h (the single source of truth).
#ifdef __has_include
#if __has_include("config.local.h")
#include "config.local.h"
#endif
#endif

#define WIFI_SSID "LAPTOP_92SEERD2_3485"
#define WIFI_PASSWORD "[8447Yq9"
#define PUMP_FW_VERSION "2026-10-06-ESP32-C3-SUPERMINI"

// C3 Super Mini relay/LED/sensor pins.
#define RELAY_PIN 4
#define STATUS_LED_PIN 3
#define SOIL_PIN 0

#include "../../include/config.h"
