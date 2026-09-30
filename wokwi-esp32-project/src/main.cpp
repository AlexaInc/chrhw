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
float missionArrivalRadiusM = 2.0;
long latestFrontCm = 400, latestLeftCm = 400, latestRightCm = 400;
unsigned long lastNavSensorAt = 0;

// OLED Display setup (I2C: SDA=21, SCL=18)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

unsigned long lastSensorMillis = 0;
unsigned long lastWifiCheckMillis = 0;
const unsigned long SENSOR_INTERVAL = 5000;
const unsigned long WIFI_CHECK_INTERVAL = 5000;

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
void navigateMission();
void captureBothSides(int scanPoint);
void sendMissionUpdate(const char *type, const char *state, const String &message = "");

void socketIOEvent(socketIOmessageType_t type, uint8_t *payload, size_t length) {
    switch (type) {
        case sIOtype_DISCONNECT:
            socketConnected = false;
            Serial.println("\n[DEBUG] [IOc] ❌ Socket.IO DISCONNECTED");
            break;

        case sIOtype_CONNECT:
            Serial.println("\n[DEBUG] [IOc] ✔️ Socket.IO CONNECTED successfully!");
            socketIO.send(sIOtype_CONNECT, "/");
            socketConnected = true;
            break;

        case sIOtype_EVENT: {
            if (payload == nullptr) break;
            Serial.printf("[DEBUG] [IOc] 📥 Received Event Payload: %s\n", payload);
            
            DynamicJsonDocument doc(98304);
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
                    if (strcmp(action, "field_context") == 0) {
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
                    } else if (strcmp(action, "autonomous_mission") == 0) {
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
                        captureBothSides(currentWaypointIndex);
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
    cameraServo.write(90); // camera faces forward while the robot is moving
    controlMotors("STOP");

    Serial.println("[DEBUG] [INIT] All GPIO pins initialized.");

    // Initialize I2C & OLED (SDA=21, SCL=18)
    Wire.begin(21, 18);
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("[DEBUG] [OLED] ❌ Allocation failed!");
    } else {
        Serial.println("[DEBUG] [OLED] ✔️ Initialized successfully.");
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0,0);
        display.print("Agri Robot Initializing...");
        display.display();
    }

    dht.begin();
    Serial.println("[DEBUG] [DHT] DHT22 sensor initialized.");

    connectWiFi();
    initServerAddress(); // Initialize central server URL structure
    startSocketIO();
    Serial.println("[DEBUG] [INIT] Setup sequence completed.\n");
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
    cameraServo.write(90);
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
    cameraServo.write(35);
    delay(700);
    captureAndUploadImage("left", scanPoint);
    cameraServo.write(145);
    delay(700);
    captureAndUploadImage("right", scanPoint);
    cameraServo.write(90);
    delay(400);
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
            cameraServo.write(90);
            sendMissionUpdate("mission_complete", "completed", "All selected blocks completed");
            return;
        }
        sendMissionUpdate("mission_progress", "running", "Waypoint completed");
        return;
    }

    // Three sensors are physically aimed front, front-left ~45°, front-right ~45°.
    // Stop and bias the turn toward the side with more clearance, then resume GPS course.
    if (latestFrontCm < 25 || (latestLeftCm < 18 && latestRightCm < 18)) {
        controlMotors("STOP");
        controlMotors(latestLeftCm >= latestRightCm ? "LEFT" : "RIGHT");
        delay(450);
        controlMotors("STOP");
        return;
    }

    double desired = TinyGPSPlus::courseTo(currentLatitude, currentLongitude,
                                            target.latitude, target.longitude);
    double actual = gps.course.isValid() && gps.speed.kmph() > 0.3 ? gps.course.deg() : desired;
    float error = normalizeHeadingError((float)(desired - actual));
    if (error > 22) controlMotors("RIGHT");
    else if (error < -22) controlMotors("LEFT");
    else controlMotors("FORWARD");
}

void controlMotors(String action) {
    Serial.printf("[DEBUG] [MOTOR] Executing action -> %s\n", action.c_str());

    if (action == "FORWARD") {
        digitalWrite(IN1_PIN, HIGH);
        digitalWrite(IN2_PIN, LOW);
        digitalWrite(IN3_PIN, HIGH);
        digitalWrite(IN4_PIN, LOW);
        analogWrite(ENA_PIN, 200);
        analogWrite(ENB_PIN, 200);
    } else if (action == "BACKWARD") {
        digitalWrite(IN1_PIN, LOW);
        digitalWrite(IN2_PIN, HIGH);
        digitalWrite(IN3_PIN, LOW);
        digitalWrite(IN4_PIN, HIGH);
        analogWrite(ENA_PIN, 200);
        analogWrite(ENB_PIN, 200);
    } else if (action == "LEFT") {
        digitalWrite(IN1_PIN, LOW);
        digitalWrite(IN2_PIN, HIGH);
        digitalWrite(IN3_PIN, HIGH);
        digitalWrite(IN4_PIN, LOW);
        analogWrite(ENA_PIN, 180);
        analogWrite(ENB_PIN, 180);
    } else if (action == "RIGHT") {
        digitalWrite(IN1_PIN, HIGH);
        digitalWrite(IN2_PIN, LOW);
        digitalWrite(IN3_PIN, LOW);
        digitalWrite(IN4_PIN, HIGH);
        analogWrite(ENA_PIN, 180);
        analogWrite(ENB_PIN, 180);
    } else if (action == "STOP") {
        digitalWrite(IN1_PIN, LOW);
        digitalWrite(IN2_PIN, LOW);
        digitalWrite(IN3_PIN, LOW);
        digitalWrite(IN4_PIN, LOW);
        analogWrite(ENA_PIN, 0);
        analogWrite(ENB_PIN, 0);
    } else {
        Serial.printf("[DEBUG] [MOTOR] ❓ Unknown action command: %s\n", action.c_str());
    }
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
    String socketPath = "/socket.io/?EIO=4&role=" + String(DEVICE_ROLE) + "&deviceId=" + String(DEVICE_ID) + "&token=" + String(ROBOT_TOKEN);
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

void captureAndUploadImage(const String &side, int scanPoint) {
    if (currentPlant.length() == 0 || currentBlockId.length() == 0) {
        Serial.println("[DEBUG] [CAM] Capture blocked: robot is not inside a mapped crop block.");
        return;
    }
    // Remove any stale camera bytes before starting a new frame.
    while (Serial.available() > 0) Serial.read();

    // Send a standalone 'C'. The real ESP32-CAM sketch and Wokwi custom chip
    // both treat a standalone C (after a newline/idle gap) as the shutter command.
    Serial.print('C');
    Serial.flush();

    // Wait and read the incoming byte header from CAM
    if (Serial.find("<IMG:")) {
        int imgSize = Serial.parseInt();
        if (Serial.read() == '>') {
            uint8_t* imgBuffer = (uint8_t*) malloc(imgSize);
            if (imgBuffer != NULL) {
                
                size_t bytesRead = Serial.readBytes(imgBuffer, imgSize);
                
                if (bytesRead == imgSize) {
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
                    addField("plant", currentPlant);
                    addField("blockId", currentBlockId);
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
                    Serial.println("[DEBUG] [CAM] ❌ Error: Partial image received over serial.");
                }
                free(imgBuffer);
            }
        }
    } else {
        Serial.println("[DEBUG] [CAM] ❌ Timeout: No response from ESP32-CAM on TX0/RX0!");
    }
}