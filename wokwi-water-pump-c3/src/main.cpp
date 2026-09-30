#include <Arduino.h>
#include <WiFi.h>
#include <SocketIOclient.h>
#include <ArduinoJson.h>
#include "config.h"

SocketIOclient socketIO;
bool socketConnected = false;
bool pumpOn = false;
// Fail-safe boot: never start a high-current pump before WiFi/server control is ready.
// AUTO remains available through the pump_auto command from the frontend.
bool autoMode = false;
int thresholdPercent = 35;
unsigned long pumpStopAt = 0;
unsigned long lastTelemetryAt = 0;
unsigned long lastWifiRetryAt = 0;
String activeBlockId = "";
String serverHost;
String serverBaseUrl;
uint16_t serverPort = SERVER_PORT;
bool serverSecure = false;

void setPump(bool on, unsigned long durationSeconds = 0) {
  pumpOn = on;
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_LOW ? !on : on);
  digitalWrite(STATUS_LED_PIN, on ? HIGH : LOW);
  pumpStopAt = on && durationSeconds ? millis() + durationSeconds * 1000UL : 0;
}

int soilPercent() {
  int raw = analogRead(SOIL_PIN);
  return constrain(map(raw, 4095, 1200, 0, 100), 0, 100);
}

void sendIrrigationState() {
  if (!socketConnected) return;
  DynamicJsonDocument doc(768);
  JsonArray event = doc.to<JsonArray>();
  event.add("message.upsert");
  JsonObject envelope = event.createNestedObject();
  envelope["Type"] = "irrigation";
  JsonObject message = envelope.createNestedObject("Message");
  message["deviceId"] = DEVICE_ID;
  message["pumpOn"] = pumpOn;
  message["autoMode"] = autoMode;
  message["soilMoisture"] = soilPercent();
  message["threshold"] = thresholdPercent;
  message["activeBlockId"] = activeBlockId;
  message["remainingSeconds"] = pumpStopAt > millis() ? (pumpStopAt - millis()) / 1000 : 0;
  message["lastSeen"] = millis();
  String output;
  serializeJson(doc, output);
  if (!socketIO.sendEVENT(output)) Serial.println("[PUMP] Failed to send irrigation telemetry");
}

void socketEvent(socketIOmessageType_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case sIOtype_DISCONNECT:
      socketConnected = false;
      Serial.println("[PUMP] Socket.IO disconnected; retrying automatically");
      break;
    case sIOtype_CONNECT:
      // Match the working robot sequence exactly: first join the default
      // Socket.IO namespace, then wait before transmitting any application event.
      socketIO.send(sIOtype_CONNECT, "/");
      socketConnected = true;
      lastTelemetryAt = millis();
      Serial.println("[PUMP] Socket.IO transport connected; namespace join sent");
      break;
    case sIOtype_ERROR:
      socketConnected = false;
      Serial.printf("[PUMP] Socket.IO error: %.*s\n", (int)length, payload ? (char *)payload : "");
      Serial.println("[PUMP] Check server PUMP_TOKEN against config.h PUMP_TOKEN");
      break;
    case sIOtype_EVENT: {
      if (!payload) break;
      DynamicJsonDocument doc(2048);
      DeserializationError error = deserializeJson(doc, payload, length);
      if (error) {
        Serial.printf("[PUMP] Invalid Socket.IO JSON: %s\n", error.c_str());
        break;
      }
      const char *eventName = doc[0] | "";
      if (strcmp(eventName, "control_command") != 0) break;
      JsonObject command = doc[1]["command"];
      String action = String((const char *)(command["action"] | ""));
      JsonObject data = command["data"];
      Serial.printf("[PUMP] Command: %s\n", action.c_str());

      if (action == "pump_on") {
        autoMode = false;
        activeBlockId = String((const char *)(data["blockId"] | ""));
        setPump(true, data["durationSeconds"] | 0);
      } else if (action == "pump_off" || action == "stop_irrigation" || action == "stop") {
        autoMode = false;
        setPump(false);
        activeBlockId = "";
      } else if (action == "pump_auto") {
        autoMode = data["enabled"] | false;
        if (!autoMode) setPump(false);
      } else if (action == "set_irrigation_threshold") {
        thresholdPercent = constrain((int)(data["moisturePercent"] | 35), 5, 90);
      } else if (action == "irrigate_block") {
        autoMode = false;
        activeBlockId = String((const char *)(data["blockId"] | ""));
        setPump(true, data["durationSeconds"] | 120);
      }
      sendIrrigationState();
      break;
    }
    default:
      break;
  }
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[PUMP] Connecting WiFi SSID: %s", WIFI_SSID);
  unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 30000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[PUMP] WiFi connected. IP=%s gateway=%s RSSI=%d\n",
                  WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str(), WiFi.RSSI());
  } else {
    Serial.println("[PUMP] WiFi timeout; background reconnect enabled");
  }
}

void initServerAddress() {
#if defined(CUSTOM_SERVER_HOST) && defined(CUSTOM_SERVER_URL)
  serverHost = String(CUSTOM_SERVER_HOST);
  serverBaseUrl = String(CUSTOM_SERVER_URL);
  serverHost.trim();
  serverBaseUrl.trim();
  while (serverBaseUrl.endsWith("/")) serverBaseUrl.remove(serverBaseUrl.length() - 1);
  serverSecure = serverBaseUrl.startsWith("https://");
  serverPort = serverSecure ? 443 : 80;

  int authorityStart = serverBaseUrl.indexOf("://");
  authorityStart = authorityStart >= 0 ? authorityStart + 3 : 0;
  int authorityEnd = serverBaseUrl.indexOf('/', authorityStart);
  if (authorityEnd < 0) authorityEnd = serverBaseUrl.length();
  String authority = serverBaseUrl.substring(authorityStart, authorityEnd);
  int portSeparator = authority.lastIndexOf(':');
  if (portSeparator > 0) {
    int parsedPort = authority.substring(portSeparator + 1).toInt();
    if (parsedPort > 0 && parsedPort <= 65535) serverPort = (uint16_t)parsedPort;
  }

  int hostScheme = serverHost.indexOf("://");
  if (hostScheme >= 0) serverHost = serverHost.substring(hostScheme + 3);
  int hostSlash = serverHost.indexOf('/');
  if (hostSlash >= 0) serverHost = serverHost.substring(0, hostSlash);
  int hostPort = serverHost.lastIndexOf(':');
  if (hostPort > 0) serverHost = serverHost.substring(0, hostPort);
  if (!serverHost.length()) {
    serverHost = authority;
    int separator = serverHost.lastIndexOf(':');
    if (separator > 0) serverHost = serverHost.substring(0, separator);
  }
#else
  serverHost = WiFi.gatewayIP().toString();
  serverPort = SERVER_PORT;
  serverSecure = false;
  serverBaseUrl = "http://" + serverHost + ":" + String(serverPort);
#endif
  Serial.printf("[PUMP] Server=%s://%s:%u base=%s\n", serverSecure ? "wss" : "ws",
                serverHost.c_str(), serverPort, serverBaseUrl.c_str());
}

void connectSocket() {
  String path = "/socket.io/?EIO=4&role=" + String(DEVICE_ROLE) + "&deviceId=" + String(DEVICE_ID) + "&token=" + String(PUMP_TOKEN);
  Serial.printf("[PUMP] Opening Socket.IO: %s://%s:%u/socket.io/ role=%s deviceId=%s\n",
                serverSecure ? "wss" : "ws", serverHost.c_str(), serverPort, DEVICE_ROLE, DEVICE_ID);
  if (serverSecure) socketIO.beginSSL(serverHost.c_str(), serverPort, path.c_str());
  else socketIO.begin(serverHost.c_str(), serverPort, path.c_str());
  socketIO.onEvent(socketEvent);
  socketIO.setReconnectInterval(5000);
}

void setup() {
  // RELAY_ACTIVE_LOW modules can turn on while their input floats during boot.
  // Drive the relay OFF before Serial, WiFi, delays, or sensor initialization.
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_LOW ? HIGH : LOW);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_LOW ? HIGH : LOW);
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);
  pinMode(SOIL_PIN, INPUT);
  pumpOn = false;
  pumpStopAt = 0;

  Serial.begin(115200);
  delay(500);
  Serial.printf("\n[PUMP] ESP32-C3 pump controller booting (SAFE RELAY OFF)\n[PUMP] Firmware: %s\n", PUMP_FW_VERSION);
  connectWiFi();
  initServerAddress();
  connectSocket();
}

void loop() {
  socketIO.loop();

  if (WiFi.status() != WL_CONNECTED && millis() - lastWifiRetryAt >= 5000) {
    lastWifiRetryAt = millis();
    Serial.println("[PUMP] WiFi unavailable; reconnecting");
    WiFi.reconnect();
  }

  if (pumpStopAt && (long)(millis() - pumpStopAt) >= 0) {
    setPump(false);
    activeBlockId = "";
  }
  if (autoMode) {
    int moisture = soilPercent();
    if (!pumpOn && moisture < thresholdPercent - 2) setPump(true);
    if (pumpOn && moisture >= thresholdPercent + 3) setPump(false);
  }
  if (millis() - lastTelemetryAt >= 5000) {
    lastTelemetryAt = millis();
    sendIrrigationState();
  }
}
