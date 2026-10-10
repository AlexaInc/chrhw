// ---------------------------------------------------------------------------
// GENERATED FILE - DO NOT EDIT THIS COPY.
//
// This is what the Arduino IDE compiles: the machine values of THIS board
// (config.machine.h, which you own and edit) followed by a copy of the shared
// configuration wokwi-esp32-project/include/config.h.
//
//   board values : config.machine.h          (edit this one)
//   shared config: wokwi-esp32-project/include/config.h   (edit that one)
//   then run     : bash scripts/sync-arduino-ide.sh
//
// Everything in the shared part is #ifndef-guarded, so the board values above
// always win. A config.local.h next to this file (optional, git-ignored) is
// read after the board values and before the shared defaults, so it beats both.
// ---------------------------------------------------------------------------
#pragma once

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
#define WIFI_SSID "vivo Y29"
#define WIFI_PASSWORD "00000000"

// This board talks to the LAN gateway (http://<gateway-ip>:8000 / ws://...) 
// instead of the hosted server. To use the hosted server instead, delete the
// next line and uncomment the two below it.
// #define CHRH_FORCE_GATEWAY_MODE 1
 #define CUSTOM_SERVER_HOST "crophealth.dpdns.org"
 #define CUSTOM_SERVER_URL  "https://crophealth.dpdns.org"

#ifdef __has_include
#if __has_include("config.local.h")
#include "config.local.h"
#endif
#endif

// ===========================================================================
// --- copy of wokwi-esp32-project/include/config.h (generated, do not edit) -------------------------
// ===========================================================================
// ===========================================================================
// CHR-01 rover firmware — THE configuration file.
//
// Every other config.h in this repository (root copy, Arduino IDE sketch
// folder) is now an include-shim that points back here, so pins, Wi-Fi and
// limits are edited in ONE place and every build target follows.
//     PlatformIO : include/config.h            (this file)
//     Root copy  : config.h                    -> #include "include/config.h"
//     Arduino IDE: arduino-ide/esp32-dev-controller/config.h
//                                              -> #include "../../../include/config.h"
//
// Machine-specific overrides (Wi-Fi password, server URL, a different board)
// do NOT belong in this file. Create `config.local.h` next to the sketch /
// in include/ and define ONLY what differs — it is loaded first, and every
// default below is `#ifndef`-guarded so the local value wins. See
// include/config.local.h.example (config.local.h is git-ignored, so no
// credentials are ever committed again).
// ===========================================================================
// (config.local.h is already included at the top of this generated file)

// --- Identity -------------------------------------------------------------
#ifndef DEVICE_ROLE
#define DEVICE_ROLE "esp_32"
#endif
// Reported to the server in the boot handshake (device_hello).
#ifndef FW_VERSION
#define FW_VERSION "2026-10-07-rain-do"
#endif

// --- Over-the-air (OTA) firmware updates ----------------------------------
// The admin panel pushes updates: the board downloads the image from the same
// server it already talks to, flashes itself and reboots. The server decides
// WHICH image; these flags only say what this build accepts.
//
// FW_TARGET is the identity of this board family. An image uploaded under
// another target is refused here (and never even handed over by the server), so
// a pump image can never end up on the rover or the other way round. It is also
// what the panel shows next to each board.
#ifndef FW_TARGET
#define FW_TARGET "rover"
#endif
#ifndef OTA_ENABLED
#define OTA_ENABLED 1
#endif
// Where OTA is decided about: the admin panel's "Update boards" trigger, plus
// the same request being kept queued on the server for a board that is off.
// This flag only exists so a build can be compiled without any OTA code (0).
#if OTA_ENABLED && !defined(ENABLE_OTA)
#define ENABLE_OTA 1
#endif
#ifndef DEVICE_ID
#define DEVICE_ID "robot-01"
#endif
// Must match ROBOT_TOKEN in the server's .env.
#ifndef ROBOT_TOKEN
#define ROBOT_TOKEN "4408dc8d907853dcb3c3cd3e9714e78f8a92a5509755d25cd6e561dd2c78d9d6"
#endif

// --- GPS placeholder position ----------------------------------------------
// The GPS module can be missing, unpowered or without a sky view, and the
// operator still needs a position on the map. While there is no valid fix the
// rover reports the placeholder position below and marks the message
// `fix:false`, so the app shows "NO GPS FIX" instead of an empty map.
// Change these two numbers to your own field centre, or set
// GPS_FALLBACK_ENABLED to 0 to send nothing at all while there is no fix.
#ifndef GPS_FALLBACK_ENABLED
#define GPS_FALLBACK_ENABLED 1
#endif
#ifndef GPS_FALLBACK_LATITUDE
#define GPS_FALLBACK_LATITUDE 7.489087449264883
#endif
#ifndef GPS_FALLBACK_LONGITUDE
#define GPS_FALLBACK_LONGITUDE 80.36537714662697
#endif

// --- Wi-Fi & server -------------------------------------------------------
// Defaults target the Wokwi simulator (Wokwi-GUEST). For real hardware put
// your SSID/password and the server URL in config.local.h.
#ifndef WIFI_SSID
#define WIFI_SSID "Wokwi-GUEST"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif
// Define BOTH to talk to the production server; leave both undefined to fall
// back to http://<gateway-ip>:8000.
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


// --- Pin map --------------------------------------------------------------
#ifndef DHTPIN
#define DHTPIN 19
#endif
#ifndef DHTTYPE
#define DHTTYPE DHT22
#endif

#ifndef ULTRASONIC_TRIG_PIN
#define ULTRASONIC_TRIG_PIN 15
#endif
#ifndef ECHO_FORWARD_PIN
#define ECHO_FORWARD_PIN 32
#endif
#ifndef ECHO_LEFT_PIN
#define ECHO_LEFT_PIN 23
#endif
#ifndef ECHO_RIGHT_PIN
#define ECHO_RIGHT_PIN 39
#endif
#ifndef CAMERA_SERVO
#define CAMERA_SERVO 4
#endif

// AO provides continuous wetness; DO is a diagnostic threshold input. GPIO35
// is input-only and was freed when rover soil sensing was removed. GPIO22 stays
// assigned to L298N IN4. Keep both real-module outputs within 3.3V-safe limits.
#ifndef RAIN_ANALOG_PIN
#define RAIN_ANALOG_PIN 34
#endif
#ifndef RAIN_DIGITAL_PIN
#define RAIN_DIGITAL_PIN 35
#endif
#ifndef RAIN_THRESHOLD_PERCENT
#define RAIN_THRESHOLD_PERCENT 50
#endif
#ifndef ENA_PIN
#define ENA_PIN 25
#endif
#ifndef IN1_PIN
#define IN1_PIN 26
#endif
#ifndef IN2_PIN
#define IN2_PIN 27
#endif
#ifndef IN3_PIN
#define IN3_PIN 14
#endif
#ifndef IN4_PIN
#define IN4_PIN 22
#endif
#ifndef ENB_PIN
#define ENB_PIN 13
#endif

// --- microSD card (field-map cache) --------------------------------------
// Wiring taken from the Wokwi diagram (wokwi-microsd-card part "sd1"):
//   CS -> GPIO5, MOSI/DI -> GPIO2, SCK -> GPIO33, MISO/DO -> GPIO36(VP)
// GPIO36 is input-only, which is exactly what MISO needs.
#ifndef SD_ENABLED
#define SD_ENABLED 1
#endif
#ifndef SD_CS_PIN
#define SD_CS_PIN 5
#endif
#ifndef SD_MOSI_PIN
#define SD_MOSI_PIN 2
#endif
#ifndef SD_MISO_PIN
#define SD_MISO_PIN 36
#endif
#ifndef SD_SCK_PIN
#define SD_SCK_PIN 33
#endif
#ifndef SD_SPI_HZ
#define SD_SPI_HZ 10000000
#endif
// Everything the firmware caches lives in this folder on the card.
#ifndef CACHE_DIR
#define CACHE_DIR "/chrhw"
#endif
#ifndef MAP_FILE
#define MAP_FILE "/chrhw/fieldmap.json"
#endif
#ifndef MAP_META_FILE
#define MAP_META_FILE "/chrhw/fieldmap.meta"
#endif
#ifndef MAP_TMP_FILE
#define MAP_TMP_FILE "/chrhw/fieldmap.tmp"
#endif
#ifndef MOTION_CFG_FILE
#define MOTION_CFG_FILE "/chrhw/motion.cfg"
#endif
// Abort a field-map download that stalls for this long.
#ifndef MAP_RX_TIMEOUT_MS
#define MAP_RX_TIMEOUT_MS 20000
#endif

// --- Motion limits (SAFETY) ----------------------------------------------
// The rover is driven around crops, not on a road. These are PWM duty values
// (0-255) for the L298N enables; the old firmware used a hardcoded 200/180,
// which is ~78 % duty — far too fast to stop next to a plant.
//
// MOTION_HARD_MAX_PWM is a compile-time ceiling: NOTHING (not even a server
// config, the app or a mission) can drive above it. The web panel's
// "Drive speed & limits" settings can only move inside this ceiling.
#ifndef MOTION_HARD_MAX_PWM
#define MOTION_HARD_MAX_PWM 150          // ~59 % duty — absolute ceiling
#endif
#ifndef MOTION_CRUISE_PWM
#define MOTION_CRUISE_PWM 110            // straight-line patrol speed
#endif
#ifndef MOTION_TURN_PWM
#define MOTION_TURN_PWM 95               // in-place turns
#endif
#ifndef MOTION_CRAWL_PWM
#define MOTION_CRAWL_PWM 70              // near crops / obstacles
#endif
#ifndef MOTION_REVERSE_PWM
#define MOTION_REVERSE_PWM 75            // backing out of a row
#endif
#ifndef MOTION_MIN_PWM
#define MOTION_MIN_PWM 55                // below this the gearbox just buzzes
#endif

// Acceleration limiting: PWM is stepped, never jumped. Prevents the wheels
// from lurching (and the current spike that used to brown out the HC-SR04s).
#ifndef MOTION_RAMP_UP_STEP
#define MOTION_RAMP_UP_STEP 10
#endif
#ifndef MOTION_RAMP_UP_MS
#define MOTION_RAMP_UP_MS 20
#endif
#ifndef MOTION_RAMP_DOWN_STEP
#define MOTION_RAMP_DOWN_STEP 25
#endif
#ifndef MOTION_RAMP_DOWN_MS
#define MOTION_RAMP_DOWN_MS 12
#endif

// --- Web-panel speed scale ------------------------------------------------
// The panel slider is a percentage of the values above: 100 % == cruise 110 /
// turn 95, and 0 % means "do not drive at all". Anything above 100 % is
// rejected, so the panel can only ever make the rover SLOWER than the safe
// defaults — never faster.
#ifndef MOTION_DEFAULT_DRIVE_PERCENT
#define MOTION_DEFAULT_DRIVE_PERCENT 100
#endif
#ifndef MOTION_DEFAULT_TURN_PERCENT
#define MOTION_DEFAULT_TURN_PERCENT 100
#endif
#ifndef MOTION_DEFAULT_CRAWL_PERCENT
#define MOTION_DEFAULT_CRAWL_PERCENT 100
#endif
#ifndef MOTION_MAX_PERCENT
#define MOTION_MAX_PERCENT 100
#endif

// --- Motion engine timing (all non-blocking) ------------------------------
// How often the motor state machine is serviced from loop().
#ifndef MOTION_SERVICE_INTERVAL_MS
#define MOTION_SERVICE_INTERVAL_MS 5
#endif
// H-bridge dead time before a direction change is energised again.
#ifndef MOTION_DEAD_TIME_MS
#define MOTION_DEAD_TIME_MS 40
#endif
// Settling time after IN1..IN4 change, before PWM is applied.
#ifndef MOTION_SETTLE_MS
#define MOTION_SETTLE_MS 10
#endif

// --- Obstacle envelope ----------------------------------------------------
// front cm > SLOW  -> full cruise speed
// CRAWL..SLOW      -> crawl speed
// STOP..CRAWL      -> the front-arc planner steers around it (turns out,
//                     creeps past, turns back). The rover only stops when no
//                     side gap is wide enough to pass - see FRONT ARC below.
#ifndef OBSTACLE_SLOW_CM
#define OBSTACLE_SLOW_CM 70
#endif
#ifndef OBSTACLE_CRAWL_CM
#define OBSTACLE_CRAWL_CM 45
#endif
#ifndef OBSTACLE_STOP_CM
#define OBSTACLE_STOP_CM 30
#endif
// A turn on the spot is blocked when that side is this close to a plant.
#ifndef OBSTACLE_SIDE_STOP_CM
#define OBSTACLE_SIDE_STOP_CM 15
#endif
// Corridor test used by the autonomous bypass planner.
#ifndef OBSTACLE_CORRIDOR_CM
#define OBSTACLE_CORRIDOR_CM 20
#endif
// The bypass leg is abandoned (and re-planned) below this clearance.
#ifndef OBSTACLE_BYPASS_REPLAN_CM
#define OBSTACLE_BYPASS_REPLAN_CM 18
#endif
// Absolute emergency line: nothing may be commanded forward below this.
#ifndef OBSTACLE_EMERGENCY_CM
#define OBSTACLE_EMERGENCY_CM 12
#endif
// Bypass timings, expressed for the default turn PWM above. They are scaled
// automatically when the panel lowers the turn speed, so the rover still turns
// the same physical angle.
#ifndef AVOID_TURN_OUT_MS
#define AVOID_TURN_OUT_MS 1200
#endif
#ifndef AVOID_PASS_MS
#define AVOID_PASS_MS 1500
#endif
#ifndef AVOID_CORRIDOR_TURN_MS
#define AVOID_CORRIDOR_TURN_MS 1500
#endif
#ifndef AVOID_TURN_BACK_MS
#define AVOID_TURN_BACK_MS 1200
#endif
// --- FRONT SENSOR ARC (three HC-SR04 on the printed brackets) -------------
// The rover carries one centre sensor and the two side sensors whose brackets
// splay them outwards. Every reading is along that sensor's own axis, so the
// planner (serviceAvoidance() in src/main.cpp) converts it with the mounting
// angles below before it decides anything:
//     lateral room  = reading * sin(angle + half cone)
//     forward reach = reading * cos(angle - half cone)
// KEEP THESE NUMBERS IN STEP WITH THE BRACKETS THAT ARE ACTUALLY BOLTED ON -
// a wrong angle steers the rover wrongly. The web panel can change both angles
// at runtime (SENSOR_ANGLE_MIN_DEG .. SENSOR_ANGLE_MAX_DEG) and the rover saves
// them on the SD card.
#ifndef SENSOR_ANGLE_LEFT_DEG
#define SENSOR_ANGLE_LEFT_DEG 45
#endif
#ifndef SENSOR_ANGLE_RIGHT_DEG
#define SENSOR_ANGLE_RIGHT_DEG 45
#endif
// Half of an HC-SR04 cone (~15 deg total). The cone edge is what covers the
// front corners, and it is why an angled sensor leaves no blind spot between
// the centre beam and the side beam.
#ifndef SENSOR_BEAM_HALF_DEG
#define SENSOR_BEAM_HALF_DEG 8
#endif
#ifndef SENSOR_ANGLE_MIN_DEG
#define SENSOR_ANGLE_MIN_DEG 0
#endif
#ifndef SENSOR_ANGLE_MAX_DEG
#define SENSOR_ANGLE_MAX_DEG 80
#endif
// Chassis width. A gap narrower than half the rover plus this margin is never
// treated as "passable" - the rover stops and reports "no-path" instead of
// grinding through the crop row.
#ifndef ROVER_HALF_WIDTH_CM
#define ROVER_HALF_WIDTH_CM 13
#endif
#ifndef OBSTACLE_SIDE_MARGIN_CM
#define OBSTACLE_SIDE_MARGIN_CM 4
#endif
// The arc is scanned ONE sensor per slot: three HC-SR04 on a shared trigger
// would hear each other's bursts. Order is centre, left, centre, right, so the
// centre beam (the one that decides a stop) is refreshed twice as often.
#ifndef SENSOR_SLOT_MS
#define SENSOR_SLOT_MS 60
#endif
// Slower cadence while the rover is standing still (telemetry/panel only).
#ifndef SENSOR_IDLE_SLOT_MS
#define SENSOR_IDLE_SLOT_MS 250
#endif
// Safety reads give up early: anything further away cannot become a problem
// before the next scan, and a short timeout keeps loop() responsive.
#ifndef ULTRASONIC_SAFETY_TIMEOUT_US
#define ULTRASONIC_SAFETY_TIMEOUT_US 12000
#endif
// "Nothing in range" is reported as this, never as a fake 0 cm (a wall).
#ifndef ULTRASONIC_MAX_CM
#define ULTRASONIC_MAX_CM 400
#endif
// --- Front-arc driving policy --------------------------------------------
// Manual assist: steers a held FORWARD command around a plant instead of just
// stopping. The panel can switch it off (then manual mode keeps the envelope).
#ifndef AVOID_ASSIST_MANUAL
#define AVOID_ASSIST_MANUAL 1
#endif
// Consecutive arc scans needed before a manoeuvre starts / before it is
// declared finished - this is the hysteresis that stops the rover from
// twitching between "drive" and "steer" every 60 ms.
#ifndef AVOID_CONFIRM_SCANS
#define AVOID_CONFIRM_SCANS 2
#endif
#ifndef AVOID_CLEAR_SCANS
#define AVOID_CLEAR_SCANS 3
#endif
// Longest a single steer-out / turn-back leg may last. If turning out does not
// open the path in this time the rover creeps forward and looks again, so it
// never spins on the spot forever.
#ifndef AVOID_MAX_TURN_MS
#define AVOID_MAX_TURN_MS 2600
#endif
// Minimum gap between two re-plans of a running detour.
#ifndef AVOID_REPLAN_COOLDOWN_MS
#define AVOID_REPLAN_COOLDOWN_MS 800
#endif

// --- Dead-man failsafes ---------------------------------------------------
// If a fresh command does not arrive within this window the motors are cut.
// The app re-sends a held direction every 250 ms and a stop on release, so a
// dropped Wi-Fi link / closed browser tab can never leave the rover running.
#ifndef DRIVE_FAILSAFE_MS
#define DRIVE_FAILSAFE_MS 1500
#endif
// Autonomous navigation must re-issue its motion intent this often.
#ifndef AUTO_FAILSAFE_MS
#define AUTO_FAILSAFE_MS 3000
#endif
// Stop immediately when the Socket.IO link drops.
#ifndef STOP_ON_SOCKET_LOSS
#define STOP_ON_SOCKET_LOSS 1
#endif
