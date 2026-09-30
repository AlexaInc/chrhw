#ifndef CONFIG_H
#define CONFIG_H

// --- Pin Definitions  ---
#define DHTPIN 19
#define DHTTYPE DHT22

#define ULTRASONIC_TRIG_PIN 15
#define ECHO_FORWARD_PIN    32
#define ECHO_LEFT_PIN       23
#define ECHO_RIGHT_PIN      39
#define CAMERA_SERVO 4

#define RAIN_DIGITAL_PIN    22
#define RAIN_ANALOG_PIN     34
#define SOIL_ANALOG_PIN     35

#define ENA_PIN             25
#define IN1_PIN             26
#define IN2_PIN             27
#define IN3_PIN             14
#define IN4_PIN             12
#define ENB_PIN             13

// WiFi & Server Credentials
#define WIFI_SSID "LAPTOP_92SEERD2_3485"
#define WIFI_PASSWORD "[8447Yq9"

// Define BOTH lines to send REST and Socket.IO traffic directly to this server.
// CUSTOM_SERVER_URL is a BASE URL, not one endpoint such as /auth/login.
// HTTPS automatically selects port 443 and WSS. An explicit port also works,
// for example: "http://example.com:8000".
// #define CUSTOM_SERVER_HOST "crophealth.dpdns.org"
// #define CUSTOM_SERVER_URL  "https://crophealth.dpdns.org"

// Comment out/delete BOTH CUSTOM_SERVER_* lines above to restore the original
// fallback: http://<gateway-ip>:8000 and ws://<gateway-ip>:8000/socket.io/
#define SERVER_PORT 8000
#define DEVICE_ROLE "esp_32"
#define DEVICE_ID "robot-01"
#define ROBOT_TOKEN "4408dc8d907853dcb3c3cd3e9714e78f8a92a5509755d25cd6e561dd2c78d9d6"

#endif