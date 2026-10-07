#pragma once
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
#ifdef __has_include
#if __has_include("config.local.h")
#include "config.local.h"
#endif
#endif

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

// --- Over-the-air (OTA) firmware updates ----------------------------------
// Same chain as the rover: the admin panel queues an image, the board downloads
// it from the server it already talks to, flashes it and reboots (the relay is
// released before the flash starts, so the pump is off the whole time).
//
// FW_TARGET names the board the image was built for. The two pump boards have
// different pins, so they must NOT share images: the C3 Super Mini and the
// DevKit V1 builds override this below with "pump-c3" / "pump-devkit". The
// generic "pump" default matches nothing on the server, which keeps a build
// without board-specific values from ever being pushed to a board.
#ifndef FW_TARGET
#define FW_TARGET "pump"
#endif
#ifndef OTA_ENABLED
#define OTA_ENABLED 1
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
