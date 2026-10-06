#pragma once

#define WIFI_SSID "LAPTOP_92SEERD2_3485"
#define WIFI_PASSWORD "[8447Yq9"
#define PUMP_FW_VERSION "2026-10-03-ESP32-DEVKIT-V1"

// Must match the pump identity/token configured on the server.
#define DEVICE_ROLE "esp_c3_pump"
#define DEVICE_ID "pump-01"
#define PUMP_TOKEN "280dba3544bc64f80b7c188c6d977fa27ca02c0c79be0c098d8e6227aac9e907"

#define SERVER_PORT 8000
#define CUSTOM_SERVER_HOST "crophealth.dpdns.org"
#define CUSTOM_SERVER_URL "https://crophealth.dpdns.org"

// ESP32 DevKit V1 safe pin assignment.
// GPIO34 is ADC1 input-only and remains usable while Wi-Fi is active.
#define RELAY_PIN 19
#define STATUS_LED_PIN 2
#define SOIL_PIN 34

// Direct active-low relay module: LOW=ON, HIGH=OFF.
#define RELAY_ACTIVE_LOW true
