#pragma once
// ESP32 DevKit V1 build. Only the values that DIFFER from the shared firmware
// configuration live here; everything else comes from ../../include/config.h
// (the single source of truth).
#ifdef __has_include
#if __has_include("config.local.h")
#include "config.local.h"
#endif
#endif

#define WIFI_SSID "LAPTOP_92SEERD2_3485"
#define WIFI_PASSWORD "[8447Yq9"
#define PUMP_FW_VERSION "2026-10-03-ESP32-DEVKIT-V1"

// DevKit V1 pins (GPIO34 is ADC1 input-only - safe with Wi-Fi active).
#define RELAY_PIN 19
#define STATUS_LED_PIN 2
#define SOIL_PIN 34

#include "../../include/config.h"
