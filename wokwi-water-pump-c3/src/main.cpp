#include <Arduino.h>
#include <WiFi.h>
#include <SocketIOclient.h>
#include <ArduinoJson.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
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
  // This 5 V relay module is active-low: LOW energizes, HIGH releases.
  // Keep the levels explicit so a configuration mismatch cannot invert ON/OFF.
  const uint8_t relayLevel = on ? LOW : HIGH;
  digitalWrite(RELAY_PIN, relayLevel);
  pumpOn = on;
  digitalWrite(STATUS_LED_PIN, on ? HIGH : LOW);
  pumpStopAt = on && durationSeconds ? millis() + durationSeconds * 1000UL : 0;
  Serial.printf("[PUMP] Relay requested=%s GPIO%d=%s readback=%d\n",
                on ? "ON" : "OFF", RELAY_PIN,
                relayLevel == LOW ? "LOW" : "HIGH", digitalRead(RELAY_PIN));
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

/**
 * Tell the server who this board is. The admin panel needs two things it cannot
 * guess: which firmware is running (to show "update available") and FW_TARGET
 * (to send THIS board the image built for it - the C3 and the DevKit have
 * different pins, so they must never share a .bin).
 */
void sendDeviceHello() {
  if (!socketConnected) return;
  DynamicJsonDocument doc(512);
  JsonArray event = doc.to<JsonArray>();
  event.add("device_hello");
  JsonObject hello = event.createNestedObject();
  hello["deviceId"] = DEVICE_ID;
  hello["role"] = DEVICE_ROLE;
  hello["firmware"] = PUMP_FW_VERSION;
  hello["fwTarget"] = FW_TARGET;
  hello["pumpOn"] = pumpOn;
  hello["autoMode"] = autoMode;
  hello["threshold"] = thresholdPercent;
  String output;
  serializeJson(doc, output);
  if (!socketIO.sendEVENT(output)) Serial.println("[PUMP] Failed to send hello");
  Serial.printf("[PUMP] Hello sent (firmware=%s, target=%s)\n", PUMP_FW_VERSION, FW_TARGET);
}

// ---------------------------------------------------------------------------
// OTA firmware update (admin panel -> chrserver -> this board)
//
// Same chain as the rover: the panel queues an image, the server sends the `ota`
// command and serves the file itself. This board only ever accepts an image for
// its own FW_TARGET (a DevKit image must never land on the C3, and the other way
// round), and it releases the relay BEFORE the download so the pump is off while
// flashing - after the reboot the relay stays off until the server says
// otherwise, exactly like a normal boot.
// ---------------------------------------------------------------------------
#if OTA_ENABLED
static String otaVersion = "";

void sendOtaStatus(const char *status, const char *reason = "", int percent = -1) {
  Serial.printf("[PUMP][OTA] %s%s%s\n", status, reason && *reason ? " - " : "", reason ? reason : "");
  if (!socketConnected) return;
  DynamicJsonDocument doc(384);
  JsonArray event = doc.to<JsonArray>();
  event.add("ota_status");
  JsonObject message = event.createNestedObject();
  message["deviceId"] = DEVICE_ID;
  message["target"] = FW_TARGET;
  message["version"] = otaVersion;
  message["status"] = status;
  if (percent >= 0) message["percent"] = percent;
  if (reason && *reason) message["reason"] = reason;
  String output;
  serializeJson(doc, output);
  socketIO.sendEVENT(output);
}

bool applyOtaUpdate(const String &version, const String &path, const String &md5, long size) {
  if (WiFi.status() != WL_CONNECTED) {
    sendOtaStatus("failed", "no Wi-Fi");
    return false;
  }
  if (path.length() == 0) {
    sendOtaStatus("failed", "the command carried no download path");
    return false;
  }
  const String url = serverBaseUrl + path;
  if (!url.startsWith("http")) {
    sendOtaStatus("failed", "no server address yet");
    return false;
  }

  // Pump OFF and stay off across the reboot: flashing with a live relay is the
  // one thing this must never do.
  autoMode = false;
  setPump(false);
  activeBlockId = "";
  delay(200);

  if (size > 0 && (long)ESP.getFreeSketchSpace() < size) {
    sendOtaStatus("failed", "not enough flash space - use a partition scheme with two app slots");
    return false;
  }

  otaVersion = version;
  sendOtaStatus("starting", "");

  httpUpdate.rebootOnUpdate(true);
#ifdef STATUS_LED_PIN
  httpUpdate.setLedPin(STATUS_LED_PIN, HIGH);
#else
  httpUpdate.setLedPin(-1);
#endif
  httpUpdate.onStart([]() { sendOtaStatus("downloading", ""); });
  // onEnd takes no argument in this core version - onError reports failures.
  httpUpdate.onEnd([]() { Serial.println("[PUMP][OTA] download complete, flashing"); });

  Serial.printf("[PUMP][OTA] downloading %s (running %s, requested %s, md5 %s)\n", url.c_str(), PUMP_FW_VERSION,
                version.length() ? version.c_str() : "?");
  // The board authenticates the download with its own device token. It goes in
  // the URL because the HTTPUpdate API in this core version takes no headers.
  String downloadUrl = url;
  downloadUrl += downloadUrl.indexOf('?') >= 0 ? "&" : "?";
  downloadUrl += "token=";
  downloadUrl += PUMP_TOKEN;

  if (serverSecure) {
    WiFiClientSecure client;
    client.setInsecure(); // the tunnel certificate is not pinned in the firmware
    switch (httpUpdate.update(client, downloadUrl, PUMP_FW_VERSION)) {
      case HTTP_UPDATE_FAILED:
        sendOtaStatus("failed", httpUpdate.getLastErrorString().c_str());
        return false;
      case HTTP_UPDATE_NO_UPDATES:
        sendOtaStatus("success", "already up to date");
        return false;
      default:
        break;
    }
  } else {
    WiFiClient client;
    switch (httpUpdate.update(client, downloadUrl, PUMP_FW_VERSION)) {
      case HTTP_UPDATE_FAILED:
        sendOtaStatus("failed", httpUpdate.getLastErrorString().c_str());
        return false;
      case HTTP_UPDATE_NO_UPDATES:
        sendOtaStatus("success", "already up to date");
        return false;
      default:
        break;
    }
  }
  sendOtaStatus("success", "flashed, rebooting");
  return true;
}
#endif

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
      sendDeviceHello();
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

      if (action == "ota") {
        // Firmware update from the admin panel - only for THIS board's target.
#if OTA_ENABLED
        String otaTarget = String((const char *)(data["target"] | ""));
        String otaRequested = String((const char *)(data["version"] | ""));
        String otaPath = String((const char *)(data["path"] | ""));
        String otaMd5 = String((const char *)(data["md5"] | ""));
        long otaSize = data["size"] | 0L;
        Serial.printf("[PUMP][OTA] Command target=%s version=%s (this board: %s)\n", otaTarget.c_str(), otaRequested.c_str(), FW_TARGET);
        if (otaTarget != FW_TARGET) {
          otaVersion = otaRequested;
          sendOtaStatus("ignored", "target mismatch - this image is for another board");
        } else if (otaRequested == PUMP_FW_VERSION) {
          otaVersion = otaRequested;
          sendOtaStatus("success", "already running this version");
        } else {
          applyOtaUpdate(otaRequested, otaPath, otaMd5, otaSize);
        }
#else
        Serial.println("[PUMP][OTA] Ignored: OTA is disabled in this build");
#endif
      } else if (action == "pump_on") {
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
  digitalWrite(RELAY_PIN, HIGH);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);
  pinMode(SOIL_PIN, INPUT);
  pumpOn = false;
  pumpStopAt = 0;

  Serial.begin(115200);
  delay(500);
  Serial.printf("\n[PUMP] ESP32 DevKit V1 pump controller booting (SAFE RELAY OFF)\n[PUMP] Firmware: %s\n", PUMP_FW_VERSION);
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
