// ---------------------------------------------------------------------------
// GENERATED FILE - DO NOT EDIT THIS COPY.
//
// This is what the Arduino IDE compiles: the machine values of THIS board
// (config.machine.h, which you own and edit) followed by a copy of the shared
// configuration wokwi-water-pump-c3/include/config.h.
//
//   board values : config.machine.h          (edit this one)
//   shared config: wokwi-water-pump-c3/include/config.h   (edit that one)
//   then run     : bash scripts/sync-arduino-ide.sh
//
// Everything in the shared part is #ifndef-guarded, so the board values above
// always win. A config.local.h next to this file (optional, git-ignored) is
// read after the board values and before the shared defaults, so it beats both.
// ---------------------------------------------------------------------------
#pragma once

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

#ifdef __has_include
#if __has_include("config.local.h")
#include "config.local.h"
#endif
#endif

// ===========================================================================
// --- copy of wokwi-water-pump-c3/include/config.h (generated, do not edit) -------------------------
// ===========================================================================
// ===========================================================================
// CHR water-pump controller — THE configuration file.
//
// Every other config.h in this repository is an include-shim that points back
// here, so pins, Wi-Fi and tokens are edited in ONE place and every build
// target follows:
//     PlatformIO : include/config.h                    (this file)
//     Sketch     : sketch.ino -> src/main.cpp
//     Arduino IDE: arduino-ide/<board>/config.h     -> #include "../../include/config.h"
//
// Machine-specific values (real Wi-Fi, board pins) do NOT belong in this file.
// A sketch folder defines only what differs and then includes this file, or
// creates a git-ignored `config.local.h` next to the sketch. Every default
// below is #ifndef-guarded so the local value always wins.
// ===========================================================================
// (config.local.h is already included at the top of this generated file)

// --- Identity -------------------------------------------------------------
// The server-compatible identity stays `esp_c3_pump` for every board build so
// no server/client change is needed.
#ifndef DEVICE_ROLE
#define DEVICE_ROLE "esp_c3_pump"
#endif
#ifndef DEVICE_ID
#define DEVICE_ID "pump-01"
#endif
// Must match PUMP_TOKEN in the server's .env.
#ifndef PUMP_TOKEN
#define PUMP_TOKEN "280dba3544bc64f80b7c188c6d977fa27ca02c0c79be0c098d8e6227aac9e907"
#endif
#ifndef PUMP_FW_VERSION
#define PUMP_FW_VERSION "2026-10-06-shared-source"
#endif

// --- Wi-Fi & server -------------------------------------------------------
// Defaults target the Wokwi simulator. Real boards override these in their own
// sketch-folder config.h (or config.local.h).
#ifndef WIFI_SSID
#define WIFI_SSID "Wokwi-GUEST"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif
// Define BOTH to talk to the hosted server; leave both undefined to fall back
// to http://<gateway-ip>:8000.
#ifndef CUSTOM_SERVER_HOST
#define CUSTOM_SERVER_HOST "crophealth.dpdns.org"
#endif
#ifndef CUSTOM_SERVER_URL
#define CUSTOM_SERVER_URL "https://crophealth.dpdns.org"
#endif
#ifndef SERVER_PORT
#define SERVER_PORT 8000
#endif
// A build target that must talk to the local gateway instead of the hosted
// server defines CHRH_FORCE_GATEWAY_MODE before including this file; the
// firmware then falls back to http://<gateway-ip>:8000 exactly as before.
#ifdef CHRH_FORCE_GATEWAY_MODE
#undef CUSTOM_SERVER_HOST
#undef CUSTOM_SERVER_URL
#endif


// --- Pin map (ESP32 DevKit V1 default) ------------------------------------
// GPIO34 is ADC1 and keeps working while Wi-Fi is active.
#ifndef RELAY_PIN
#define RELAY_PIN 25
#endif
#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN 2
#endif
#ifndef SOIL_PIN
#define SOIL_PIN 34
#endif

// Direct active-low relay module: LOW = ON, HIGH = OFF.
#ifndef RELAY_ACTIVE_LOW
#define RELAY_ACTIVE_LOW true
#endif
