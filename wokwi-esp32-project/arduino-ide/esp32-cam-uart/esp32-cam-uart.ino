#include <Arduino.h>
#include "esp_camera.h"
#include "img_converters.h"

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
static const uint8_t SOFTWARE_JPEG_QUALITY = 80; // frame2jpg(): 0..100
static uint8_t previousByte = '\n';
static uint32_t lastReceiveMs = 0;
static bool cameraReady = false;
static esp_err_t lastCameraError = ESP_FAIL;

static void setCameraPower(bool on) {
  // AI Thinker PWDN is active HIGH. Keep it LOW during initialization and use
  // a power cycle only for a retry, matching the proven CameraWebServer setup.
  pinMode(PWDN_GPIO_NUM, OUTPUT);
  digitalWrite(PWDN_GPIO_NUM, on ? LOW : HIGH);
  delay(on ? 100 : 40);
}

static esp_err_t beginCamera(uint32_t xclkHz, framesize_t frameSize, bool usePsram) {
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
  config.xclk_freq_hz = xclkHz;

  // This is intentionally the same working raw-camera configuration as the
  // Espressif CameraWebServer test supplied by the user. This module captures
  // RGB565 correctly but does not provide a usable hardware-JPEG stream.
  config.pixel_format = PIXFORMAT_RGB565;
  config.frame_size = frameSize;
  config.jpeg_quality = 12; // unused in RGB565 mode
  config.fb_count = 1;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = usePsram ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  return esp_camera_init(&config);
}

static bool initCamera() {
  const bool usePsram = psramFound();
  setCameraPower(true);

  // FRAMESIZE_240X240 is important: this exact mode works in CameraWebServer
  // and uses 25% less raw memory than QVGA (115200 versus 153600 bytes).
  lastCameraError = beginCamera(20000000, FRAMESIZE_240X240, usePsram);
  if (lastCameraError != ESP_OK) {
    esp_camera_deinit();
    setCameraPower(false);
    setCameraPower(true);
    // Last-resort low-memory/low-clock mode.
    lastCameraError = beginCamera(10000000, FRAMESIZE_QQVGA, usePsram);
  }
  if (lastCameraError != ESP_OK) return false;

  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor == nullptr) {
    lastCameraError = ESP_ERR_NOT_FOUND;
    esp_camera_deinit();
    return false;
  }

  sensor->set_whitebal(sensor, 1);
  sensor->set_exposure_ctrl(sensor, 1);
  sensor->set_gain_ctrl(sensor, 1);
  lastCameraError = ESP_OK;
  return true;
}

static void sendNoFrame(const char *stage, esp_err_t errorCode) {
  // Keep <IMG:0> backward-compatible, then append a machine-readable reason.
  // The controller prints this reason so initialization, frame capture and
  // software JPEG failures are no longer reported as the same vague fault.
  Serial.print("<IMG:0><CAMERR:");
  Serial.print(stage);
  Serial.print(":0x");
  Serial.print((uint32_t)errorCode, HEX);
  Serial.print('>');
  Serial.flush();
}

struct JpegStreamContext {
  bool transmit;
  size_t length;
};

static size_t jpegStreamCallback(void *arg, size_t index, const void *data, size_t len) {
  JpegStreamContext *context = static_cast<JpegStreamContext *>(arg);
  if (index == 0) context->length = 0;
  if (context->transmit) {
    const size_t written = Serial.write(static_cast<const uint8_t *>(data), len);
    if (written != len) return 0;
  }
  context->length += len;
  return len;
}

static void sendPhoto() {
  if (!cameraReady) cameraReady = initCamera();
  if (!cameraReady) {
    sendNoFrame("INIT", lastCameraError);
    return;
  }

  // With one framebuffer + CAMERA_GRAB_WHEN_EMPTY, the buffer can contain the
  // frame captured immediately after the previous request (or boot probe). If
  // it sat there until the next command, returning it makes every photo appear
  // exactly one capture behind. Drain that queued frame first, return it to the
  // driver, then wait for and use the newly captured frame.
  camera_fb_t *staleFrame = esp_camera_fb_get();
  if (!staleFrame) {
    cameraReady = false;
    sendNoFrame("CAPTURE_FLUSH", ESP_FAIL);
    return;
  }
  esp_camera_fb_return(staleFrame);
  delay(10); // yield while the sensor begins filling the released framebuffer

  camera_fb_t *frame = esp_camera_fb_get();
  if (!frame || !frame->buf || frame->len == 0) {
    if (frame) esp_camera_fb_return(frame);
    cameraReady = false; // force a full re-initialization on the next request
    sendNoFrame("CAPTURE", ESP_FAIL);
    return;
  }

  // frame2jpg() allocates one contiguous output buffer and fails on this board
  // even though CameraWebServer's callback encoder works. Use the same callback
  // path here: pass 1 counts bytes, pass 2 writes chunks directly to UART.
  // This removes the large temporary JPEG allocation that caused ENCODE:0x101.
  JpegStreamContext counter = {false, 0};
  const bool counted = frame2jpg_cb(frame, SOFTWARE_JPEG_QUALITY,
                                    jpegStreamCallback, &counter);
  if (!counted || counter.length == 0) {
    esp_camera_fb_return(frame);
    sendNoFrame("ENCODE_COUNT", ESP_FAIL);
    return;
  }

  Serial.print("<IMG:");
  Serial.print(counter.length);
  Serial.print('>');

  JpegStreamContext sender = {true, 0};
  const bool sent = frame2jpg_cb(frame, SOFTWARE_JPEG_QUALITY,
                                 jpegStreamCallback, &sender);
  esp_camera_fb_return(frame);
  Serial.flush();

  // A failure after the header becomes a partial frame. The controller already
  // detects that condition and retries/reports the UART fault.
  if (!sent || sender.length != counter.length) cameraReady = false;
}

void setup() {
  // UART0: ESP32-CAM U0R/GPIO3 receives from DevKit TX0,
  //        ESP32-CAM U0T/GPIO1 sends to DevKit RX0.
  // No debug messages are printed: this UART carries binary JPEG data.
  Serial.begin(CAMERA_UART_BAUD);
  Serial.setTimeout(1000);
  delay(300);
  cameraReady = initCamera();
  previousByte = '\n';
  lastReceiveMs = millis();
}

void loop() {
  while (Serial.available() > 0) {
    const uint8_t currentByte = (uint8_t)Serial.read();
    const uint32_t now = millis();

    // Ignore controller debug text. Accept only a standalone C: beginning of a
    // line or after at least 20 ms of UART silence.
    const bool standaloneCapture =
      currentByte == 'C' &&
      (previousByte == '\n' || previousByte == '\r' || (now - lastReceiveMs) >= 20);

    previousByte = currentByte;
    lastReceiveMs = now;
    if (standaloneCapture) sendPhoto();
  }
  delay(1);
}
