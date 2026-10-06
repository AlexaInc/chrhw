#include <WiFi.h>
#include <WebSocketsClient.h>
#include <SocketIOclient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "DHT.h"
#include "config.h"
#include "logo_bitmap.h"
#include <HTTPClient.h>
#include <TinyGPSPlus.h>
#include <ESP32Servo.h>

DHT dht(DHTPIN, DHTTYPE);
SocketIOclient socketIO;
TinyGPSPlus gps;

String currentBlockId = "";
String currentBlockName = "";
String currentPlant = "";
double currentLatitude = 0;
double currentLongitude = 0;
unsigned long lastLocationMillis = 0;

Servo cameraServo;
static const int MAX_WAYPOINTS = 512;
struct MissionWaypoint { double latitude; double longitude; bool scan; int index; };
MissionWaypoint missionWaypoints[MAX_WAYPOINTS];
int missionWaypointCount = 0;
int currentWaypointIndex = 0;
String activeMissionId = "";
int activePatrolId = 0;
bool autonomousActive = false;
bool autonomousPaused = false;
bool missionTransferActive = false;
bool missionTransferStartPaused = true;
int missionTransferExpected = 0;
String missionTransferId = "";
float missionArrivalRadiusM = 2.0;
long latestFrontCm = 400, latestLeftCm = 400, latestRightCm = 400;
unsigned long lastNavSensorAt = 0;

// Full camera sweep: centre=90°, then approximately 90° to either side.
const int CAMERA_CENTER_ANGLE = 90;
// Physical bracket is mirrored: lower PWM angle looks right, higher looks left.
const int CAMERA_RIGHT_ANGLE = 0;
const int CAMERA_LEFT_ANGLE = 180;

// Non-blocking obstacle bypass. The three ultrasonic sensors choose the clearer
// side; the rover turns out, passes the obstacle, turns back, then resumes GPS.
enum AvoidancePhase { AVOID_NONE, AVOID_TURN_OUT, AVOID_PASS, AVOID_TURN_BACK };
AvoidancePhase avoidancePhase = AVOID_NONE;
unsigned long avoidanceDeadline = 0;
String avoidanceTurn = "LEFT";

// OLED Display setup — JMD0.96D-1 0.96" 128x64 (SSD1306, I2C: SDA=21, SCL=18)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
bool oledOk = false;

unsigned long lastSensorMillis = 0;
unsigned long lastWifiCheckMillis = 0;
unsigned long lastDisplayMillis = 0;
const unsigned long SENSOR_INTERVAL = 5000;
const unsigned long WIFI_CHECK_INTERVAL = 5000;
const unsigned long DISPLAY_INTERVAL = 1000;
bool captureInProgress = false;
bool cameraConnected = false;   // verified by receiving a complete frame
bool cameraStatusKnown = false;
unsigned long camFaultUntil = 0; // OLED shows CAM ERROR until this time

volatile bool socketConnected = false;

// Server configuration is selected at boot:
// 1) If BOTH CUSTOM_SERVER_HOST and CUSTOM_SERVER_URL are defined, use them.
// 2) Otherwise, fall back to http://<gateway-ip>:SERVER_PORT.
String serverHost = "";
String serverBaseUrl = "";
uint16_t serverPort = SERVER_PORT;
bool serverSecure = false;

// Function prototypes
void connectWiFi();
void initServerAddress();
void startSocketIO();
void sendSensorData();
void sendLocationData();
void controlMotors(String action);
long readUltrasonic(int echoPin, String sensorName);
void captureAndUploadImage(const String &side = "manual", int scanPoint = -1);
void loadAutonomousMission(JsonObject data);
void beginAutonomousMissionTransfer(JsonObject data);
void appendAutonomousMissionChunk(JsonObject data);
void finishAutonomousMissionTransfer(JsonObject data);
void navigateMission();
bool handleObstacleAvoidance();
void captureBothSides(int scanPoint);
void sendMissionUpdate(const char *type, const char *state, const String &message = "");
void playBootAnimation();
void showBootStage(const char *caption, int progressPct);
void updateStatusDisplay(bool force = false);
void sendCameraFault(const String &reason);
bool probeCameraConnection();
String readCameraDiagnostic();

// ---------------------------------------------------------------------------
// OLED UI — logo boot animation + live status screen
// ---------------------------------------------------------------------------

// Logo reveal + brand line. Runs once right after the OLED is initialized.
void playBootAnimation() {
    if (!oledOk) return;
    const int logoX = (SCREEN_WIDTH - LOGO_BIG_W) / 2;
    const int logoY = 2;

    // Phase 1: the logo "grows" upwards from the soil line, like a sprout.
    for (int reveal = 4; reveal <= LOGO_BIG_H; reveal += 4) {
        display.clearDisplay();
        display.drawFastHLine(0, logoY + LOGO_BIG_H + 1, SCREEN_WIDTH, SSD1306_WHITE);
        display.drawBitmap(logoX, logoY + (LOGO_BIG_H - reveal), LOGO_BIG,
                           LOGO_BIG_W, LOGO_BIG_H, SSD1306_WHITE);
        // Hide everything above the reveal window so it rises from the ground.
        display.fillRect(0, 0, SCREEN_WIDTH, logoY + (LOGO_BIG_H - reveal), SSD1306_BLACK);
        display.display();
        delay(45);
    }

    // Phase 2: radar-style pulse rings around the fully grown logo.
    for (int r = 26; r <= 62; r += 9) {
        display.clearDisplay();
        display.drawBitmap(logoX, logoY, LOGO_BIG, LOGO_BIG_W, LOGO_BIG_H, SSD1306_WHITE);
        display.drawCircle(SCREEN_WIDTH / 2, logoY + LOGO_BIG_H / 2, r, SSD1306_WHITE);
        display.display();
        delay(70);
    }

    // Phase 3: brand line typed out under the logo.
    const char *brand = "CropHealth Robot";
    display.clearDisplay();
    display.drawBitmap(logoX, logoY, LOGO_BIG, LOGO_BIG_W, LOGO_BIG_H, SSD1306_WHITE);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    for (size_t i = 1; i <= strlen(brand); i++) {
        display.fillRect(0, 54, SCREEN_WIDTH, 10, SSD1306_BLACK);
        display.setCursor((SCREEN_WIDTH - (int)strlen(brand) * 6) / 2, 55);
        for (size_t c = 0; c < i; c++) display.print(brand[c]);
        display.display();
        delay(28);
    }
    delay(350);
}

// Boot progress screen: small logo + current stage + progress bar.
void showBootStage(const char *caption, int progressPct) {
    if (!oledOk) return;
    display.clearDisplay();
    display.drawBitmap(0, 0, LOGO_SMALL, LOGO_SMALL_W, LOGO_SMALL_H, SSD1306_WHITE);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(20, 0);
    display.print("CropHealth Robot");
    display.setCursor(20, 9);
    display.print(DEVICE_ID);
    display.drawFastHLine(0, 19, SCREEN_WIDTH, SSD1306_WHITE);
    display.setCursor(0, 28);
    display.print(caption);
    display.drawRect(0, 46, SCREEN_WIDTH, 10, SSD1306_WHITE);
    int fillW = constrain(progressPct, 0, 100) * (SCREEN_WIDTH - 4) / 100;
    display.fillRect(2, 48, fillW, 6, SSD1306_WHITE);
    display.setCursor(0, 57);
    display.printf("%d%%", constrain(progressPct, 0, 100));
    display.display();
}

// Live status screen: replaces the old, never-updated "Initializing..." text.
// Shows WiFi / WebSocket / GPS state and the current drive mode at 1 Hz.
void updateStatusDisplay(bool force) {
    if (!oledOk) return;
    if (!force && millis() - lastDisplayMillis < DISPLAY_INTERVAL) return;
    lastDisplayMillis = millis();

    bool wifiUp = WiFi.status() == WL_CONNECTED;
    bool gpsFix = gps.location.isValid() && currentLatitude != 0 && currentLongitude != 0;

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Header: logo + device id + heartbeat tick.
    display.drawBitmap(0, 0, LOGO_SMALL, LOGO_SMALL_W, LOGO_SMALL_H, SSD1306_WHITE);
    display.setCursor(20, 0);
    display.print("CropHealth Robot");
    display.setCursor(20, 9);
    display.print(DEVICE_ID);
    if ((millis() / 1000) % 2 == 0) display.fillCircle(124, 3, 2, SSD1306_WHITE);
    display.drawFastHLine(0, 19, SCREEN_WIDTH, SSD1306_WHITE);

    // WiFi row.
    display.setCursor(0, 23);
    if (wifiUp) display.printf("WiFi OK %s", WiFi.localIP().toString().c_str());
    else display.print("WiFi CONNECTING...");

    // WebSocket row.
    display.setCursor(0, 33);
    display.printf("WS   %s", socketConnected ? "CONNECTED" : "OFFLINE");

    // GPS + camera row. Camera state is based on a real boot-time frame,
    // not merely on the UART pins being configured.
    display.setCursor(0, 43);
    if (gpsFix) display.printf("GPS FIX %d ", (int)(gps.satellites.isValid() ? gps.satellites.value() : 0));
    else display.print("GPS NO FIX ");
    if (!cameraStatusKnown) display.print("CAM ?");
    else display.print(cameraConnected ? "CAM OK" : "CAM ERR");

    // Mode / activity row.
    display.setCursor(0, 53);
    if (captureInProgress) display.print("CAPTURING PHOTO...");
    else if (millis() < camFaultUntil) display.print("CAM ERROR! CHECK CAM");
    else if (autonomousActive && !autonomousPaused)
        display.printf("AUTO WP %d/%d", currentWaypointIndex + 1, missionWaypointCount);
    else if (autonomousActive && autonomousPaused) display.print("AUTO PAUSED");
    else {
        display.print("MANUAL");
        if (currentBlockName.length() > 0) display.printf(" %s", currentBlockName.c_str());
    }
    display.display();
}

void socketIOEvent(socketIOmessageType_t type, uint8_t *payload, size_t length) {
    switch (type) {
        case sIOtype_DISCONNECT:
            socketConnected = false;
            Serial.printf("\n[DEBUG] [IOc] ❌ Socket.IO DISCONNECTED (WiFi=%s, freeHeap=%u)\n",
                          WiFi.status() == WL_CONNECTED ? "connected" : "disconnected",
                          ESP.getFreeHeap());
            break;

        case sIOtype_CONNECT:
            Serial.println("\n[DEBUG] [IOc] ✔️ Socket.IO CONNECTED successfully!");
            socketIO.send(sIOtype_CONNECT, "/");
            socketConnected = true;
            break;

        case sIOtype_EVENT: {
            if (payload == nullptr) break;
            Serial.printf("[DEBUG] [IOc] 📥 Received Event Payload: %s\n", payload);
            
            // Do not reserve a fixed 96 KB for every Socket.IO event. That
            // temporary allocation starved the WiFi/WebSocket stack when the
            // server sent field_map immediately after connecting.
            const size_t jsonCapacity = min((size_t)49152,
                                            max((size_t)8192, length + (size_t)8192));
            DynamicJsonDocument doc(jsonCapacity);
            DeserializationError err = deserializeJson(doc, payload, length);
            if (err) {
                Serial.print("[DEBUG] [JSON] ❌ Deserialization failed: ");
                Serial.println(err.c_str());
                break;
            }

            const char *eventName = doc[0];
            if (eventName != nullptr) {
                Serial.printf("[DEBUG] [SocketEvent] Event Name: %s\n", eventName);
                if (strcmp(eventName, "control_command") == 0) {
                    const char *action = doc[1]["command"]["action"] | "(none)";
                    Serial.print("[DEBUG] [CMD] 🎮 Action extracted: ");
                    Serial.println(action);
                    JsonVariant data = doc[1]["command"]["data"];
                    if (strcmp(action, "field_map") == 0) {
                        // field_map is configuration sent automatically at
                        // connection time, not a motor command. Mission
                        // waypoints arrive separately as autonomous_mission.
                        const char *mapName = data["name"] | "(unnamed)";
                        const size_t blockCount = data["blocks"].is<JsonArray>()
                                                  ? data["blocks"].size() : 0;
                        Serial.printf("[DEBUG] [MAP] Field map loaded: %s (%u blocks)\n",
                                      mapName, (unsigned)blockCount);
                    } else if (strcmp(action, "field_context") == 0) {
                        if (data.isNull()) {
                            currentBlockId = "";
                            currentBlockName = "";
                            currentPlant = "";
                        } else {
                            currentBlockId = String((const char *)(data["blockId"] | ""));
                            currentBlockName = String((const char *)(data["blockName"] | ""));
                            currentPlant = String((const char *)(data["plant"] | ""));
                        }
                        Serial.printf("[DEBUG] [MAP] Block=%s Plant=%s\n", currentBlockId.c_str(), currentPlant.c_str());
                    } else if (strcmp(action, "autonomous_mission_begin") == 0) {
                        beginAutonomousMissionTransfer(data.as<JsonObject>());
                    } else if (strcmp(action, "autonomous_mission_chunk") == 0) {
                        appendAutonomousMissionChunk(data.as<JsonObject>());
                    } else if (strcmp(action, "autonomous_mission_end") == 0) {
                        finishAutonomousMissionTransfer(data.as<JsonObject>());
                    } else if (strcmp(action, "autonomous_mission") == 0) {
                        // Backward compatibility for small missions from an
                        // older server. New servers use bounded chunks.
                        loadAutonomousMission(data.as<JsonObject>());
                    } else if (strcmp(action, "pause_patrol") == 0) {
                        autonomousPaused = true;
                        controlMotors("STOP");
                        sendMissionUpdate("mission_progress", "paused", "Paused by operator");
                    } else if (strcmp(action, "start_patrol") == 0) {
                        autonomousPaused = false;
                        if (missionWaypointCount > 0) autonomousActive = true;
                        sendMissionUpdate("mission_progress", "running", "Resumed by operator");
                    } else if (strcmp(action, "cap_photo") == 0 || strcmp(action, "camera_capture_burst") == 0) {
                        if (strcmp(action, "camera_capture_burst") == 0 || (autonomousActive && !autonomousPaused)) {
                            // Burst captures physical left and right photos. In
                            // manual mode the active server patrol still groups
                            // both photos into the selected block collection.
                            captureBothSides(autonomousActive ? currentWaypointIndex : -1);
                        } else {
                            Serial.println("[DEBUG] [CAM] Manual capture requested by operator.");
                            controlMotors("STOP");
                            cameraServo.write(CAMERA_CENTER_ANGLE);
                            delay(300);
                            captureAndUploadImage("manual", -1);
                        }
                    } else if (strcmp(action, "drive") == 0) {
                        autonomousPaused = true; // manual command always overrides autonomous drive
                        String direction = String((const char *)(data["direction"] | "stop"));
                        direction.toUpperCase();
                        controlMotors(direction);
                    } else if (strcmp(action, "stop") == 0 || strcmp(action, "return_to_base") == 0) {
                        autonomousPaused = true;
                        controlMotors("STOP");
                    } else {
                        String motorAction(action);
                        motorAction.toUpperCase();
                        controlMotors(motorAction);
                    }
                }
            }
            break;
        }
        default:
            Serial.printf("[DEBUG] [IOc] ℹ️ Unhandled Socket type: %d\n", type);
            break;
    }
}

void setup() {
    // High-speed UART setup for UART0 (TX0 / RX0) to ESP32-CAM
    Serial.begin(921600);
    Serial.setTimeout(8000); // Allow enough time for an ESP32-CAM JPEG frame
    Serial2.begin(9600, SERIAL_8N1, 16, 17); // GPS on UART2 (TX=17, RX=16)
    delay(1000);

    Serial.println("\n========================================");
    Serial.println("   🌱 ESP32 AGRI ROBOT BOOTING UP...    ");
    Serial.println("========================================");

    // Initialize Pins
    pinMode(RAIN_DIGITAL_PIN, INPUT);
    pinMode(ULTRASONIC_TRIG_PIN, OUTPUT);
    pinMode(ECHO_FORWARD_PIN, INPUT);
    pinMode(ECHO_LEFT_PIN, INPUT);
    pinMode(ECHO_RIGHT_PIN, INPUT);

    pinMode(ENA_PIN, OUTPUT);
    pinMode(IN1_PIN, OUTPUT);
    pinMode(IN2_PIN, OUTPUT);
    pinMode(IN3_PIN, OUTPUT);
    pinMode(IN4_PIN, OUTPUT);
    pinMode(ENB_PIN, OUTPUT);
    cameraServo.setPeriodHertz(50);
    cameraServo.attach(CAMERA_SERVO, 500, 2400);
    cameraServo.write(CAMERA_CENTER_ANGLE); // camera faces forward while the robot is moving
    controlMotors("STOP");

    Serial.println("[DEBUG] [INIT] All GPIO pins initialized.");

    // Initialize I2C & OLED — JMD0.96D-1 (SSD1306 @ 0x3C, SDA=21, SCL=18)
    Wire.begin(21, 18);
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("[DEBUG] [OLED] ❌ Allocation failed!");
    } else {
        oledOk = true;
        Serial.println("[DEBUG] [OLED] ✔️ Initialized successfully.");
        playBootAnimation();
        showBootStage("Hardware ready", 25);
    }

    // Verify the complete camera path during boot by requesting and draining
    // one JPEG. This catches camera init, UART wiring and power faults early.
    showBootStage("Checking camera...", 30);
    cameraConnected = probeCameraConnection();
    cameraStatusKnown = true;
    if (cameraConnected) {
        Serial.println("[DEBUG] [CAM] Camera connected and test frame received.");
        showBootStage("Camera connected", 35);
    } else {
        Serial.println("[DEBUG] [CAM] Camera not ready during boot test.");
        showBootStage("Camera NOT ready", 35);
        camFaultUntil = millis() + 15000;
    }
    delay(700); // make the camera result readable on the OLED

    dht.begin();
    Serial.println("[DEBUG] [DHT] DHT22 sensor initialized.");

    showBootStage("Connecting WiFi...", 40);
    connectWiFi();
    showBootStage("WiFi connected", 70);
    initServerAddress(); // Initialize central server URL structure
    showBootStage("Linking server...", 85);
    startSocketIO();
    showBootStage("Boot complete", 100);
    Serial.println("[DEBUG] [INIT] Setup sequence completed.\n");
    updateStatusDisplay(true); // switch to the live status screen immediately
}

void loop() {
    socketIO.loop();

    while (Serial2.available() > 0) {
        gps.encode((char)Serial2.read());
    }
    if (gps.location.isUpdated()) {
        currentLatitude = gps.location.lat();
        currentLongitude = gps.location.lng();
    }

    if (millis() - lastLocationMillis >= 5000) {
        lastLocationMillis = millis();
        sendLocationData();
    }

    navigateMission();

    if (millis() - lastSensorMillis >= SENSOR_INTERVAL) {
        lastSensorMillis = millis();
        Serial.println("\n----------------------------------------");
        Serial.println("[DEBUG] ⏱️ Sensor interval triggered. Reading sensors...");
        sendSensorData();
    }

    if (millis() - lastWifiCheckMillis >= WIFI_CHECK_INTERVAL) {
        lastWifiCheckMillis = millis();
        if (WiFi.status() != WL_CONNECTED) {
            socketConnected = false;
            Serial.println("[DEBUG] [WiFi] ⚠️ Disconnected! Attempting to reconnect...");
            WiFi.reconnect();
        }
    }

    updateStatusDisplay(); // keep the OLED status screen live (1 Hz)
}

// Select the remote server or the original gateway-IP fallback.
void initServerAddress() {
#if defined(CUSTOM_SERVER_HOST) && defined(CUSTOM_SERVER_URL)
    serverHost = String(CUSTOM_SERVER_HOST);
    serverBaseUrl = String(CUSTOM_SERVER_URL);
    serverHost.trim();
    serverBaseUrl.trim();

    // CUSTOM_SERVER_URL must be a base URL, e.g.:
    //   https://crophealth.dpdns.org
    //   https://crophealth.dpdns.org/backend
    while (serverBaseUrl.endsWith("/")) {
        serverBaseUrl.remove(serverBaseUrl.length() - 1);
    }

    serverSecure = serverBaseUrl.startsWith("https://");
    serverPort = serverSecure ? 443 : 80;

    // Read an explicit port from the URL, e.g. http://host:8000.
    int authorityStart = serverBaseUrl.indexOf("://");
    authorityStart = authorityStart >= 0 ? authorityStart + 3 : 0;
    int authorityEnd = serverBaseUrl.indexOf('/', authorityStart);
    if (authorityEnd < 0) authorityEnd = serverBaseUrl.length();
    String authority = serverBaseUrl.substring(authorityStart, authorityEnd);
    int portSeparator = authority.lastIndexOf(':');
    if (portSeparator > 0) {
        int parsedPort = authority.substring(portSeparator + 1).toInt();
        if (parsedPort > 0 && parsedPort <= 65535) {
            serverPort = (uint16_t)parsedPort;
        }
    }

    // Keep CUSTOM_SERVER_HOST as a hostname only.
    int hostScheme = serverHost.indexOf("://");
    if (hostScheme >= 0) serverHost = serverHost.substring(hostScheme + 3);
    int hostSlash = serverHost.indexOf('/');
    if (hostSlash >= 0) serverHost = serverHost.substring(0, hostSlash);
    int hostPort = serverHost.lastIndexOf(':');
    if (hostPort > 0) serverHost = serverHost.substring(0, hostPort);

    // If CUSTOM_SERVER_HOST was accidentally left empty, recover it from URL.
    if (serverHost.length() == 0) {
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

    Serial.println("[DEBUG] [SERVER] Server configured:");
    Serial.printf("         --> Mode: %s\n", serverSecure ? "HTTPS/WSS" : "HTTP/WS");
    Serial.printf("         --> Host: %s\n", serverHost.c_str());
    Serial.printf("         --> Port: %u\n", serverPort);
    Serial.printf("         --> REST Base URL: %s\n", serverBaseUrl.c_str());
}

long readUltrasonic(int echoPin, String sensorName) {
    digitalWrite(ULTRASONIC_TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(ULTRASONIC_TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(ULTRASONIC_TRIG_PIN, LOW);

    long duration = pulseIn(echoPin, HIGH, 30000);
    long distance = duration * 0.034 / 2;
    if (distance == 0) distance = 400;

    Serial.printf("[DEBUG] [Ultrasonic-%s] Duration: %ld us, Distance: %ld cm\n", sensorName.c_str(), duration, distance);
    return distance;
}

void sendLocationData() {
    if (!socketConnected || currentLatitude == 0 || currentLongitude == 0) return;
    DynamicJsonDocument doc(512);
    JsonArray event = doc.to<JsonArray>();
    event.add("message.upsert");
    JsonObject envelope = event.createNestedObject();
    envelope["Type"] = "location";
    JsonObject message = envelope.createNestedObject("Message");
    message["latitude"] = currentLatitude;
    message["longitude"] = currentLongitude;
    message["altitude"] = gps.altitude.isValid() ? gps.altitude.meters() : 0;
    message["satellites"] = gps.satellites.isValid() ? gps.satellites.value() : 0;
    message["deviceId"] = DEVICE_ID;
    String output;
    serializeJson(doc, output);
    socketIO.sendEVENT(output);
}

void sendSensorData() {
    float humidity = dht.readHumidity();
    float temperature = dht.readTemperature();
    
    int rawRainValue = analogRead(RAIN_ANALOG_PIN);
    float rainPercentage = constrain(map(rawRainValue, 4095, 0, 0, 100), 0.0, 100.0);

    int rawSoilValue = analogRead(SOIL_ANALOG_PIN);
    float soilMoisturePercentage = constrain(map(rawSoilValue, 4095, 1500, 0, 100), 0.0, 100.0);
    int digitalRainState = digitalRead(RAIN_DIGITAL_PIN);

    Serial.printf("[DEBUG] [DHT22] Temp: %.2f °C | Humidity: %.2f %%\n", temperature, humidity);
    Serial.printf("[DEBUG] [Rain] Raw Analog: %d | Calculated Percentage: %.2f %% | Digital State: %d\n", rawRainValue, rainPercentage, digitalRainState);
    Serial.printf("[DEBUG] [Soil] Raw Analog: %d | Calculated Moisture: %.2f %%\n", rawSoilValue, soilMoisturePercentage);

    long distForward = readUltrasonic(ECHO_FORWARD_PIN, "FORWARD");
    long distLeft = readUltrasonic(ECHO_LEFT_PIN, "LEFT-45DEG");
    long distRight = readUltrasonic(ECHO_RIGHT_PIN, "RIGHT-45DEG");
    latestFrontCm = distForward;
    latestLeftCm = distLeft;
    latestRightCm = distRight;

    if (distForward < 15) {
        Serial.println("[DEBUG] [SAFETY] ⚠️ Obstacle closer than 15cm ahead! Forcing emergency STOP.");
        controlMotors("STOP");
    }

    if (isnan(humidity) || isnan(temperature)) {
        Serial.println("[DEBUG] [SENSOR] ❌ Failed to read from DHT22 sensor!");
        return;
    }

    DynamicJsonDocument doc(1024);
    JsonArray event = doc.to<JsonArray>();
    event.add("message.upsert");

    JsonObject data = event.createNestedObject();
    data["Type"] = "sensors";

    JsonObject message = data.createNestedObject("Message");
    message["temperature"] = temperature;
    message["humidity"] = humidity;
    message["soilMoisture"] = soilMoisturePercentage;
    message["rainDrop"] = rainPercentage;
    message["isRaining"] = (digitalRainState == LOW);
    message["distForward"] = distForward;
    message["distLeft"] = distLeft;
    message["distRight"] = distRight;
    message["blockId"] = currentBlockId;
    message["plant"] = currentPlant;
    message["deviceId"] = DEVICE_ID;

    String output;
    serializeJson(doc, output);
    Serial.printf("[DEBUG] [JSON Payload Out]: %s\n", output.c_str());

    if (socketConnected) {
        socketIO.sendEVENT(output);
        Serial.println("[DEBUG] [Socket.IO] 📤 Sensor payload successfully sent to server.");
    } else {
        Serial.println("[DEBUG] [Socket.IO] ⚠️ Cannot send data, socket is disconnected.");
    }
}

void sendMissionUpdate(const char *type, const char *state, const String &messageText) {
    if (!socketConnected || activeMissionId.length() == 0) return;
    DynamicJsonDocument doc(1024);
    JsonArray event = doc.to<JsonArray>();
    event.add("message.upsert");
    JsonObject envelope = event.createNestedObject();
    envelope["Type"] = type;
    JsonObject message = envelope.createNestedObject("Message");
    message["missionId"] = activeMissionId;
    message["patrolId"] = activePatrolId;
    message["state"] = state;
    message["currentWaypoint"] = currentWaypointIndex;
    message["totalWaypoints"] = missionWaypointCount;
    message["progress"] = missionWaypointCount ? (currentWaypointIndex * 100.0 / missionWaypointCount) : 0;
    message["message"] = messageText;
    String output; serializeJson(doc, output); socketIO.sendEVENT(output);
}

void beginAutonomousMissionTransfer(JsonObject data) {
    const int expected = data["totalWaypoints"] | 0;
    const char *missionId = data["missionId"] | "";
    missionTransferActive = false;
    missionWaypointCount = 0;
    autonomousActive = false;
    controlMotors("STOP");

    if (expected <= 0 || expected > MAX_WAYPOINTS || missionId[0] == '\0') {
        Serial.printf("[DEBUG] [MISSION] ❌ Invalid chunked mission header: id=%s count=%d\n",
                      missionId, expected);
        return;
    }

    missionTransferId = missionId;
    activeMissionId = missionId;
    activePatrolId = data["patrolId"] | 0;
    missionArrivalRadiusM = data["config"]["arrivalRadiusM"] | 2.0;
    missionTransferExpected = expected;
    missionTransferStartPaused = data["startPaused"] | true;
    currentWaypointIndex = 0;
    autonomousPaused = true; // never move with a partially loaded route
    missionTransferActive = true;
    Serial.printf("[DEBUG] [MISSION] Receiving %d waypoints in bounded chunks...\n", expected);
}

void appendAutonomousMissionChunk(JsonObject data) {
    const char *missionId = data["missionId"] | "";
    const int offset = data["offset"] | -1;
    JsonArray points = data["waypoints"].as<JsonArray>();

    if (!missionTransferActive || missionTransferId != missionId ||
        offset != missionWaypointCount || points.isNull() ||
        missionWaypointCount + (int)points.size() > missionTransferExpected) {
        Serial.printf("[DEBUG] [MISSION] ❌ Rejected chunk: id=%s offset=%d received=%d size=%u\n",
                      missionId, offset, missionWaypointCount, (unsigned)points.size());
        missionTransferActive = false;
        missionWaypointCount = 0;
        return;
    }

    for (JsonObject point : points) {
        MissionWaypoint &target = missionWaypoints[missionWaypointCount];
        target.latitude = point["latitude"] | 0.0;
        target.longitude = point["longitude"] | 0.0;
        target.scan = point["scan"] | false;
        target.index = point["index"] | missionWaypointCount;
        missionWaypointCount++;
    }
    Serial.printf("[DEBUG] [MISSION] Chunk accepted: %d/%d waypoints\n",
                  missionWaypointCount, missionTransferExpected);
}

void finishAutonomousMissionTransfer(JsonObject data) {
    const char *missionId = data["missionId"] | "";
    if (!missionTransferActive || missionTransferId != missionId ||
        missionWaypointCount != missionTransferExpected) {
        Serial.printf("[DEBUG] [MISSION] ❌ Incomplete route: received=%d expected=%d\n",
                      missionWaypointCount, missionTransferExpected);
        missionTransferActive = false;
        missionWaypointCount = 0;
        autonomousActive = false;
        return;
    }

    missionTransferActive = false;
    currentWaypointIndex = 0;
    autonomousActive = missionWaypointCount > 0;
    autonomousPaused = missionTransferStartPaused;
    cameraServo.write(CAMERA_CENTER_ANGLE);
    Serial.printf("[DEBUG] [MISSION] ✔️ Route loaded: %d waypoints (%s)\n",
                  missionWaypointCount, autonomousPaused ? "paused" : "running");
    sendMissionUpdate("mission_progress", autonomousPaused ? "paused" : "running",
                      autonomousPaused ? "Mission restored in paused state" : "Mission loaded");
}

void loadAutonomousMission(JsonObject data) {
    JsonArray points = data["waypoints"].as<JsonArray>();
    activeMissionId = String((const char *)(data["missionId"] | ""));
    if ((int)points.size() > MAX_WAYPOINTS) {
        missionWaypointCount = 0;
        autonomousActive = false;
        sendMissionUpdate("mission_progress", "fault", "Route exceeds 512-waypoint device capacity; increase route spacing");
        return;
    }
    missionWaypointCount = (int)points.size();
    currentWaypointIndex = 0;
    activePatrolId = data["patrolId"] | 0;
    missionArrivalRadiusM = data["config"]["arrivalRadiusM"] | 2.0;
    for (int i = 0; i < missionWaypointCount; i++) {
        missionWaypoints[i].latitude = points[i]["latitude"] | 0.0;
        missionWaypoints[i].longitude = points[i]["longitude"] | 0.0;
        missionWaypoints[i].scan = points[i]["scan"] | false;
        missionWaypoints[i].index = points[i]["index"] | i;
    }
    autonomousPaused = false;
    autonomousActive = missionWaypointCount > 0;
    cameraServo.write(CAMERA_CENTER_ANGLE);
    sendMissionUpdate("mission_progress", autonomousActive ? "running" : "fault",
                      autonomousActive ? "Mission loaded" : "Mission has no waypoints");
}

static float normalizeHeadingError(float value) {
    while (value > 180) value -= 360;
    while (value < -180) value += 360;
    return value;
}

void captureBothSides(int scanPoint) {
    controlMotors("STOP");
    // The physical bracket is mirrored: 0° faces right and 180° faces left.
    // Capture right first, then left, using the full ±90° sweep rather than
    // the old 35°/145° positions (only ~55° away from forward).
    cameraServo.write(CAMERA_RIGHT_ANGLE);
    delay(900); // centre -> endpoint
    captureAndUploadImage("right", scanPoint);
    cameraServo.write(CAMERA_LEFT_ANGLE);
    delay(1200); // full 180° traverse takes longer
    captureAndUploadImage("left", scanPoint);
    cameraServo.write(CAMERA_CENTER_ANGLE);
    delay(900);
}

bool handleObstacleAvoidance() {
    const unsigned long now = millis();

    if (avoidancePhase != AVOID_NONE) {
        // If the bypass leg itself becomes blocked, stop and re-plan toward
        // whichever side currently has more clearance.
        if (avoidancePhase == AVOID_PASS && latestFrontCm < 18) {
            controlMotors("STOP");
            avoidanceTurn = latestLeftCm >= latestRightCm ? "LEFT" : "RIGHT";
            controlMotors(avoidanceTurn);
            avoidancePhase = AVOID_TURN_OUT;
            avoidanceDeadline = now + 650;
            return true;
        }
        if ((long)(now - avoidanceDeadline) < 0) return true;

        if (avoidancePhase == AVOID_TURN_OUT) {
            controlMotors("FORWARD");
            avoidancePhase = AVOID_PASS;
            avoidanceDeadline = now + 1000;
        } else if (avoidancePhase == AVOID_PASS) {
            controlMotors(avoidanceTurn == "LEFT" ? "RIGHT" : "LEFT");
            avoidancePhase = AVOID_TURN_BACK;
            avoidanceDeadline = now + 650;
        } else {
            controlMotors("STOP");
            avoidancePhase = AVOID_NONE;
        }
        return true;
    }

    const bool frontBlocked = latestFrontCm < 30;
    const bool corridorBlocked = latestLeftCm < 20 && latestRightCm < 20;
    if (!frontBlocked && !corridorBlocked) return false;

    controlMotors("STOP");
    avoidanceTurn = latestLeftCm >= latestRightCm ? "LEFT" : "RIGHT";
    Serial.printf("[DEBUG] [AVOID] Obstacle F/L/R=%ld/%ld/%ld cm; bypassing %s\n",
                  latestFrontCm, latestLeftCm, latestRightCm, avoidanceTurn.c_str());
    controlMotors(avoidanceTurn);
    avoidancePhase = AVOID_TURN_OUT;
    avoidanceDeadline = now + (corridorBlocked ? 800 : 650);
    return true;
}

void navigateMission() {
    if (!autonomousActive || autonomousPaused || currentWaypointIndex >= missionWaypointCount) return;
    if (currentLatitude == 0 || currentLongitude == 0 || !gps.location.isValid()) {
        controlMotors("STOP");
        return;
    }

    if (millis() - lastNavSensorAt >= 500) {
        lastNavSensorAt = millis();
        latestFrontCm = readUltrasonic(ECHO_FORWARD_PIN, "NAV-FRONT");
        latestLeftCm = readUltrasonic(ECHO_LEFT_PIN, "NAV-LEFT45");
        latestRightCm = readUltrasonic(ECHO_RIGHT_PIN, "NAV-RIGHT45");
    }

    MissionWaypoint &target = missionWaypoints[currentWaypointIndex];
    double distance = TinyGPSPlus::distanceBetween(currentLatitude, currentLongitude,
                                                    target.latitude, target.longitude);
    if (distance <= missionArrivalRadiusM) {
        controlMotors("STOP");
        if (target.scan) captureBothSides(target.index);
        currentWaypointIndex++;
        if (currentWaypointIndex >= missionWaypointCount) {
            autonomousActive = false;
            cameraServo.write(CAMERA_CENTER_ANGLE);
            sendMissionUpdate("mission_complete", "completed", "All selected blocks completed");
            return;
        }
        sendMissionUpdate("mission_progress", "running", "Waypoint completed");
        return;
    }

    // Route around obstacles instead of only stopping/turning in place.
    if (handleObstacleAvoidance()) return;

    double desired = TinyGPSPlus::courseTo(currentLatitude, currentLongitude,
                                            target.latitude, target.longitude);
    double actual = gps.course.isValid() && gps.speed.kmph() > 0.3 ? gps.course.deg() : desired;
    float error = normalizeHeadingError((float)(desired - actual));
    if (error > 22) controlMotors("RIGHT");
    else if (error < -22) controlMotors("LEFT");
    else controlMotors("FORWARD");
}

void controlMotors(String action) {
    static String currentAction = "STOP";
    Serial.printf("[DEBUG] [MOTOR] Executing action -> %s\n", action.c_str());

    if (action != "FORWARD" && action != "BACKWARD" && action != "LEFT" &&
        action != "RIGHT" && action != "STOP") {
        Serial.printf("[DEBUG] [MOTOR] Unknown action command: %s\n", action.c_str());
        return;
    }

    // Never reverse an H-bridge while PWM is active. Four motors have a large
    // start/reversal surge; removing enable first prevents one bridge channel
    // dropping out while its direction inputs are changing.
    if (action == "STOP") {
        analogWrite(ENA_PIN, 0);
        analogWrite(ENB_PIN, 0);
        digitalWrite(IN1_PIN, LOW);
        digitalWrite(IN2_PIN, LOW);
        digitalWrite(IN3_PIN, LOW);
        digitalWrite(IN4_PIN, LOW);
        currentAction = "STOP";
        return;
    }

    const int targetPwm = (action == "LEFT" || action == "RIGHT") ? 180 : 200;
    const bool changedDirection = action != currentAction;
    if (changedDirection) {
        analogWrite(ENA_PIN, 0);
        analogWrite(ENB_PIN, 0);
        digitalWrite(IN1_PIN, LOW);
        digitalWrite(IN2_PIN, LOW);
        digitalWrite(IN3_PIN, LOW);
        digitalWrite(IN4_PIN, LOW);
        delay(40); // H-bridge dead time
    }

    if (action == "FORWARD") {
        digitalWrite(IN1_PIN, HIGH); digitalWrite(IN2_PIN, LOW);
        digitalWrite(IN3_PIN, HIGH); digitalWrite(IN4_PIN, LOW);
    } else if (action == "BACKWARD") {
        digitalWrite(IN1_PIN, LOW);  digitalWrite(IN2_PIN, HIGH);
        digitalWrite(IN3_PIN, LOW);  digitalWrite(IN4_PIN, HIGH);
    } else if (action == "LEFT") {
        digitalWrite(IN1_PIN, LOW);  digitalWrite(IN2_PIN, HIGH);
        digitalWrite(IN3_PIN, HIGH); digitalWrite(IN4_PIN, LOW);
    } else { // RIGHT
        digitalWrite(IN1_PIN, HIGH); digitalWrite(IN2_PIN, LOW);
        digitalWrite(IN3_PIN, LOW);  digitalWrite(IN4_PIN, HIGH);
    }

    if (changedDirection) {
        delay(10); // let IN1-IN4 settle before enabling the bridges
        for (int pwm = 90; pwm < targetPwm; pwm += 22) {
            analogWrite(ENA_PIN, pwm);
            analogWrite(ENB_PIN, pwm);
            delay(18);
        }
    }
    analogWrite(ENA_PIN, targetPwm);
    analogWrite(ENB_PIN, targetPwm);
    currentAction = action;

    Serial.printf("[DEBUG] [MOTOR] pins IN1=%d IN2=%d IN3=%d IN4=%d EN=%d\n",
                  digitalRead(IN1_PIN), digitalRead(IN2_PIN),
                  digitalRead(IN3_PIN), digitalRead(IN4_PIN), targetPwm);
}

void connectWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.printf("[DEBUG] [WiFi] Connecting to SSID: %s", WIFI_SSID);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        attempts++;
        if (oledOk && attempts % 2 == 0) {
            char caption[48];
            snprintf(caption, sizeof(caption), "WiFi: %.20s %.*s", WIFI_SSID, (attempts / 2) % 4, "...");
            showBootStage(caption, 40 + min(attempts / 2, 25));
        }
        if (attempts >= 60) {
            Serial.println("\n[DEBUG] [WiFi] ⚠️ Connection timeout! Retrying...");
            WiFi.disconnect(true);
            delay(1000);
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
            attempts = 0;
        }
    }
    Serial.println("\n[DEBUG] [WiFi] ✔️ Connected successfully to Main Router!");
    Serial.print("[DEBUG] [WiFi] Station IP Address: ");
    Serial.println(WiFi.localIP());
}

void startSocketIO() {
    // Match the known-good Wokwi project exactly. DEVICE_ID is deliberately
    // omitted: the server already defaults esp_32 clients to robot-01.
    String socketPath = "/socket.io/?EIO=4&role=" + String(DEVICE_ROLE) + "&token=" + String(ROBOT_TOKEN);
    Serial.printf("[DEBUG] [Socket.IO] Connecting to %s://%s:%u%s\n",
                  serverSecure ? "wss" : "ws",
                  serverHost.c_str(), serverPort, socketPath.c_str());

    if (serverSecure) {
        // HTTPS base URL => secure WebSocket (WSS), normally on port 443.
        socketIO.beginSSL(serverHost.c_str(), serverPort, socketPath.c_str());
    } else {
        // Custom HTTP URL or original gateway-IP fallback.
        socketIO.begin(serverHost.c_str(), serverPort, socketPath.c_str());
    }

    socketIO.onEvent(socketIOEvent);
    socketIO.setReconnectInterval(5000);
}

String readCameraDiagnostic() {
    // Called after <IMG:0>. New camera firmware appends
    // <CAMERR:INIT|CAPTURE|ENCODE:0x...>; old firmware simply times out here.
    const unsigned long previousTimeout = Serial.getTimeout();
    Serial.setTimeout(350);
    String diagnostic = "no diagnostic (upload the updated ESP32-CAM sketch)";
    if (Serial.find("<CAMERR:")) {
        String value = Serial.readStringUntil('>');
        if (value.length() > 0) diagnostic = value;
    }
    Serial.setTimeout(previousTimeout);
    return diagnostic;
}

bool probeCameraConnection() {
    while (Serial.available() > 0) Serial.read();
    Serial.flush();
    delay(25);
    Serial.print('C');
    Serial.flush();

    Serial.setTimeout(5000);
    if (!Serial.find("<IMG:")) {
        Serial.setTimeout(8000);
        return false;
    }

    const int imgSize = Serial.parseInt();
    if (Serial.read() != '>') {
        Serial.println("[DEBUG] [CAM] Boot test received a malformed image header.");
        Serial.setTimeout(8000);
        return false;
    }
    if (imgSize <= 0) {
        const String diagnostic = readCameraDiagnostic();
        Serial.printf("[DEBUG] [CAM] Boot diagnostic: %s\n", diagnostic.c_str());
        Serial.setTimeout(8000);
        return false;
    }
    if (imgSize > 512000) {
        Serial.printf("[DEBUG] [CAM] Boot test rejected impossible frame size: %d\n", imgSize);
        Serial.setTimeout(8000);
        return false;
    }

    // Drain the boot-test frame in small chunks; do not allocate another full
    // image just to establish that the camera link works.
    uint8_t scratch[256];
    size_t remaining = (size_t)imgSize;
    while (remaining > 0) {
        const size_t wanted = min(remaining, sizeof(scratch));
        const size_t received = Serial.readBytes(scratch, wanted);
        if (received == 0) {
            Serial.setTimeout(8000);
            return false;
        }
        remaining -= received;
    }
    Serial.setTimeout(8000);
    return true;
}

void captureAndUploadImage(const String &side, int scanPoint) {
    // Manual captures must always work, even without GPS/field-map context.
    // The server resolves the crop block from the mission, GPS or the recent
    // trail — and simply stores the photo if no mapped block applies.
    if (currentPlant.length() == 0 || currentBlockId.length() == 0) {
        Serial.println("[DEBUG] [CAM] No mapped crop block context; capturing anyway (server resolves/stores).");
    }
    captureInProgress = true;
    updateStatusDisplay(true);

    // Trigger the CAM — up to 3 attempts. Both the real ESP32-CAM sketch and
    // the Wokwi chip accept a standalone 'C' at start-of-line OR after >=20ms
    // of UART silence, so a forced 25ms idle gap before the bare 'C' makes the
    // trigger reliable even if a previous debug line was corrupted on the wire.
    bool headerFound = false;
    Serial.setTimeout(3000); // per-attempt wait for the "<IMG:" header
    for (int attempt = 1; attempt <= 3 && !headerFound; attempt++) {
        while (Serial.available() > 0) Serial.read(); // drop stale bytes
        Serial.flush();
        delay(25); // guarantee the CAM sees an idle gap before the 'C'
        Serial.print('C');
        Serial.flush();
        headerFound = Serial.find("<IMG:");
        if (!headerFound) {
            Serial.printf("[DEBUG] [CAM] No frame header (attempt %d/3), retrying...\n", attempt);
        }
    }
    Serial.setTimeout(8000); // restore the long timeout for the JPEG body

    if (headerFound) {
        int imgSize = Serial.parseInt();
        if (imgSize <= 0) {
            // Consume the image-header terminator before reading the appended
            // camera diagnostic. This also remains compatible with old firmware.
            if (Serial.peek() == '>') Serial.read();
            const String diagnostic = readCameraDiagnostic();
            cameraConnected = false;
            cameraStatusKnown = true;
            Serial.printf("[DEBUG] [CAM] ❌ CAM answered <IMG:0>; reason=%s\n", diagnostic.c_str());
            sendCameraFault("ESP32-CAM returned no frame (" + diagnostic + "). INIT means power/ribbon/sensor; ENCODE usually means insufficient free memory.");
        } else if (imgSize > 512000) {
            cameraConnected = false;
            cameraStatusKnown = true;
            Serial.printf("[DEBUG] [CAM] ❌ Rejected impossible frame size: %d\n", imgSize);
            sendCameraFault("ESP32-CAM announced an invalid frame size; UART data may be corrupted.");
        } else if (Serial.read() == '>') {
            uint8_t* imgBuffer = (uint8_t*) malloc(imgSize);
            if (imgBuffer != NULL) {
                
                size_t bytesRead = Serial.readBytes(imgBuffer, imgSize);
                
                if (bytesRead == imgSize) {
                    cameraConnected = true;
                    cameraStatusKnown = true;
                    Serial.println("[DEBUG] [CAM] ✔️ Received Image successfully over Serial! Sending HTTP Request...");

                    // 3. Construct HTTP Multipart/form-data upload request using centralized Base URL
                    HTTPClient http;
                    String uploadUrl = serverBaseUrl + "/api/images/upload";
                    
                    http.begin(uploadUrl);

                    String boundary = "---011000010111000001101001";
                    http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
                    http.addHeader("X-Device-Token", ROBOT_TOKEN);

                    // Form Body Construction
                    String head = "--" + boundary + "\r\n";
                    head += "Content-Disposition: form-data; name=\"file\"; filename=\"000000.jpg\"\r\n";
                    head += "Content-Type: image/jpeg\r\n\r\n";

                    String tail = "\r\n";
                    auto addField = [&](const String &name, const String &value) {
                        tail += "--" + boundary + "\r\n";
                        tail += "Content-Disposition: form-data; name=\"" + name + "\"\r\n\r\n";
                        tail += value + "\r\n";
                    };
                    // Context fields are optional: the server maps GPS -> block
                    // itself and accepts unmapped manual captures.
                    if (currentPlant.length() > 0) addField("plant", currentPlant);
                    if (currentBlockId.length() > 0) addField("blockId", currentBlockId);
                    addField("latitude", String(currentLatitude, 7));
                    addField("longitude", String(currentLongitude, 7));
                    addField("deviceId", DEVICE_ID);
                    addField("missionId", activeMissionId);
                    addField("patrolId", String(activePatrolId));
                    addField("scanPoint", String(scanPoint));
                    addField("side", side);
                    tail += "--" + boundary + "--\r\n";

                    size_t totalLen = head.length() + imgSize + tail.length();
                    uint8_t *payloadBuffer = (uint8_t *)malloc(totalLen);

                    if (payloadBuffer != NULL) {
                        memcpy(payloadBuffer, head.c_str(), head.length());
                        memcpy(payloadBuffer + head.length(), imgBuffer, imgSize);
                        memcpy(payloadBuffer + head.length() + imgSize, tail.c_str(), tail.length());

                        int httpCode = http.POST(payloadBuffer, totalLen);

                        if (httpCode > 0) {
                            Serial.printf("[DEBUG] [HTTP Upload] Success! Response code: %d\n", httpCode);
                            String response = http.getString();
                            Serial.println("[DEBUG] [HTTP Response]: " + response);
                        } else {
                            Serial.printf("[DEBUG] [HTTP Upload] ❌ Error failed: %s\n", http.errorToString(httpCode).c_str());
                        }

                        free(payloadBuffer);
                    }
                    http.end();
                } else {
                    cameraConnected = false;
                    cameraStatusKnown = true;
                    Serial.println("[DEBUG] [CAM] ❌ Error: Partial image received over serial.");
                    sendCameraFault("Partial image received from the ESP32-CAM: UART link unstable - check wiring/GND and keep the USB serial monitor disconnected.");
                }
                free(imgBuffer);
            } else {
                cameraConnected = false;
                cameraStatusKnown = true;
                Serial.println("[DEBUG] [CAM] ❌ Out of memory for the incoming frame.");
                sendCameraFault("Controller ran out of RAM for the incoming camera frame.");
            }
        }
    } else {
        cameraConnected = false;
        cameraStatusKnown = true;
        Serial.println("[DEBUG] [CAM] ❌ Timeout: No response from ESP32-CAM on TX0/RX0 after 3 attempts!");
        Serial.println("[DEBUG] [CAM]    Checklist: CAM on stable 5V + common GND, CAM U0T->DevKit RX0 and CAM U0R->DevKit TX0, GPIO0 NOT grounded (that holds the CAM in flash mode), both sides 921600 baud, and no USB serial monitor open on the same pins while capturing.");
        sendCameraFault("No response from ESP32-CAM on the UART link after 3 attempts - check 5V power, TX0/RX0 cross-wiring, GPIO0 not grounded, and disconnect the USB serial monitor.");
    }
    captureInProgress = false;
    updateStatusDisplay(true);
}

// Report a camera failure to the operator: 15 s CAM ERROR banner on the OLED
// and a camera_fault message the server turns into a dashboard alert.
void sendCameraFault(const String &reason) {
    camFaultUntil = millis() + 15000;
    if (!socketConnected) return;
    DynamicJsonDocument doc(512);
    JsonArray event = doc.to<JsonArray>();
    event.add("message.upsert");
    JsonObject envelope = event.createNestedObject();
    envelope["Type"] = "camera_fault";
    JsonObject message = envelope.createNestedObject("Message");
    message["reason"] = reason;
    message["deviceId"] = DEVICE_ID;
    String output;
    serializeJson(doc, output);
    socketIO.sendEVENT(output);
}