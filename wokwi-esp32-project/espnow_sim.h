// ============================================================================
//  espnow_sim.h  -  header-only ESP-NOW stand-in for Wokwi (use with chip.c)
//
//  Wokwi cannot simulate the ESP-NOW radio, so packets arrive from the custom
//  chip over a UART. Your sketch changes in ONE place:
//
//      //#include <esp_now.h>
//      #include "espnow_sim.h"
//
//  Everything else (esp_now_init, esp_now_register_recv_cb, OnDataRecv,
//  esp_now_send, esp_now_add_peer) keeps working unchanged.
//
//  Default wiring - ESP32 GPIO16 <- chip TX, ESP32 GPIO17 -> chip RX.
//  Override before the include if you need other pins:
//      #define ESPNOW_SIM_RX_PIN 4
//      #define ESPNOW_SIM_TX_PIN 5
//      #define ESPNOW_SIM_SERIAL Serial1
//      #include "espnow_sim.h"
// ============================================================================
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_err.h>

#ifndef ESPNOW_SIM_SERIAL
#define ESPNOW_SIM_SERIAL Serial1
#endif
#ifndef ESPNOW_SIM_RX_PIN
#define ESPNOW_SIM_RX_PIN 16      // ESP32 receives here (wire to chip TX)
#endif
#ifndef ESPNOW_SIM_TX_PIN
#define ESPNOW_SIM_TX_PIN 17      // ESP32 transmits here (wire to chip RX)
#endif
#ifndef ESPNOW_SIM_BAUD
#define ESPNOW_SIM_BAUD 500000    // must match UART_BAUD in chip.c
#endif

#define ESP_NOW_ETH_ALEN     6
#define ESP_NOW_MAX_DATA_LEN 250
#define ESP_NOW_KEY_LEN      16

typedef struct esp_now_recv_info {
  uint8_t *src_addr;
  uint8_t *des_addr;
  void    *rx_ctrl;
} esp_now_recv_info_t;

typedef enum {
  ESP_NOW_SEND_SUCCESS = 0,
  ESP_NOW_SEND_FAIL,
} esp_now_send_status_t;

typedef struct esp_now_peer_info {
  uint8_t peer_addr[ESP_NOW_ETH_ALEN];
  uint8_t lmk[ESP_NOW_KEY_LEN];
  uint8_t channel;
  int     ifidx;
  bool    encrypt;
  void   *priv;
} esp_now_peer_info_t;

typedef void (*esp_now_recv_cb_t)(const esp_now_recv_info_t *info,
                                  const uint8_t *data, int data_len);
typedef void (*esp_now_send_cb_t)(const uint8_t *mac_addr,
                                  esp_now_send_status_t status);

// ---------------------------------------------------------------- internals

static esp_now_recv_cb_t espnow_sim_recv_cb = nullptr;
static esp_now_send_cb_t espnow_sim_send_cb = nullptr;

static void espnow_sim_task(void *arg) {
  (void)arg;
  static const char magic[4] = { 'W', 'N', 'O', 'W' };
  uint8_t matched = 0;

  for (;;) {
    if (!ESPNOW_SIM_SERIAL.available()) {
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }

    uint8_t b = ESPNOW_SIM_SERIAL.read();
    matched = (b == (uint8_t)magic[matched]) ? matched + 1
                                             : (b == (uint8_t)magic[0] ? 1 : 0);
    if (matched < 4) continue;
    matched = 0;

    while (!ESPNOW_SIM_SERIAL.available()) vTaskDelay(pdMS_TO_TICKS(1));
    if (ESPNOW_SIM_SERIAL.read() != 'D') continue;          // packet type

    uint8_t src[ESP_NOW_ETH_ALEN];
    for (int i = 0; i < ESP_NOW_ETH_ALEN; i++) {
      while (!ESPNOW_SIM_SERIAL.available()) vTaskDelay(pdMS_TO_TICKS(1));
      src[i] = ESPNOW_SIM_SERIAL.read();
    }

    while (!ESPNOW_SIM_SERIAL.available()) vTaskDelay(pdMS_TO_TICKS(1));
    uint8_t len = ESPNOW_SIM_SERIAL.read();

    static uint8_t payload[ESP_NOW_MAX_DATA_LEN];
    memset(payload, 0, sizeof(payload));
    for (uint8_t i = 0; i < len; i++) {
      while (!ESPNOW_SIM_SERIAL.available()) vTaskDelay(pdMS_TO_TICKS(1));
      payload[i] = ESPNOW_SIM_SERIAL.read();
    }

    if (espnow_sim_recv_cb) {
      uint8_t dst[ESP_NOW_ETH_ALEN] = { 0 };
      WiFi.macAddress(dst);
      esp_now_recv_info_t info;
      info.src_addr = src;
      info.des_addr = dst;
      info.rx_ctrl  = nullptr;
      espnow_sim_recv_cb(&info, payload, len);
    }
  }
}

// ---------------------------------------------------------------- ESP-NOW API

static inline esp_err_t esp_now_init(void) {
  static bool started = false;
  if (started) return ESP_OK;

  ESPNOW_SIM_SERIAL.begin(ESPNOW_SIM_BAUD, SERIAL_8N1,
                          ESPNOW_SIM_RX_PIN, ESPNOW_SIM_TX_PIN);
  ESPNOW_SIM_SERIAL.setRxBufferSize(1024);
  delay(20);
  while (ESPNOW_SIM_SERIAL.available()) ESPNOW_SIM_SERIAL.read();

  // Big stack: the receive callback usually does real work (camera, HTTP...).
  if (xTaskCreatePinnedToCore(espnow_sim_task, "espnow_sim", 8192,
                              nullptr, 2, nullptr, 1) != pdPASS) {
    return ESP_FAIL;
  }
  started = true;
  return ESP_OK;
}

static inline esp_err_t esp_now_deinit(void) { return ESP_OK; }

static inline esp_err_t esp_now_register_recv_cb(esp_now_recv_cb_t cb) {
  espnow_sim_recv_cb = cb;
  return ESP_OK;
}

static inline esp_err_t esp_now_unregister_recv_cb(void) {
  espnow_sim_recv_cb = nullptr;
  return ESP_OK;
}

static inline esp_err_t esp_now_register_send_cb(esp_now_send_cb_t cb) {
  espnow_sim_send_cb = cb;
  return ESP_OK;
}

static inline esp_err_t esp_now_unregister_send_cb(void) {
  espnow_sim_send_cb = nullptr;
  return ESP_OK;
}

static inline esp_err_t esp_now_add_peer(const esp_now_peer_info_t *peer) {
  return peer ? ESP_OK : ESP_FAIL;
}

static inline esp_err_t esp_now_del_peer(const uint8_t *) { return ESP_OK; }

static inline esp_err_t esp_now_send(const uint8_t *peer_addr,
                                     const uint8_t *data, size_t len) {
  if (len > ESP_NOW_MAX_DATA_LEN) return ESP_FAIL;
  static const uint8_t broadcast[ESP_NOW_ETH_ALEN] =
      { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
  const uint8_t *mac = peer_addr ? peer_addr : broadcast;

  uint8_t header[12] = { 'W', 'N', 'O', 'W', 'S' };
  memcpy(header + 5, mac, ESP_NOW_ETH_ALEN);
  header[11] = (uint8_t)len;
  ESPNOW_SIM_SERIAL.write(header, sizeof(header));
  ESPNOW_SIM_SERIAL.write(data, len);

  if (espnow_sim_send_cb) espnow_sim_send_cb(mac, ESP_NOW_SEND_SUCCESS);
  return ESP_OK;
}
