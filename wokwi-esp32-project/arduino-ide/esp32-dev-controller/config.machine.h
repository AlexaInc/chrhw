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

// This board's OTA identity: upload the rover image under the SAME target.
#define FW_TARGET "rover"

// Wi-Fi of THIS board (the machine-specific part - never committed elsewhere).
#define WIFI_SSID "LAPTOP_92SEERD2_3485"
#define WIFI_PASSWORD "[8447Yq9"

// This board talks to the LAN gateway (http://<gateway-ip>:8000 / ws://...) 
// instead of the hosted server. To use the hosted server instead, delete the
// next line and uncomment the two below it.
#define CHRH_FORCE_GATEWAY_MODE 1
// #define CUSTOM_SERVER_HOST "crophealth.dpdns.org"
// #define CUSTOM_SERVER_URL  "https://crophealth.dpdns.org"
