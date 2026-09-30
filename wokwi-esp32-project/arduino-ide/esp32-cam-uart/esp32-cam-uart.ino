#include <Arduino.h>
#include "esp_camera.h"

// AI Thinker ESP32-CAM pin map
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

static const uint32_t CAMERA_UART_BAUD = 921600;
static uint8_t previousByte = '\n';
static uint32_t lastReceiveMs = 0;
static bool cameraReady = false;

static bool initCamera() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_QVGA; // 320x240
    config.jpeg_quality = 12;           // Lower number = higher quality
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    config.frame_size = FRAMESIZE_QQVGA; // 160x120 without PSRAM
    config.jpeg_quality = 16;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  if (esp_camera_init(&config) != ESP_OK) return false;

  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor) {
    sensor->set_framesize(sensor, psramFound() ? FRAMESIZE_QVGA : FRAMESIZE_QQVGA);
  }
  return true;
}

static void sendPhoto() {
  if (!cameraReady) cameraReady = initCamera(); // retry: boot brownouts often clear
  if (!cameraReady) {
    // Answer instead of staying silent, so the controller reports a precise
    // "camera init failed" fault instead of a blind UART timeout.
    Serial.print("<IMG:0>");
    Serial.flush();
    return;
  }

  camera_fb_t *frame = esp_camera_fb_get();
  if (!frame || !frame->buf || frame->len == 0) {
    if (frame) esp_camera_fb_return(frame);
    Serial.print("<IMG:0>"); // no frame available — report, don't go silent
    Serial.flush();
    return;
  }

  // Protocol expected by the ESP32 DevKit controller:
  // <IMG:12345><12345 raw JPEG bytes>
  Serial.print("<IMG:");
  Serial.print(frame->len);
  Serial.print('>');
  Serial.write(frame->buf, frame->len);
  Serial.flush();

  esp_camera_fb_return(frame);
}

void setup() {
  // UART0: ESP32-CAM U0R/GPIO3 receives from DevKit TX0,
  //        ESP32-CAM U0T/GPIO1 sends to DevKit RX0.
  // No debug messages are printed on this UART.
  Serial.begin(CAMERA_UART_BAUD);
  Serial.setTimeout(1000);
  delay(300);
  cameraReady = initCamera();
  previousByte = '\n';
  lastReceiveMs = millis();
}

void loop() {
  while (Serial.available() > 0) {
    uint8_t currentByte = (uint8_t)Serial.read();
    uint32_t now = millis();

    // Ignore the controller's normal debug text. A capture is accepted only
    // for a standalone C: beginning of a line or after at least 20ms silence.
    bool standaloneCapture =
      currentByte == 'C' &&
      (previousByte == '\n' || previousByte == '\r' || (now - lastReceiveMs) >= 20);

    previousByte = currentByte;
    lastReceiveMs = now;

    if (standaloneCapture) sendPhoto();
  }
  delay(1);
}
