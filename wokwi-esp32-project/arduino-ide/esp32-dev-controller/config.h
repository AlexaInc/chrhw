#pragma once
// ---------------------------------------------------------------------------
// Arduino IDE sketch folder for the REAL rover (ESP32 DevKit V1).
//
// The firmware CODE is shared with the Wokwi/PlatformIO build — this sketch
// includes ../../src/main.cpp, so the firmware is edited in ONE place and both
// builds follow. Pins, tokens, motion limits and the SD wiring all come from
// ../../include/config.h.
//
// This file holds ONLY what is specific to this machine: the Wi-Fi credentials
// and the server routing this board used before the sources were merged.
// ---------------------------------------------------------------------------
#ifdef __has_include
#if __has_include("config.local.h")
#include "config.local.h"       // optional, git-ignored, wins over everything below
#endif
#endif

#define WIFI_SSID "LAPTOP_92SEERD2_3485"
#define WIFI_PASSWORD "[8447Yq9"

// This board talks to the LAN gateway (http://<gateway-ip>:8000 / ws://...)
// instead of the hosted server — the same behaviour as before. To use the
// hosted server instead, delete the next line and uncomment the two below it.
#define CHRH_FORCE_GATEWAY_MODE 1
// #define CUSTOM_SERVER_HOST "crophealth.dpdns.org"
// #define CUSTOM_SERVER_URL  "https://crophealth.dpdns.org"

#include "../../include/config.h"
