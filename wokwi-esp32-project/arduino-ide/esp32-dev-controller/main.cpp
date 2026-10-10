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
#include "arc_math.h"
#include "logo_bitmap.h"
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <TinyGPSPlus.h>
#include <ESP32Servo.h>
#include <math.h>

DHT dht(DHTPIN, DHTTYPE);
SocketIOclient socketIO;
TinyGPSPlus gps;

String currentBlockId = "";
String currentBlockName = "";
String currentPlant = "";
double currentLatitude = 0;
double currentLongitude = 0;
unsigned long lastLocationMillis = 0;

// ---------------------------------------------------------------------------
// GPS placeholder (see config.h) -------------------------------------------
// `gpsHasFix()` is the only question the firmware asks about the module: the
// helpers below answer with the last real fix, or with the placeholder position
// from config.h while the GPS is silent. Navigation never uses the
// placeholder - the rover refuses to drive on a position it did not measure.
// ---------------------------------------------------------------------------
bool gpsHasFix() {
    return gps.location.isValid() && currentLatitude != 0 && currentLongitude != 0;
}

double reportLatitude() {
#if GPS_FALLBACK_ENABLED
    return gpsHasFix() ? currentLatitude : (double)GPS_FALLBACK_LATITUDE;
#else
    return currentLatitude;
#endif
}

double reportLongitude() {
#if GPS_FALLBACK_ENABLED
    return gpsHasFix() ? currentLongitude : (double)GPS_FALLBACK_LONGITUDE;
#else
    return currentLongitude;
#endif
}

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
// Front sensor arc: one centre sensor plus the two bracket-mounted side
// sensors. Readings are along each sensor's own axis, so the planner converts
// them with the mounting angles (SENSOR_ANGLE_*_DEG) before deciding anything.
long latestFrontCm = ULTRASONIC_MAX_CM;
long latestLeftCm = ULTRASONIC_MAX_CM;
long latestRightCm = ULTRASONIC_MAX_CM;
unsigned long lastNavSensorAt = 0;
// Round-robin arc scan (three HC-SR04 on one trigger must never fire together).
enum ArcSlot : uint8_t { ARC_SLOT_CENTER = 0, ARC_SLOT_LEFT, ARC_SLOT_CENTER2, ARC_SLOT_RIGHT };
uint8_t arcSlot = ARC_SLOT_CENTER;
unsigned long arcSlotAt = 0;
float gapLeftCm = 0.0f;   // lateral room the left ray currently proves
float gapRightCm = 0.0f;  // lateral room the right ray currently proves

// Full camera sweep: centre=90°, then approximately 90° to either side.
const int CAMERA_CENTER_ANGLE = 90;
// Physical bracket is mirrored: lower PWM angle looks right, higher looks left.
const int CAMERA_RIGHT_ANGLE = 0;
const int CAMERA_LEFT_ANGLE = 180;

/* ==========================================================================
 * FRONT ARC PLANNER STATE
 * --------------------------------------------------------------------------
 * The rover is NOT supposed to stop for a plant it can drive around. The three
 * beams of the front arc overlap into one fan (a side sensor at 45 deg with an
 * 8 deg half cone watches everything from 37 deg outwards), so the planner can
 * tell "blocked straight ahead" from "something beside the front corner":
 *
 *   1. something inside the emergency ring      -> brake, no alternative
 *   2. path ahead blocked                       -> steer to the wider side,
 *                                                  creep past, turn back (auto)
 *   3. plant intruding into the swept path      -> shave off while driving on
 *   4. no side gap wide enough to pass          -> the ONLY planned stop,
 *                                                  reported as "no-path"
 *
 * In autonomous mode the manoeuvre is a full detour that hands control back to
 * the mission when it is done. In manual mode the same steering is applied to a
 * held FORWARD command while the panel's assist switch is on.
 * ======================================================================== */
enum AvoidState : uint8_t {
    AVOID_STATE_CLEAR = 0,
    AVOID_STATE_STEER_LEFT,   // steering away from something on the left
    AVOID_STATE_STEER_RIGHT,  // steering away from something on the right
    AVOID_STATE_CREEP,        // turned enough: creeping past at crawl speed
    AVOID_STATE_TURN_BACK,    // autonomous: swinging back onto the heading
    AVOID_STATE_NO_PATH,      // no side gap wide enough - stopped
    AVOID_STATE_EMERGENCY     // inside the emergency ring - braked
};
AvoidState avoidState = AVOID_STATE_CLEAR;
int avoidDir = 0;                     // -1 = steering left, +1 = steering right
bool avoidanceOwnsMotion = false;     // an autonomous detour is driving the rover
unsigned long avoidLegDeadline = 0;   // end of the current steer-out/creep leg
unsigned long avoidSteerUntil = 0;    // manual assist: stop turning out at this time
unsigned long avoidReplanAt = 0;      // minimum gap between two re-plans
uint8_t avoidHits = 0, avoidClearHits = 0;
String avoidReportedState = "";       // last state reported to the server

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

/* ==========================================================================
 * STATE THAT MUST SURVIVE A REBOOT (microSD cache)
 * ========================================================================== */
bool sdOk = false;
String cachedMapRev = "";        // revision of the field map stored on the card
String cachedMapName = "";
int cachedMapBlocks = 0;
uint32_t cachedMapBytes = 0;
unsigned long cachedMapLoadedAt = 0;

/* ==========================================================================
 * MOTION LIMITS - the rover is never allowed to run faster than this
 * ========================================================================== */
// Percentages of the safe defaults in config.h (100 % == cruise 110 / turn 95).
// The web panel can only lower them; 0 % means "do not drive at all".
int motionDrivePercent = MOTION_DEFAULT_DRIVE_PERCENT;
int motionTurnPercent = MOTION_DEFAULT_TURN_PERCENT;
int motionCrawlPercent = MOTION_DEFAULT_CRAWL_PERCENT;
// Front-arc geometry of THIS rover (panel-adjustable, saved on SD): the angles
// the side brackets actually hold and whether manual driving gets the assist.
int motionAngleLeftDeg = SENSOR_ANGLE_LEFT_DEG;
int motionAngleRightDeg = SENSOR_ANGLE_RIGHT_DEG;
bool motionAvoidAssist = (AVOID_ASSIST_MANUAL != 0);

// Motion engine state, serviced from loop(). The firmware never sits inside a
// delay() while the operator presses STOP.
enum MotionSource : uint8_t { SRC_NONE = 0, SRC_MANUAL = 1, SRC_AUTO = 2 };
MotionSource motionSource = SRC_NONE;
String motionIntent = "STOP";       // action the last command asked for
String activeAction = "STOP";       // action currently energised on the bridge
String pendingAction = "";          // action waiting for the H-bridge dead time
int appliedPwm = 0;                 // PWM currently written to ENA/ENB
int motionTargetPwm = 0;            // PWM the envelope allows this interval
unsigned long motionCmdAt = 0;      // when a fresh motion command arrived
unsigned long motorServiceAt = 0;   // service throttle
unsigned long lastRampMs = 0;
unsigned long bridgeHoldUntil = 0;  // dead-time / settle window
bool failsafeTripped = false;
String motionBlockedBy = "";        // "", "obstacle", "failsafe"
bool avoidanceBypassActive = false; // autonomous detour is creeping past a plant

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
long readUltrasonic(int echoPin, String sensorName, bool quiet = false, uint32_t timeoutUs = 30000);
void captureAndUploadImage(const String &side = "manual", int scanPoint = -1);
void loadAutonomousMission(JsonObject data);
void beginAutonomousMissionTransfer(JsonObject data);
void appendAutonomousMissionChunk(JsonObject data);
void finishAutonomousMissionTransfer(JsonObject data);
void navigateMission();
void serviceAvoidance();
void captureBothSides(int scanPoint);
void sendMissionUpdate(const char *type, const char *state, const String &message = "");
void playBootAnimation();
void showBootStage(const char *caption, int progressPct);
void updateStatusDisplay(bool force = false);
void sendCameraFault(const String &reason);
bool probeCameraConnection();
String readCameraDiagnostic();
// Safety / storage helpers (defined further down, used across the sketch).
void serviceMotors();
void setMotionSource(MotionSource source);
void updateSafetySensors();
static const char *avoidStateName();
void handleFieldMap(JsonVariantConst data);
void applyMotionConfig(JsonVariantConst data);
void sendMotionStatus(const char *reason);
void sendMapStatus(const char *reason);
void sendDeviceHello();
bool storageInit();
bool loadCachedMapMeta();
bool saveFieldMapToCache(JsonVariantConst data, const String &rev);
bool loadMotionConfigFromCache();
bool saveMotionConfigToCache();

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
    // gpsHasFix() also answers false for a 0,0 fix - and the row below says
    // PLACEHOLDER, so the operator can see why the position is not from the sky.
    bool gpsFix = gpsHasFix();

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
    display.printf("WS %s MAP %s", socketConnected ? "OK " : "OFF",
                   cachedMapRev.length() ? cachedMapRev.substring(0, 6).c_str() : "none");

    // GPS + camera row. Camera state is based on a real boot-time frame,
    // not merely on the UART pins being configured.
    display.setCursor(0, 43);
    if (gpsFix) display.printf("GPS FIX %d ", (int)(gps.satellites.isValid() ? gps.satellites.value() : 0));
#if GPS_FALLBACK_ENABLED
    else display.print("GPS NOFIX PLH ");
#else
    else display.print("GPS NO FIX ");
#endif
    if (!cameraStatusKnown) display.print("CAM ?");
    else display.print(cameraConnected ? "CAM OK" : "CAM ERR");

    // Mode / activity row.
    display.setCursor(0, 53);
    if (captureInProgress) display.print("CAPTURING PHOTO...");
    else if (millis() < camFaultUntil) display.print("CAM ERROR! CHECK CAM");
    else if (avoidState != AVOID_STATE_CLEAR) {
        // Arc planner at work: which way it steers and how far the plant is.
        switch (avoidState) {
            case AVOID_STATE_STEER_LEFT:  display.printf("AVOID < %ldcm", latestFrontCm); break;
            case AVOID_STATE_STEER_RIGHT: display.printf("AVOID > %ldcm", latestFrontCm); break;
            case AVOID_STATE_CREEP:       display.printf("CREEP PAST %ldcm", latestFrontCm); break;
            case AVOID_STATE_TURN_BACK:   display.print("TURN BACK ON PATH"); break;
            case AVOID_STATE_NO_PATH:     display.print("NO PATH - STOPPED"); break;
            default:                      display.print("EMERGENCY STOP"); break;
        }
    }
    else if (autonomousActive && !autonomousPaused)
        display.printf("AUTO WP %d/%d", currentWaypointIndex + 1, missionWaypointCount);
    else if (autonomousActive && autonomousPaused) display.print("AUTO PAUSED");
    else {
        display.print("MANUAL");
        // Live duty cycle - the operator sees immediately that the panel's
        // speed limit (and the obstacle envelope) really took effect.
        if (appliedPwm > 0 || motionIntent != "STOP") display.printf(" %d%%", appliedPwm * 100 / 255);
        else if (currentBlockName.length() > 0) display.printf(" %s", currentBlockName.c_str());
    }
    display.display();
}

/* ==========================================================================
 * MOTION ENGINE - slow, ramped, and always able to stop in time
 * --------------------------------------------------------------------------
 * The previous build wrote 200/180 PWM straight to ENA/ENB and blocked the
 * main loop with delay() while it ramped and reversed direction. This engine:
 *   * scales every speed down (percent of the safe defaults + hard ceiling),
 *   * crawls when a plant is inside the slow-distance band,
 *   * refuses to move forward at all inside the stop distance,
 *   * steps the PWM instead of jumping, and brakes instantly on STOP,
 *   * cuts the motors when fresh commands stop arriving (dead-man failsafe),
 *   * never blocks the loop, so STOP and a dropped Wi-Fi link take effect
 *     immediately instead of after a delay().
 * ========================================================================== */

// Percent (0-100) of a base PWM, floored at the motor's stall threshold and
// hard-capped at MOTION_HARD_MAX_PWM. 0 % means "do not drive".
static int scalePwm(int percent, int basePwm) {
    if (percent <= 0) return 0;
    const int pct = constrain(percent, 0, MOTION_MAX_PERCENT);
    int pwm = (basePwm * pct + 50) / 100;
    if (pwm <= 0) return 0;
    if (pwm < MOTION_MIN_PWM) pwm = MOTION_MIN_PWM;
    if (pwm > MOTION_HARD_MAX_PWM) pwm = MOTION_HARD_MAX_PWM;
    return pwm;
}

static int drivePwm() { return scalePwm(motionDrivePercent, MOTION_CRUISE_PWM); }
static int turnPwm() { return scalePwm(motionTurnPercent, MOTION_TURN_PWM); }

static int reversePwm() {
    const int pwm = scalePwm(motionDrivePercent, MOTION_REVERSE_PWM);
    const int drive = drivePwm();
    return (drive > 0 && pwm > drive) ? drive : pwm;
}

// Crawl speed for the "a plant is close" band - never faster than cruise.
static int crawlPwm() {
    int pwm = scalePwm(motionCrawlPercent, MOTION_CRAWL_PWM);
    if (pwm <= 0) pwm = MOTION_MIN_PWM;
    const int drive = drivePwm();
    if (drive <= 0) return 0;
    return (pwm > drive) ? drive : pwm;
}

// The bypass legs are time based. When the panel lowers the turn speed the
// legs are stretched, so the rover still turns the same physical angle.
static unsigned long turnScaledMs(unsigned long baseMs) {
    const int pwm = turnPwm();
    if (pwm <= 0) return baseMs;
    const double scaled = (double)baseMs * (double)MOTION_TURN_PWM / (double)pwm;
    unsigned long result = (unsigned long)scaled;
    if (result < baseMs / 4) result = baseMs / 4;
    if (result > baseMs * 4) result = baseMs * 4;
    return result;
}

void setMotionSource(MotionSource source) {
    if (source != SRC_AUTO) avoidanceBypassActive = false; // manual control cancels a bypass
    if (motionSource == source) return;
    motionSource = source;
    static const char *names[] = { "none", "manual", "auto" };
    Serial.printf("[SAFETY] Motion source -> %s\n", names[(int)source]);
}

// Speed the obstacle envelope allows for the requested action. Sets mustStop
// when the rover is too close to move at all.
static int envelopePwm(const String &action, bool &mustStop) {
    mustStop = false;
    if (action == "FORWARD") {
        if (avoidanceBypassActive) {
            // The autonomous detour must creep past the plant it just turned
            // away from; only a genuine emergency stop can interrupt it.
            if (latestFrontCm <= OBSTACLE_EMERGENCY_CM) { mustStop = true; return 0; }
            return crawlPwm();
        }
        if (latestFrontCm <= OBSTACLE_STOP_CM) { mustStop = true; return 0; }
        if (latestFrontCm <= OBSTACLE_CRAWL_CM) return crawlPwm();
        if (latestFrontCm <= OBSTACLE_SLOW_CM) return (drivePwm() + crawlPwm()) / 2;
        return drivePwm();
    }
    if (action == "LEFT" || action == "RIGHT") {
        const long side = (action == "LEFT") ? latestLeftCm : latestRightCm;
        if (side <= OBSTACLE_SIDE_STOP_CM) { mustStop = true; return 0; }
        if (side <= OBSTACLE_SLOW_CM) return (turnPwm() + MOTION_MIN_PWM) / 2;
        return turnPwm();
    }
    if (action == "BACKWARD") return reversePwm();
    return 0;
}

// Write a direction on the H-bridge without energising it yet.
static void setBridgeDirection(const String &action) {
    if (action == "FORWARD") {
        digitalWrite(IN1_PIN, HIGH); digitalWrite(IN2_PIN, LOW);
        digitalWrite(IN3_PIN, HIGH); digitalWrite(IN4_PIN, LOW);
    } else if (action == "BACKWARD") {
        digitalWrite(IN1_PIN, LOW); digitalWrite(IN2_PIN, HIGH);
        digitalWrite(IN3_PIN, LOW); digitalWrite(IN4_PIN, HIGH);
    } else if (action == "LEFT") {
        digitalWrite(IN1_PIN, LOW); digitalWrite(IN2_PIN, HIGH);
        digitalWrite(IN3_PIN, HIGH); digitalWrite(IN4_PIN, LOW);
    } else if (action == "RIGHT") {
        digitalWrite(IN1_PIN, HIGH); digitalWrite(IN2_PIN, LOW);
        digitalWrite(IN3_PIN, LOW); digitalWrite(IN4_PIN, HIGH);
    } else {
        digitalWrite(IN1_PIN, LOW); digitalWrite(IN2_PIN, LOW);
        digitalWrite(IN3_PIN, LOW); digitalWrite(IN4_PIN, LOW);
    }
}

// Remove drive without touching the direction inputs (fast decay).
static void cutMotorOutput() {
    analogWrite(ENA_PIN, 0);
    analogWrite(ENB_PIN, 0);
    appliedPwm = 0;
}

// Full, immediate stop: PWM to zero first, then release the bridge.
static void stopBridgeNow() {
    cutMotorOutput();
    setBridgeDirection("STOP");
    activeAction = "STOP";
    pendingAction = "";
}

/* The one place that decides what the wheels do. Called from loop(). */
void serviceMotors() {
    const unsigned long now = millis();
    if (now - motorServiceAt < MOTION_SERVICE_INTERVAL_MS) return;
    motorServiceAt = now;

    // 1. Dead-man failsafe - a stale command may never keep the wheels turning.
    if (motionIntent != "STOP") {
        const unsigned long budget = (motionSource == SRC_AUTO) ? AUTO_FAILSAFE_MS : DRIVE_FAILSAFE_MS;
        if (motionCmdAt == 0 || (now - motionCmdAt) > budget) {
            motionIntent = "STOP";
            stopBridgeNow();
            motionTargetPwm = 0;
            if (!failsafeTripped) {
                failsafeTripped = true;
                motionBlockedBy = "failsafe";
                Serial.printf("[SAFETY] No fresh %s command for %lu ms - motors cut.\n",
                              motionSource == SRC_AUTO ? "autonomous" : "drive", budget);
                sendMotionStatus("failsafe");
            }
            return;
        }
    }

    // 2. Resolve what the envelope allows right now.
    if (motionIntent == "STOP") {
        if (activeAction != "STOP" || appliedPwm != 0) stopBridgeNow();
        motionTargetPwm = 0;
        return;
    }

    // The arc planner may redirect a plain "drive forward" into a turn away from
    // a plant that sits inside the forward beam. What the operator asked for is
    // kept in motionIntent (telemetry keeps reporting FORWARD); only the wheels
    // turn, so the rover drives around the plant instead of stopping at it.
    String effective = motionIntent;
    if (motionIntent == "FORWARD") {
        if (avoidState == AVOID_STATE_STEER_LEFT) effective = "LEFT";
        else if (avoidState == AVOID_STATE_STEER_RIGHT) effective = "RIGHT";
    }

    bool mustStop = false;
    const int wantedPwm = envelopePwm(effective, mustStop);
    if (mustStop || wantedPwm <= 0) {
        const long distance = effective == "FORWARD" ? latestFrontCm
                              : (effective == "LEFT" ? latestLeftCm : latestRightCm);
        const bool firstReport = motionBlockedBy != "obstacle";
        motionIntent = "STOP";
        stopBridgeNow();
        motionTargetPwm = 0;
        if (firstReport) {
            motionBlockedBy = "obstacle";
            Serial.printf("[SAFETY] Too close (%ld cm) to keep moving - stopped in place.\n", distance);
            sendMotionStatus("obstacle");
        }
        return;
    }
    // The planner owns motionBlockedBy while it is steering; the envelope only
    // reports a plain "obstacle" stop when nothing is steering the rover.
    if (avoidState == AVOID_STATE_CLEAR) motionBlockedBy = "";
    motionTargetPwm = wantedPwm;

    // 3. Direction change: brake, wait out the H-bridge dead time, settle, ramp.
    if (activeAction != effective) {
        if (pendingAction != effective) {
            cutMotorOutput();
            setBridgeDirection("STOP");
            activeAction = "STOP";
            pendingAction = effective;
            bridgeHoldUntil = now + MOTION_DEAD_TIME_MS;
            return;
        }
        if ((long)(now - bridgeHoldUntil) < 0) return;
        setBridgeDirection(effective);
        activeAction = effective;
        appliedPwm = 0;
        lastRampMs = now;
        bridgeHoldUntil = now + MOTION_SETTLE_MS;
        pendingAction = "";
        return;
    }
    if ((long)(now - bridgeHoldUntil) < 0) return;

    // 4. Ramp - PWM is stepped in both directions, never jumped to.
    if (appliedPwm < motionTargetPwm) {
        if (now - lastRampMs >= MOTION_RAMP_UP_MS) {
            lastRampMs = now;
            appliedPwm = min(appliedPwm + MOTION_RAMP_UP_STEP, motionTargetPwm);
        }
    } else if (appliedPwm > motionTargetPwm) {
        if (now - lastRampMs >= MOTION_RAMP_DOWN_MS) {
            lastRampMs = now;
            appliedPwm = max(appliedPwm - MOTION_RAMP_DOWN_STEP, motionTargetPwm);
        }
    }
    analogWrite(ENA_PIN, appliedPwm);
    analogWrite(ENB_PIN, appliedPwm);
}

/* Scan the front arc far more often than the 5 s telemetry tick so both the
 * envelope and the planner always work with fresh distances.
 *
 * Three HC-SR04 share one trigger pin, so they must be triggered ONE at a
 * time (fired together they hear each other's burst and report nonsense). The
 * arc is therefore scanned one sensor per slot, centre first and twice as
 * often:  slot 0 centre | slot 1 left | slot 2 centre | slot 3 right.
 * A whole left+centre+right cycle takes 4 * SENSOR_SLOT_MS (240 ms by default)
 * and the centre beam - the one that decides a stop - is never older than
 * 2 * SENSOR_SLOT_MS. */
void updateSafetySensors() {
    const unsigned long now = millis();
    const bool moving = (motionIntent != "STOP") || (autonomousActive && !autonomousPaused) ||
                        avoidState != AVOID_STATE_CLEAR;
    const unsigned long slotMs = moving ? SENSOR_SLOT_MS : SENSOR_IDLE_SLOT_MS;
    if (now - arcSlotAt < slotMs) return;
    arcSlotAt = now;
    switch (arcSlot) {
        case ARC_SLOT_LEFT:
            latestLeftCm = readUltrasonic(ECHO_LEFT_PIN, "ARC-LEFT", true, ULTRASONIC_SAFETY_TIMEOUT_US);
            break;
        case ARC_SLOT_RIGHT:
            latestRightCm = readUltrasonic(ECHO_RIGHT_PIN, "ARC-RIGHT", true, ULTRASONIC_SAFETY_TIMEOUT_US);
            break;
        default:
            latestFrontCm = readUltrasonic(ECHO_FORWARD_PIN, "ARC-CENTRE", true, ULTRASONIC_SAFETY_TIMEOUT_US);
            break;
    }
    arcSlot = (uint8_t)((arcSlot + 1) & 0x03);
}

/* ==========================================================================
 * FRONT SENSOR ARC PLANNER
 * --------------------------------------------------------------------------
 * Geometry first. The two side sensors sit on brackets that splay them
 * outwards, so together with the centre sensor they watch one continuous fan
 * instead of three narrow beams. A reading on its own says nothing, so every
 * sample is converted into the two numbers that matter for driving:
 *
 *     lateral room  = reading * sin(angle + half cone)
 *     forward reach = reading * cos(angle - half cone)
 *
 * "lateral room" is how wide the gap on that side is (the cone's outer edge is
 * what actually covers the front corner), "forward reach" is how soon
 * something on that ray could be in the rover's way. A gap only counts as
 * passable when it is wider than ROVER_HALF_WIDTH_CM + OBSTACLE_SIDE_MARGIN_CM.
 * ======================================================================== */

/* The geometry helpers (angles -> lateral room / forward reach) and the whole
 * decision table live in include/arc_math.h, which has no Arduino dependency
 * and is unit tested on a PC by scripts/test-arc-math.sh:
 *
 *     ./scripts/test-arc-math.sh
 *
 * Keep the maths there and the driving policy here. */

static const char *avoidStateName() {
    switch (avoidState) {
        case AVOID_STATE_STEER_LEFT:  return "steer-left";
        case AVOID_STATE_STEER_RIGHT: return "steer-right";
        case AVOID_STATE_CREEP:       return "creep";
        case AVOID_STATE_TURN_BACK:   return "turn-back";
        case AVOID_STATE_NO_PATH:     return "no-path";
        case AVOID_STATE_EMERGENCY:   return "emergency";
        default:                      return "clear";
    }
}

// Announce an avoidance state change. Only transitions are sent (never one
// message per scan), and the server turns them into dashboard alerts.
static void reportAvoidState(const char *reason, const char *blockedBy) {
    motionBlockedBy = blockedBy;
    if (avoidReportedState == reason) return;
    avoidReportedState = reason;
    Serial.printf("[AVOID] %s: state=%s blockedBy=%s F/L/R=%ld/%ld/%ld cm gaps L/R=%.0f/%.0f cm\n",
                  reason, avoidStateName(), blockedBy, latestFrontCm, latestLeftCm, latestRightCm,
                  gapLeftCm, gapRightCm);
    sendMotionStatus(reason);
}

// The one planned stop of the whole planner: nothing wide enough to pass.
static void stopForNoPath() {
    if (motionIntent != "STOP") controlMotors("STOP");
    reportAvoidState("no-path", "no-path");
}

// Picking the more open side (arcChooseDir) and naming the blocking side
// (arcBlockedSideName) both come from include/arc_math.h.

/* Autonomous detour: turn out of the way, creep past the plant, swing back
 * onto the mission heading. Every leg is time-boxed, is scaled by the panel's
 * turn speed (so a slower rover still turns the same angle) and re-plans live
 * when the side it is turning into disappears. */
static void serviceAvoidManoeuvre(const unsigned long now, bool leftOpen, bool rightOpen) {
    // A detour keeps its own command alive: the autonomous failsafe measures how
    // fresh the motion intent is, and a long creep leg must not trip it.
    if (motionIntent != "STOP" && (now - motionCmdAt) > 400) motionCmdAt = now;
    const bool legOver = ((long)(now - avoidLegDeadline) >= 0);
    const String turnAway = (avoidDir > 0) ? "RIGHT" : "LEFT";
    const String turnBack = (avoidDir > 0) ? "LEFT" : "RIGHT";

    if (avoidState == AVOID_STATE_STEER_LEFT || avoidState == AVOID_STATE_STEER_RIGHT) {
        avoidanceBypassActive = false;   // only the creep leg relaxes the stop band
        const bool sideOpen = (avoidDir > 0) ? rightOpen : leftOpen;
        if (sideOpen && latestFrontCm <= OBSTACLE_CRAWL_CM && !legOver) {
            controlMotors(turnAway);     // still needs to turn further out
            return;
        }
        // Turned far enough, or the side it was heading for is gone: creep past
        // at crawl speed (the envelope keeps the emergency ring active).
        avoidanceBypassActive = true;
        avoidState = AVOID_STATE_CREEP;
        avoidLegDeadline = now + turnScaledMs(AVOID_PASS_MS);
        reportAvoidState("creep", arcBlockedSideName(leftOpen, rightOpen));
        controlMotors("FORWARD");
        return;
    }

    if (avoidState == AVOID_STATE_CREEP) {
        avoidanceBypassActive = true;
        // Something new appeared inside the bypass line: re-plan toward
        // whatever is open now (rate-limited so the rover commits to a leg).
        if (latestFrontCm <= OBSTACLE_BYPASS_REPLAN_CM && (long)(now - avoidReplanAt) >= 0) {
            avoidReplanAt = now + AVOID_REPLAN_COOLDOWN_MS;
            if (!leftOpen && !rightOpen) {
                avoidanceOwnsMotion = false;
                avoidanceBypassActive = false;
                avoidState = AVOID_STATE_NO_PATH;
                stopForNoPath();
                return;
            }
            avoidDir = arcChooseDir(leftOpen, rightOpen, gapLeftCm, gapRightCm);
            avoidState = (avoidDir > 0) ? AVOID_STATE_STEER_RIGHT : AVOID_STATE_STEER_LEFT;
            avoidLegDeadline = now + turnScaledMs(AVOID_TURN_OUT_MS);
            reportAvoidState("avoiding", arcBlockedSideName(leftOpen, rightOpen));
            controlMotors(avoidDir > 0 ? "RIGHT" : "LEFT");
            return;
        }
        if (!legOver) { controlMotors("FORWARD"); return; }
        // Cleared the plant: swing back onto the mission heading.
        avoidState = AVOID_STATE_TURN_BACK;
        avoidLegDeadline = now + turnScaledMs(AVOID_TURN_BACK_MS);
        reportAvoidState("turn-back", "");
        controlMotors(turnBack);
        return;
    }

    if (avoidState == AVOID_STATE_TURN_BACK) {
        avoidanceBypassActive = false;
        if (!legOver) { controlMotors(turnBack); return; }
        avoidDir = 0;
        avoidState = AVOID_STATE_CLEAR;
        avoidanceOwnsMotion = false;
        reportAvoidState("clear", "");
        controlMotors("STOP");   // hand the wheels back to navigateMission()
        return;
    }

    // NO_PATH / EMERGENCY while a detour was running: release the mission
    // planner; the envelope keeps the rover braked until the path is clear.
    avoidanceOwnsMotion = false;
    avoidanceBypassActive = false;
}

/* The planner itself. Runs on every loop tick (after updateSafetySensors()),
 * decides whether the rover should steer around something, and leaves the
 * actual PWM to serviceMotors(). Called in BOTH modes: autonomous detours and
 * the manual assist share one decision table, so there is exactly one place
 * where "go around, stop only if you really cannot" is implemented. */
void serviceAvoidance() {
    const unsigned long now = millis();

    // --- geometry: three raw readings -> one verdict (include/arc_math.h) --
    const ArcDecision arc = arcEvaluate({ latestFrontCm, latestLeftCm, latestRightCm },
                                        motionAngleLeftDeg, motionAngleRightDeg);
    gapLeftCm  = arc.gapLeftCm;
    gapRightCm = arc.gapRightCm;
    const bool emergency = arc.emergency;
    const bool aheadBlocked = arc.aheadBlocked;
    const bool leftOpen = arc.leftOpen;
    const bool rightOpen = arc.rightOpen;
    const bool cornerIntrusion = arc.cornerIntrusion;
    const float passable = arc.passableCm;

    // --- 1. emergency ring: the only unconditional brake --------------------
    if (emergency) {
        if (avoidState != AVOID_STATE_EMERGENCY) {
            avoidState = AVOID_STATE_EMERGENCY;
            avoidDir = 0;
            avoidanceOwnsMotion = false;
            avoidanceBypassActive = false;
            Serial.printf("[SAFETY] Emergency ring: %ld cm ahead - braking.\n", latestFrontCm);
            if (motionIntent != "STOP") controlMotors("STOP");
            reportAvoidState("emergency", "emergency");
        }
        return;
    }
    if (avoidState == AVOID_STATE_EMERGENCY) {   // ring left behind
        avoidState = AVOID_STATE_CLEAR;
        reportAvoidState("clear", "");
    }

    // --- 2. a running detour keeps the wheels ------------------------------
    if (avoidanceOwnsMotion) { serviceAvoidManoeuvre(now, leftOpen, rightOpen); return; }

    // --- 3. hysteresis: confirm a blockage before acting on it -------------
    const bool deciding = aheadBlocked || cornerIntrusion;
    if (deciding) { if (avoidHits < 255) avoidHits++; avoidClearHits = 0; }
    else { if (avoidClearHits < 255) avoidClearHits++; avoidHits = 0; }
    const bool confirmed = (avoidHits >= AVOID_CONFIRM_SCANS);
    const bool autonomousRunning = autonomousActive && !autonomousPaused;
    const bool wantsForward = (motionIntent == "FORWARD");
    const bool manoeuvring = (avoidState == AVOID_STATE_STEER_LEFT || avoidState == AVOID_STATE_STEER_RIGHT ||
                              avoidState == AVOID_STATE_CREEP || avoidState == AVOID_STATE_NO_PATH);

    if (!confirmed) {
        if (!deciding && avoidClearHits >= AVOID_CLEAR_SCANS && manoeuvring) {
            avoidState = AVOID_STATE_CLEAR;
            avoidDir = 0;
            avoidanceBypassActive = false;
            reportAvoidState("clear", "");
        }
        return;
    }

    if (!autonomousRunning && (!motionAvoidAssist || !wantsForward)) {
        // Manual driving with the assist switched off (or the operator is not
        // driving forward): the envelope still slows and stops the rover, but
        // the operator - not the firmware - decides where it goes.
        if (manoeuvring) {
            avoidState = AVOID_STATE_CLEAR;
            avoidDir = 0;
            avoidanceBypassActive = false;
            reportAvoidState("clear", "");
        }
        return;
    }

    // --- 4. nowhere to go: the one planned stop ----------------------------
    if (!leftOpen && !rightOpen) {
        if (avoidState != AVOID_STATE_NO_PATH) {
            avoidState = AVOID_STATE_NO_PATH;
            avoidDir = 0;
            avoidanceBypassActive = false;
            Serial.printf("[SAFETY] No gap wide enough to pass (gaps L/R=%.0f/%.0f cm, need %.0f cm).\n",
                          gapLeftCm, gapRightCm, passable);
        }
        stopForNoPath();
        return;
    }

    const int dir = arc.steerDir;
    avoidDir = dir;

    if (autonomousRunning) {
        // Full detour. The mission resumes by itself when the detour hands the
        // wheels back, so the rover keeps following its waypoints.
        avoidState = (dir > 0) ? AVOID_STATE_STEER_RIGHT : AVOID_STATE_STEER_LEFT;
        avoidLegDeadline = now + turnScaledMs(aheadBlocked ? AVOID_TURN_OUT_MS : (AVOID_TURN_OUT_MS / 2));
        avoidanceBypassActive = false;
        avoidanceOwnsMotion = true;
        reportAvoidState("avoiding", arcBlockedSideName(leftOpen, rightOpen));
        controlMotors(dir > 0 ? "RIGHT" : "LEFT");
        return;
    }

    // --- 5. manual assist: steer a held FORWARD around the plant -----------
    const AvoidState steerState = (dir > 0) ? AVOID_STATE_STEER_RIGHT : AVOID_STATE_STEER_LEFT;
    if (avoidState == AVOID_STATE_CREEP) {
        avoidanceBypassActive = true;
        if ((long)(now - avoidLegDeadline) < 0) return;
        // Creeped long enough: look at the arc again with fresh eyes.
        avoidState = AVOID_STATE_CLEAR;
        avoidHits = 0;
        avoidClearHits = 0;
        avoidanceBypassActive = false;
        return;
    }
    if (avoidState != steerState) {
        avoidState = steerState;
        avoidSteerUntil = now + (unsigned long)AVOID_MAX_TURN_MS;
        avoidanceBypassActive = false;
        Serial.printf("[ASSIST] %s blocked %ld cm ahead - steering %s (gaps L/R=%.0f/%.0f cm).\n",
                      aheadBlocked ? "Path" : "Corner", latestFrontCm, (dir > 0) ? "right" : "left",
                      gapLeftCm, gapRightCm);
        reportAvoidState("avoiding", arcBlockedSideName(leftOpen, rightOpen));
    }
    if ((long)(now - avoidSteerUntil) >= 0) {
        // Turned as far as it usefully can without the path opening: creep
        // forward past the plant instead of spinning on the spot forever.
        avoidState = AVOID_STATE_CREEP;
        avoidanceBypassActive = true;
        avoidLegDeadline = now + turnScaledMs(AVOID_PASS_MS);
        reportAvoidState("creep", arcBlockedSideName(leftOpen, rightOpen));
    }
}

/* ==========================================================================
 * microSD CACHE - the field map and the speed limits survive a reboot
 * ========================================================================== */

static String readTextFile(const char *path, size_t maxBytes) {
    String out = "";
    File f = SD.open(path, FILE_READ);
    if (!f) return out;
    while (f.available() && out.length() < maxBytes) out += (char)f.read();
    f.close();
    return out;
}

bool storageInit() {
#if SD_ENABLED
    // Wiring matches the Wokwi diagram: CS=5, SCK=33, MISO=36(VP), MOSI=2.
    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    if (!SD.begin(SD_CS_PIN, SPI, SD_SPI_HZ)) {
        sdOk = false;
        Serial.println("[SD] Card not mounted - the field map is re-downloaded on every boot.");
        return false;
    }
    sdOk = true;
    if (!SD.exists(CACHE_DIR)) SD.mkdir(CACHE_DIR);
    Serial.printf("[SD] Card mounted. Cache directory: %s\n", CACHE_DIR);
    return true;
#else
    sdOk = false;
    Serial.println("[SD] SD caching disabled by config.");
    return false;
#endif
}

bool loadCachedMapMeta() {
    cachedMapRev = "";
    cachedMapName = "";
    cachedMapBlocks = 0;
    cachedMapBytes = 0;
    if (!sdOk) return false;
    if (!SD.exists(MAP_FILE)) {
        if (SD.exists(MAP_META_FILE)) SD.remove(MAP_META_FILE); // orphaned meta file
        Serial.println("[MAP] No cached field map on the SD card.");
        return false;
    }
    File f = SD.open(MAP_FILE, FILE_READ);
    if (f) { cachedMapBytes = f.size(); f.close(); }
    if (cachedMapBytes == 0) {
        SD.remove(MAP_FILE);
        Serial.println("[MAP] Cached map file is empty - removed.");
        return false;
    }
    const String meta = readTextFile(MAP_META_FILE, 512);
    if (meta.length()) {
        StaticJsonDocument<384> doc;
        if (!deserializeJson(doc, meta)) {
            cachedMapRev = String((const char *)(doc["rev"] | ""));
            cachedMapName = String((const char *)(doc["name"] | ""));
            cachedMapBlocks = (int)(doc["blocks"] | 0);
        }
    }
    cachedMapLoadedAt = millis();
    Serial.printf("[MAP] Cached field map on SD: name=%s blocks=%d bytes=%u rev=%s\n",
                  cachedMapName.length() ? cachedMapName.c_str() : "(unnamed)",
                  cachedMapBlocks, (unsigned)cachedMapBytes,
                  cachedMapRev.length() ? cachedMapRev.c_str() : "(none)");
    return cachedMapRev.length() > 0;
}

bool saveFieldMapToCache(JsonVariantConst data, const String &rev) {
    if (!sdOk) return false;
    if (SD.exists(MAP_TMP_FILE)) SD.remove(MAP_TMP_FILE);
    File f = SD.open(MAP_TMP_FILE, FILE_WRITE);
    if (!f) {
        Serial.println("[MAP] SD write failed - keeping the previous cache.");
        return false;
    }
    const size_t written = serializeJson(data, f);
    f.close();
    if (written == 0) {
        SD.remove(MAP_TMP_FILE);
        Serial.println("[MAP] Empty field map received - cache not replaced.");
        return false;
    }
    // Write-then-rename: a power cut can never leave a half-written map behind.
    SD.remove(MAP_FILE);
    if (!SD.rename(MAP_TMP_FILE, MAP_FILE)) {
        Serial.println("[MAP] Could not move the temporary file into place.");
        return false;
    }
    cachedMapBytes = (uint32_t)written;
    StaticJsonDocument<256> meta;
    meta["rev"] = rev;
    meta["name"] = String((const char *)(data["name"] | ""));
    meta["blocks"] = data["blocks"].is<JsonArray>() ? (int)data["blocks"].size() : 0;
    meta["bytes"] = (uint32_t)written;
    File m = SD.open(MAP_META_FILE, FILE_WRITE);
    if (m) { serializeJson(meta, m); m.close(); }
    return true;
}

bool saveMotionConfigToCache() {
    if (!sdOk) return false;
    StaticJsonDocument<320> doc;
    doc["driveSpeedPercent"] = motionDrivePercent;
    doc["turnSpeedPercent"] = motionTurnPercent;
    doc["crawlSpeedPercent"] = motionCrawlPercent;
    doc["sensorAngleLeftDeg"] = motionAngleLeftDeg;
    doc["sensorAngleRightDeg"] = motionAngleRightDeg;
    doc["avoidAssist"] = motionAvoidAssist;
    doc["firmware"] = FW_VERSION;
    File f = SD.open(MOTION_CFG_FILE, FILE_WRITE);
    if (!f) return false;
    serializeJson(doc, f);
    f.close();
    return true;
}

bool loadMotionConfigFromCache() {
    if (!sdOk) return false;
    const String raw = readTextFile(MOTION_CFG_FILE, 384);
    if (!raw.length()) return false;
    StaticJsonDocument<320> doc;
    if (deserializeJson(doc, raw)) return false;
    motionDrivePercent = constrain((int)(doc["driveSpeedPercent"] | motionDrivePercent), 0, MOTION_MAX_PERCENT);
    motionTurnPercent = constrain((int)(doc["turnSpeedPercent"] | motionTurnPercent), 0, MOTION_MAX_PERCENT);
    motionCrawlPercent = constrain((int)(doc["crawlSpeedPercent"] | motionCrawlPercent), 0, MOTION_MAX_PERCENT);
    motionAngleLeftDeg = arcClampAngleDeg((int)(doc["sensorAngleLeftDeg"] | motionAngleLeftDeg));
    motionAngleRightDeg = arcClampAngleDeg((int)(doc["sensorAngleRightDeg"] | motionAngleRightDeg));
    motionAvoidAssist = doc["avoidAssist"] | motionAvoidAssist;
    Serial.printf("[MOTION] Config restored from SD: drive=%d%% (%d PWM) turn=%d%% (%d PWM) angles L/R=%d/%d deg assist=%d\n",
                  motionDrivePercent, drivePwm(), motionTurnPercent, turnPwm(),
                  motionAngleLeftDeg, motionAngleRightDeg, (int)motionAvoidAssist);
    return true;
}

/* ==========================================================================
 * SERVER MESSAGES - hello handshake, speed config, map status
 * ========================================================================== */

// Sent right after Socket.IO connects. The server compares mapRev with the
// revision of the field map it has stored and only then decides whether the
// rover still needs the (comparatively large) field_map payload at all.
void sendDeviceHello() {
    DynamicJsonDocument doc(640);
    JsonArray event = doc.to<JsonArray>();
    event.add("device_hello");
    JsonObject hello = event.createNestedObject();
    hello["deviceId"] = DEVICE_ID;
    hello["role"] = DEVICE_ROLE;
    hello["firmware"] = FW_VERSION;
    // The admin panel matches images to boards with this value (see config.h).
    hello["fwTarget"] = FW_TARGET;
    hello["mapRev"] = cachedMapRev;
    hello["mapBlocks"] = cachedMapBlocks;
    hello["mapBytes"] = cachedMapBytes;
    hello["sd"] = sdOk;
    hello["driveSpeedPercent"] = motionDrivePercent;
    hello["turnSpeedPercent"] = motionTurnPercent;
    hello["sensorAngleLeftDeg"] = motionAngleLeftDeg;
    hello["sensorAngleRightDeg"] = motionAngleRightDeg;
    hello["avoidAssist"] = motionAvoidAssist;
    String output;
    serializeJson(doc, output);
    socketIO.sendEVENT(output);
    Serial.printf("[IOc] Hello sent (firmware=%s, mapRev=%s, sd=%d)\n", FW_VERSION,
                  cachedMapRev.length() ? cachedMapRev.c_str() : "none", (int)sdOk);
}

// ---------------------------------------------------------------------------
// OTA firmware update (admin panel -> chrserver -> this board)
//
// The panel queues an image; the server sends this board an `ota` command and
// then serves the file itself, because the rover usually reaches the field
// gateway and not the internet. This side only ever accepts an image for its own
// FW_TARGET - a build for another board is refused here as well as on the
// server, so a pump image can never be flashed onto the rover.
//
// Safety: motors are stopped and the rover is handed to manual BEFORE the
// download starts, and nothing moves again while the flash runs (the update
// blocks the loop, then the board reboots into the new firmware).
// ---------------------------------------------------------------------------
#if OTA_ENABLED
static String otaVersion = "";
static int otaReportedPercent = -1;

void sendOtaStatus(const char *status, const char *reason = "", int percent = -1) {
    Serial.printf("[OTA] %s%s%s\n", status, reason && *reason ? " - " : "", reason ? reason : "");
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
    if (!OTA_ENABLED) {
        sendOtaStatus("ignored", "OTA is disabled in this build");
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        sendOtaStatus("failed", "no Wi-Fi");
        return false;
    }
    if (path.length() == 0) {
        sendOtaStatus("failed", "the command carried no download path");
        return false;
    }
    // Only ever the server this board already talks to (gateway or hosted).
    const String url = serverBaseUrl + path;
    if (!url.startsWith("http")) {
        sendOtaStatus("failed", "no server address yet");
        return false;
    }

    // Stop first: flashing never happens with the motors live.
    setMotionSource(SRC_MANUAL);
    autonomousPaused = true;
    controlMotors("STOP");
    delay(200);

    // Two app slots are required for OTA. A single-slot partition scheme (e.g.
    // "Huge APP") has nowhere to put the image - say so instead of half flashing.
    if (size > 0 && (long)ESP.getFreeSketchSpace() < size) {
        sendOtaStatus("failed", "not enough flash space - use a partition scheme with two app slots");
        return false;
    }

    otaVersion = version;
    otaReportedPercent = -1;
    sendOtaStatus("starting", "");

    httpUpdate.rebootOnUpdate(true);
    httpUpdate.setLedPin(-1);
    httpUpdate.onStart([]() { sendOtaStatus("downloading", ""); });
    httpUpdate.onProgress([](int current, int total) {
        if (total <= 0) return;
        const int percent = (int)((100.0 * current) / total);
        if (percent / 10 == otaReportedPercent / 10) return; // one message per 10 %
        otaReportedPercent = percent;
        Serial.printf("[OTA] %d%% (%d/%d bytes)\n", percent, current, total);
    });
    // onEnd takes no argument in this core version - onError reports failures.
    httpUpdate.onEnd([]() { Serial.println("[OTA] download complete, flashing"); });

    Serial.printf("[OTA] downloading %s (running %s, requested %s, md5 %s)\n", url.c_str(), FW_VERSION,
                  version.length() ? version.c_str() : "?");

    // The board authenticates the download with its own device token. It goes in
    // the URL because the HTTPUpdate API in this core version takes no headers.
    String downloadUrl = url;
    downloadUrl += downloadUrl.indexOf('?') >= 0 ? "&" : "?";
    downloadUrl += "token=";
    downloadUrl += ROBOT_TOKEN;

    if (serverSecure) {
        WiFiClientSecure client;
        client.setInsecure(); // the tunnel certificate is not pinned in the firmware
        switch (httpUpdate.update(client, downloadUrl, FW_VERSION)) {
            case HTTP_UPDATE_FAILED:
                sendOtaStatus("failed", httpUpdate.getLastErrorString().c_str());
                return false;
            case HTTP_UPDATE_NO_UPDATES:
                sendOtaStatus("success", "already up to date"); // server says: this is your build
                return false;
            default:
                break;
        }
    } else {
        WiFiClient client;
        switch (httpUpdate.update(client, downloadUrl, FW_VERSION)) {
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
    // rebootOnUpdate(true) reboots inside update(); reaching this line means the
    // image is written and the reboot follows.
    sendOtaStatus("success", "flashed, rebooting");
    return true;
}
#endif

void sendMotionStatus(const char *reason) {
    if (!socketConnected) return;
    DynamicJsonDocument doc(768);
    JsonArray event = doc.to<JsonArray>();
    event.add("message.upsert");
    JsonObject envelope = event.createNestedObject();
    envelope["Type"] = "motion_config";
    JsonObject message = envelope.createNestedObject("Message");
    message["deviceId"] = DEVICE_ID;
    message["reason"] = reason;
    message["driveSpeedPercent"] = motionDrivePercent;
    message["turnSpeedPercent"] = motionTurnPercent;
    message["drivePwm"] = drivePwm();
    message["turnPwm"] = turnPwm();
    message["cruiseBasePwm"] = MOTION_CRUISE_PWM;
    message["turnBasePwm"] = MOTION_TURN_PWM;
    message["hardMaxPwm"] = MOTION_HARD_MAX_PWM;
    message["appliedPwm"] = appliedPwm;
    message["intent"] = motionIntent;
    message["source"] = motionSource == SRC_AUTO ? "auto" : (motionSource == SRC_MANUAL ? "manual" : "none");
    message["blockedBy"] = motionBlockedBy;
    message["obstacleStopCm"] = OBSTACLE_STOP_CM;
    message["emergencyStopCm"] = OBSTACLE_EMERGENCY_CM;
    message["driveFailsafeMs"] = DRIVE_FAILSAFE_MS;
    // Front-arc state: the panel shows which way the rover is steering around a
    // plant, and the two angles it is actually using.
    message["sensorAngleLeftDeg"] = motionAngleLeftDeg;
    message["sensorAngleRightDeg"] = motionAngleRightDeg;
    message["avoidAssist"] = motionAvoidAssist;
    message["avoidState"] = avoidStateName();
    message["avoidDir"] = avoidDir;
    message["gapLeftCm"] = (int)(gapLeftCm + 0.5f);
    message["gapRightCm"] = (int)(gapRightCm + 0.5f);
    message["frontCm"] = latestFrontCm;
    message["roverHalfWidthCm"] = ROVER_HALF_WIDTH_CM;
    String output;
    serializeJson(doc, output);
    socketIO.sendEVENT(output);
}

void sendMapStatus(const char *reason) {
    if (!socketConnected) return;
    DynamicJsonDocument doc(384);
    JsonArray event = doc.to<JsonArray>();
    event.add("message.upsert");
    JsonObject envelope = event.createNestedObject();
    envelope["Type"] = "map_status";
    JsonObject message = envelope.createNestedObject("Message");
    message["deviceId"] = DEVICE_ID;
    message["reason"] = reason;
    message["rev"] = cachedMapRev;
    message["name"] = cachedMapName;
    message["blocks"] = cachedMapBlocks;
    message["bytes"] = cachedMapBytes;
    message["sd"] = sdOk;
    message["path"] = MAP_FILE;
    String output;
    serializeJson(doc, output);
    socketIO.sendEVENT(output);
}

// A field map arrived from the server: cache it unless we already hold exactly
// this revision, so only a genuinely new map touches the SD card.
void handleFieldMap(JsonVariantConst data) {
    const char *mapName = data["name"] | "(unnamed)";
    const int blockCount = data["blocks"].is<JsonArray>() ? (int)data["blocks"].size() : 0;
    const String rev = String((const char *)(data["rev"] | ""));
    if (rev.length() && rev == cachedMapRev) {
        Serial.printf("[MAP] Field map %s is already cached on SD (rev %s) - download skipped.\n",
                      mapName, rev.c_str());
        sendMapStatus("unchanged");
        return;
    }
    Serial.printf("[MAP] New field map received: %s (%d blocks, rev %s)\n", mapName, blockCount,
                  rev.length() ? rev.c_str() : "none");
    if (!saveFieldMapToCache(data, rev)) {
        Serial.println("[MAP] Not cached to SD - the map stays in effect for this session only.");
        sendMapStatus("save_failed");
        return;
    }
    cachedMapRev = rev;
    cachedMapName = String(mapName);
    cachedMapBlocks = blockCount;
    Serial.printf("[MAP] Stored on SD: %s (%u bytes)\n", MAP_FILE, (unsigned)cachedMapBytes);
    sendMapStatus("saved");
}

// `motion_config` (panel save) and `set_speed` (single value) both land here.
void applyMotionConfig(JsonVariantConst data) {
    int drive = motionDrivePercent;
    int turn = motionTurnPercent;
    const bool singlePercentOnly = data["driveSpeedPercent"].isNull() && !data["percent"].isNull();

    if (!data["driveSpeedPercent"].isNull()) drive = data["driveSpeedPercent"].as<int>();
    else if (!data["percent"].isNull()) drive = data["percent"].as<int>();

    if (!data["turnSpeedPercent"].isNull()) turn = data["turnSpeedPercent"].as<int>();
    else if (singlePercentOnly) {
        // One value for both: keep the turn/drive ratio of the safe defaults.
        const long base = MOTION_DEFAULT_DRIVE_PERCENT > 0 ? MOTION_DEFAULT_DRIVE_PERCENT : 100;
        turn = (int)((long)MOTION_DEFAULT_TURN_PERCENT * constrain(drive, 0, MOTION_MAX_PERCENT) / base);
    }

    // The front-arc geometry travels with the speed limits: the side brackets
    // can be re-bolted at a different angle, and the planner must be told.
    int angleLeft = motionAngleLeftDeg;
    int angleRight = motionAngleRightDeg;
    bool avoidAssist = motionAvoidAssist;
    if (!data["sensorAngleLeftDeg"].isNull()) angleLeft = arcClampAngleDeg(data["sensorAngleLeftDeg"].as<int>());
    if (!data["sensorAngleRightDeg"].isNull()) angleRight = arcClampAngleDeg(data["sensorAngleRightDeg"].as<int>());
    if (!data["avoidAssist"].isNull()) avoidAssist = data["avoidAssist"].as<bool>();

    drive = constrain(drive, 0, MOTION_MAX_PERCENT);
    turn = constrain(turn, 0, MOTION_MAX_PERCENT);
    const bool changed = (drive != motionDrivePercent) || (turn != motionTurnPercent) ||
                         (angleLeft != motionAngleLeftDeg) || (angleRight != motionAngleRightDeg) ||
                         (avoidAssist != motionAvoidAssist);
    motionDrivePercent = drive;
    motionTurnPercent = turn;
    motionAngleLeftDeg = angleLeft;
    motionAngleRightDeg = angleRight;
    motionAvoidAssist = avoidAssist;

    if (changed) {
        saveMotionConfigToCache();
        Serial.printf("[MOTION] Config updated: drive=%d%% (%d PWM) turn=%d%% (%d PWM) angles L/R=%d/%d deg assist=%d ceiling=%d PWM\n",
                      motionDrivePercent, drivePwm(), motionTurnPercent, turnPwm(),
                      motionAngleLeftDeg, motionAngleRightDeg, (int)motionAvoidAssist, MOTION_HARD_MAX_PWM);
    }
    sendMotionStatus(changed ? "applied" : "unchanged");
}

void socketIOEvent(socketIOmessageType_t type, uint8_t *payload, size_t length) {
    switch (type) {
        case sIOtype_DISCONNECT:
            socketConnected = false;
#if STOP_ON_SOCKET_LOSS
            // A dropped link must never leave the rover driving. The intent is
            // abandoned here; only a fresh command can start it again.
            motionIntent = "STOP";
            stopBridgeNow();
            motionBlockedBy = "socket";
            Serial.println("[SAFETY] Socket lost - motors stopped.");
#endif
            Serial.printf("\n[DEBUG] [IOc] ❌ Socket.IO DISCONNECTED (WiFi=%s, freeHeap=%u)\n",
                          WiFi.status() == WL_CONNECTED ? "connected" : "disconnected",
                          ESP.getFreeHeap());
            break;

        case sIOtype_CONNECT:
            Serial.println("\n[DEBUG] [IOc] ✔️ Socket.IO CONNECTED successfully!");
            socketIO.send(sIOtype_CONNECT, "/");
            socketConnected = true;
            sendDeviceHello();
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
                    if (strcmp(action, "ota") == 0) {
                        // Firmware update pushed from the admin panel. The image
                        // must belong to THIS board family: a mismatch is
                        // reported back and never downloaded.
#if OTA_ENABLED
                        const char *otaTarget = data["target"] | "";
                        const char *otaRequested = data["version"] | "";
                        const char *otaPath = data["path"] | "";
                        const char *otaMd5 = data["md5"] | "";
                        const long otaSize = data["size"] | 0L;
                        Serial.printf("[OTA] Command for target=%s version=%s (this board: %s)\n",
                                      otaTarget, otaRequested, FW_TARGET);
                        if (strcmp(otaTarget, FW_TARGET) != 0) {
                            otaVersion = otaRequested;
                            sendOtaStatus("ignored", "target mismatch - this image is for another board");
                        } else if (strcmp(otaRequested, FW_VERSION) == 0) {
                            otaVersion = otaRequested;
                            sendOtaStatus("success", "already running this version");
                        } else {
                            applyOtaUpdate(String(otaRequested), String(otaPath), String(otaMd5), otaSize);
                        }
#else
                        Serial.println("[OTA] Ignored: OTA is disabled in this build");
#endif
                    } else if (strcmp(action, "field_map") == 0) {
                        // field_map is configuration the server only sends when
                        // this rover's cached revision is out of date - the map
                        // itself is kept on the SD card. Mission waypoints
                        // arrive separately as autonomous_mission.
                        handleFieldMap(data);
                    } else if (strcmp(action, "motion_config") == 0 || strcmp(action, "set_speed") == 0) {
                        // Speed limits from the web panel (clamped here, and by
                        // MOTION_HARD_MAX_PWM, no matter what arrives).
                        applyMotionConfig(data);
                    } else if (strcmp(action, "get_motion_status") == 0) {
                        sendMotionStatus("requested");
                    } else if (strcmp(action, "get_map_status") == 0) {
                        sendMapStatus("requested");
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
                        setMotionSource(SRC_MANUAL);
                        autonomousPaused = true;
                        controlMotors("STOP");
                        sendMissionUpdate("mission_progress", "paused", "Paused by operator");
                    } else if (strcmp(action, "start_patrol") == 0) {
                        setMotionSource(SRC_AUTO);
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
                            setMotionSource(SRC_MANUAL);
                            controlMotors("STOP");
                            cameraServo.write(CAMERA_CENTER_ANGLE);
                            delay(300);
                            captureAndUploadImage("manual", -1);
                        }
                    } else if (strcmp(action, "drive") == 0) {
                        autonomousPaused = true; // manual command always overrides autonomous drive
                        setMotionSource(SRC_MANUAL);
                        String direction = String((const char *)(data["direction"] | "stop"));
                        direction.toUpperCase();
                        controlMotors(direction);
                    } else if (strcmp(action, "stop") == 0 || strcmp(action, "return_to_base") == 0) {
                        setMotionSource(SRC_MANUAL);
                        autonomousPaused = true;
                        controlMotors("STOP");
                    } else {
                        String motorAction(action);
                        motorAction.toUpperCase();
                        setMotionSource(SRC_MANUAL);
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
    pinMode(ULTRASONIC_TRIG_PIN, OUTPUT);
    pinMode(RAIN_DIGITAL_PIN, INPUT); // GPIO35 is input-only; custom DO is active-high
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

    // microSD cache: the field map and the speed limits survive a reboot, so a
    // reconnect never has to download the map again.
    showBootStage("SD card...", 27);
    storageInit();
    loadCachedMapMeta();
    loadMotionConfigFromCache();
    if (oledOk) showBootStage(cachedMapRev.length() ? "Field map cached" : "No field map yet", 29);

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

    // Speed limits, obstacle envelope and the dead-man failsafe run on every
    // loop - never inside a delay(), so a STOP is honoured immediately.
    serviceMotors();
    updateSafetySensors();
    // Arc planning runs straight after the scan: it turns the fresh distances
    // into a steering decision before navigateMission() can ask for a straight
    // line into a plant (and before the next socket message needs the loop).
    serviceAvoidance();

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

// `quiet` keeps the 5 Hz safety scan from flooding the serial log; every
// reading used for telemetry still prints exactly as before.
long readUltrasonic(int echoPin, String sensorName, bool quiet, uint32_t timeoutUs) {
    digitalWrite(ULTRASONIC_TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(ULTRASONIC_TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(ULTRASONIC_TRIG_PIN, LOW);

    long duration = pulseIn(echoPin, HIGH, timeoutUs);
    long distance = duration * 0.034 / 2;
    // No echo inside the window means "nothing within range": report the
    // maximum, never a fake 0 cm that would look like a wall against the nose.
    if (distance <= 0) distance = ULTRASONIC_MAX_CM;

    if (!quiet) {
        Serial.printf("[DEBUG] [Ultrasonic-%s] Duration: %ld us, Distance: %ld cm\n",
                      sensorName.c_str(), duration, distance);
    }
    return distance;
}

void sendLocationData() {
    if (!socketConnected) return;
    const bool fix = gpsHasFix();
#if !GPS_FALLBACK_ENABLED
    if (!fix) return;   // no fix and the placeholder is switched off: stay silent
#endif
    DynamicJsonDocument doc(768);
    JsonArray event = doc.to<JsonArray>();
    event.add("message.upsert");
    JsonObject envelope = event.createNestedObject();
    envelope["Type"] = "location";
    JsonObject message = envelope.createNestedObject("Message");
    // fix:false = these are the placeholder coordinates from config.h, the GPS
    // module is not answering. The app tells the operator instead of guessing.
    message["latitude"] = reportLatitude();
    message["longitude"] = reportLongitude();
    message["altitude"] = fix && gps.altitude.isValid() ? gps.altitude.meters() : 0;
    message["satellites"] = fix && gps.satellites.isValid() ? gps.satellites.value() : 0;
    message["fix"] = fix;
    message["placeholder"] = !fix;
    message["deviceId"] = DEVICE_ID;
    String output;
    serializeJson(doc, output);
    socketIO.sendEVENT(output);
}

void sendSensorData() {
    float humidity = dht.readHumidity();
    float temperature = dht.readTemperature();
    
    // The rain module is powered from 3V3 and AO is treated as a 0..3.3V
    // signal. analogReadMilliVolts() avoids assuming a fixed ADC raw-count scale
    // across Wokwi and real ESP32 boards. This sensor's AO falls as the plate
    // gets wetter, so invert it to report wetness as 0..100 percent. DO is read
    // as a diagnostic comparator signal; the established rainDrop/isRaining
    // telemetry continues to use AO so the server/client contract is unchanged.
    const uint32_t rainMilliVolts = analogReadMilliVolts(RAIN_ANALOG_PIN);
    const float rainWetnessPct = constrain(
        100.0f - (100.0f * (float)rainMilliVolts / 3300.0f), 0.0f, 100.0f);
    const bool rainDigitalWet = digitalRead(RAIN_DIGITAL_PIN) == HIGH;

    const bool isRaining = rainWetnessPct >= RAIN_THRESHOLD_PERCENT;

    Serial.printf("[DEBUG] [DHT22] Temp: %.2f °C | Humidity: %.2f %%\n", temperature, humidity);
    Serial.printf("[DEBUG] [Rain] AO: %lu mV | Wetness: %.2f %% | DO: %s | raining(AO): %d\n",
                  (unsigned long)rainMilliVolts, rainWetnessPct,
                  rainDigitalWet ? "WET" : "DRY", (int)isRaining);

    // The arc is already scanned every SENSOR_SLOT_MS by updateSafetySensors():
    // re-triggering the sensors here would block the loop for up to 3 x the
    // pulse timeout and reset the echo lines in the middle of a safety scan.
    // Report what the safety layer really sees, and leave the decision to the
    // planner - it steers around a plant, it does not slam the brakes on.
    const long distForward = latestFrontCm;
    const long distLeft = latestLeftCm;
    const long distRight = latestRightCm;

    if (isnan(humidity) || isnan(temperature)) {
        Serial.println("[DEBUG] [SENSOR] ❌ Failed to read from DHT22 sensor!");
        return;
    }

    DynamicJsonDocument doc(1400);
    JsonArray event = doc.to<JsonArray>();
    event.add("message.upsert");

    JsonObject data = event.createNestedObject();
    data["Type"] = "sensors";

    JsonObject message = data.createNestedObject("Message");
    message["temperature"] = temperature;
    message["humidity"] = humidity;
    message["rainDrop"] = rainWetnessPct;
    message["isRaining"] = isRaining;
    message["distForward"] = distForward;
    message["distLeft"] = distLeft;
    message["distRight"] = distRight;
    // Motion / speed limits and the SD field-map cache state, so the dashboard
    // shows what the rover is really doing instead of guessing.
    message["driveSpeedPercent"] = motionDrivePercent;
    message["turnSpeedPercent"] = motionTurnPercent;
    message["drivePwm"] = drivePwm();
    message["turnPwm"] = turnPwm();
    message["appliedPwm"] = appliedPwm;
    message["motionIntent"] = motionIntent;
    message["motionSource"] = motionSource == SRC_AUTO ? "auto" : (motionSource == SRC_MANUAL ? "manual" : "none");
    message["blockedBy"] = motionBlockedBy;
    message["obstacleStopCm"] = OBSTACLE_STOP_CM;
    message["hardMaxPwm"] = MOTION_HARD_MAX_PWM;
    message["mapRev"] = cachedMapRev;
    message["mapBlocks"] = cachedMapBlocks;
    message["mapCached"] = cachedMapRev.length() > 0;
    message["sdOk"] = sdOk;
    // Front-arc geometry, the gaps the planner computed and the manoeuvre it is
    // running - this is what lets the dashboard say "steering around a plant on
    // the left" instead of guessing from a single distance.
    message["sensorAngleLeftDeg"] = motionAngleLeftDeg;
    message["sensorAngleRightDeg"] = motionAngleRightDeg;
    message["gapLeftCm"] = (int)(gapLeftCm + 0.5f);
    message["gapRightCm"] = (int)(gapRightCm + 0.5f);
    message["avoidState"] = avoidStateName();
    message["avoidDir"] = avoidDir;
    message["avoidAssist"] = motionAvoidAssist;
    message["roverHalfWidthCm"] = ROVER_HALF_WIDTH_CM;
    message["emergencyStopCm"] = OBSTACLE_EMERGENCY_CM;
    message["firmware"] = FW_VERSION;
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

void navigateMission() {
    if (!autonomousActive || autonomousPaused || currentWaypointIndex >= missionWaypointCount) return;
    setMotionSource(SRC_AUTO);
    // Never drive on the placeholder position: no measured fix, no motion.
    if (!gpsHasFix()) {
        controlMotors("STOP");
        return;
    }

    // Distances are refreshed by updateSafetySensors() every 200 ms (front) /
    // 600 ms (sides) - far faster than the old 500 ms read inside navigation.

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

    // An active arc detour (serviceAvoidance()) owns the wheels until it hands
    // them back to the mission, so navigateMission() must not fight it. The
    // envelope has already slowed the rover to crawl while it detours.
    if (avoidanceOwnsMotion) return;

    double desired = TinyGPSPlus::courseTo(currentLatitude, currentLongitude,
                                            target.latitude, target.longitude);
    double actual = gps.course.isValid() && gps.speed.kmph() > 0.3 ? gps.course.deg() : desired;
    float error = normalizeHeadingError((float)(desired - actual));
    if (error > 22) controlMotors("RIGHT");
    else if (error < -22) controlMotors("LEFT");
    else controlMotors("FORWARD");
}

// Public motion entry point. Every caller keeps working unchanged: it records
// the intent (with the source the caller set) and lets the non-blocking engine
// in serviceMotors() do the electrical work on the next loop tick.
void controlMotors(String action) {
    String requested = action;
    requested.trim();
    requested.toUpperCase();

    if (requested != "FORWARD" && requested != "BACKWARD" && requested != "LEFT" &&
        requested != "RIGHT" && requested != "STOP") {
        Serial.printf("[MOTOR] Unknown action command: %s (ignored)\n", action.c_str());
        return;
    }

    failsafeTripped = false;
    motionBlockedBy = "";
    motionCmdAt = millis();

    if (requested == "STOP") {
        // Stopping never waits for a ramp or for a direction change.
        motionIntent = "STOP";
        motionTargetPwm = 0;
        stopBridgeNow();
        Serial.println("[MOTOR] STOP - bridge released");
        return;
    }

    motionIntent = requested;
    serviceMotors();
    Serial.printf("[MOTOR] %s requested (source=%s, envelope=%d PWM, ceiling=%d PWM)\n",
                  requested.c_str(),
                  motionSource == SRC_AUTO ? "auto" : (motionSource == SRC_MANUAL ? "manual" : "none"),
                  motionTargetPwm, MOTION_HARD_MAX_PWM);
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
                    addField("latitude", String(reportLatitude(), 7));
                    addField("longitude", String(reportLongitude(), 7));
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