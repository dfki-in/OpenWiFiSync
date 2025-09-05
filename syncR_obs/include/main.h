#pragma once

#include <float.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"  // IWYU pragma: keep
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gptimer.h"  // IWYU pragma: keep
#include "esp_wifi.h"  // IWYU pragma: keep

#define ENCRYPTION  0  // when set to 1, basic encryption scheme is used

#define MAXIMUM_MESSAGE_SIZE 1468
#define MAXIMUM_PURE_DATA 1426
#define GENERAL_HEADER_LEN 4
#define XXTEA_KEY_LEN 4

// * for Encryption
#define DELTA 0x9e3779b9
#define MX (((z >> 5) ^ (y << 2)) + ((y >> 3) ^ (z << 4))) ^ ((sum ^ y) + (k[(p&3) ^ e] ^ z))

// * Wi-Fi specific defines
#define WIFI_SSID "linksys"
#define WIFI_CHANNEL 11
#define VENDOR_SPECIFIC_TAG_NUMBER 221
#define WIFI_PACKET_FIXED_SIZE 32
#define WIFI_FCS_LEN 4
#define OUI_LEN 3
#define MAC_LEN 6

typedef enum {
    FOLLOW_UP = 0,
    SYNC_VAL_OBS  = 1,
} msg_type_t;

extern gptimer_handle_t gptimer;

extern uint8_t s_broadcast_mac[MAC_LEN];
extern uint8_t my_mac[MAC_LEN];
extern uint32_t key[XXTEA_KEY_LEN];

extern void wifi_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type);
