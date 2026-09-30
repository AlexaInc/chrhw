#pragma once
#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASSWORD ""
#define PUMP_FW_VERSION "2026-09-30-WOKWI-UART-V4"
#define DEVICE_ROLE "esp_c3_pump"
#define DEVICE_ID "pump-01"
#define PUMP_TOKEN "280dba3544bc64f80b7c188c6d977fa27ca02c0c79be0c098d8e6227aac9e907"
#define SERVER_PORT 8000
// Define both for direct custom-server routing. CUSTOM_SERVER_URL is retained
// for REST-capable extensions; this pump firmware currently uses Socket.IO.
#define CUSTOM_SERVER_HOST "crophealth.dpdns.org"
#define CUSTOM_SERVER_URL "https://crophealth.dpdns.org"
// Comment out both CUSTOM_SERVER_* lines to use gateway-IP port 8000 fallback.
#define RELAY_PIN 4
#define STATUS_LED_PIN 3
#define SOIL_PIN 0
#define RELAY_ACTIVE_LOW true
