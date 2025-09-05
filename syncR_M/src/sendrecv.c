#include <reent.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_crc.h"
#include "esp_random.h"
#include "esp_mac.h"
#include "driver/gptimer.h"

#include "main.h"
#include "datatypes.h"

static const char *TAG = "s&r";

static QueueHandle_t sendQueue;

static bool tx_setup_done_once = false;

static uint16_t sequence, apSeq = 0;

static uint8_t raw_frame[MAXIMUM_MESSAGE_SIZE + WIFI_PACKET_FIXED_SIZE + WIFI_FCS_LEN];
static uint8_t send_buffer[MAXIMUM_MESSAGE_SIZE];

#if ENCRYPTION
static uint32_t enc_recv_data[512];
static uint32_t enc_send_data[512];

uint32_t btea(uint32_t* v, ssize_t n, uint32_t* k) {
    uint32_t z, y, sum, e, q;
    ssize_t p;
    if (n > 1) {          /* Coding Part */
        q = 6 + 52/n;
        sum = 0;
        z = v[n - 1];
        for (; q > 0; --q) {
            sum += DELTA;
            e = (sum >> 2) & 3;
            for (p=0; p < (n - 1); p++) {
                y = v[p+1];
                z = v[p] += MX;
            }
            y = v[0];
            z = v[n - 1] += MX;
        }
        return 0;
    } else if (n < -1) {  /* Decoding Part */
        n = -n;
        q = 6 + 52/n;
        sum = q*DELTA;
        y = v[0];
        for (; q > 0; --q) {
            e = (sum >> 2) & 3;
            for (p = n - 1; p > 0; p--) {
                z = v[p-1];
                y = v[p] -= MX;
            }
            z = v[n - 1];
            y = v[0] -= MX;
            sum -= DELTA;
        }
        return 0;
    }
    return 1;
}
#endif

static esp_err_t wifi_send_raw(const uint8_t *dest_address, void *data, int len) {
    if (!tx_setup_done_once) {
        memset(raw_frame, 0, sizeof(raw_frame));

        // beacon frame type
        raw_frame[0] = 0xD0;
        raw_frame[1] = 0x00;

        // Duration
        raw_frame[2] = 0x00;
        raw_frame[3] = 0x00;

        memcpy(&raw_frame[10], my_mac, MAC_LEN);  // source MAC
        memcpy(&raw_frame[16], s_broadcast_mac, MAC_LEN);  // BSSID

        raw_frame[27] = VENDOR_SPECIFIC_TAG_NUMBER;

        raw_frame[29] = 0x19;  // OUI
        raw_frame[30] = 0x88;
        raw_frame[31] = 0x42;

        tx_setup_done_once = true;
        // randomize sequence number
        sequence = esp_random();
    } else {
        memset(&raw_frame[32], 0, sizeof(raw_frame) - WIFI_PACKET_FIXED_SIZE);
        if (sequence == UINT16_MAX)
            sequence = 0;
        else
            sequence++;
    }

    memcpy(&raw_frame[22], &sequence, 2);

    memcpy(&raw_frame[4], dest_address, MAC_LEN);

    size_t pos = 27;
    size_t offset = 0;
    while (offset < len) {
        size_t chunk = len - offset;
        if (chunk > 252) {
            chunk = 252;
        }

        raw_frame[pos + 0] = 0xDD;

        raw_frame[pos + 1] = (uint8_t)(OUI_LEN + chunk);

        raw_frame[pos + 2] = 0x19;
        raw_frame[pos + 3] = 0x88;
        raw_frame[pos + 4] = 0x42;

        memcpy(&raw_frame[pos + 5], data + offset, chunk);

        offset += chunk;
        pos += 5 + chunk;
    }

    int a = esp_wifi_80211_tx(WIFI_IF_STA, raw_frame, pos, false);
    return a;
}





void wifi_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
    uint8_t *raw_wifi_packet = pkt->payload;

    uint8_t first_byte = raw_wifi_packet[0];

    if ((first_byte & 0xFC) == 0x80) {
        uint8_t *helper = raw_wifi_packet + 24 + 12 + 1;  // behind mac header (24) and to SSID (12) + ID (1)

        uint8_t len = *helper++;

        if (memcmp(helper, WIFI_SSID, len) == 0) {
            helper -= 14;
            uint64_t apTime = *(uint64_t*)helper;
            if (apSeq != *(uint16_t*)(helper - 2)) {
                apSeq = *(uint16_t*)(helper - 2);

            } else {
                return;
            }

            uint64_t myTime = 0;

            gptimer_get_raw_count(gptimer, &myTime);
            MtoS_packet_t mPacket;
            mPacket.count = apSeq;
            mPacket.timeM = myTime;
            mPacket.timeAP = apTime;

            timer_open();
            gptimer_set_raw_count(gptimerRisingEdge, 0);
            gptimer_start(gptimerRisingEdge);

            if (xQueueSend(sendQueue, &mPacket, 10 / portTICK_PERIOD_MS) != pdTRUE) {
                ESP_LOGE(TAG, "Failed to send packet to queue");
            }
        }
    }

    return;
}

void send_data(uint8_t *dest_mac, void *data, msg_type_t msg_type, uint16_t len) {
    uint8_t toBeAdded = 0;
    uint8_t my_index = 0;

    #if ENCRYPTION
    toBeAdded = 4 - ((len + GENERAL_HEADER_LEN) % 4);  // ! need multiple of four for xxtea encryption
    if (toBeAdded == 4)
        toBeAdded = 0;
    #endif

    uint16_t data_length = len + GENERAL_HEADER_LEN;

    memset(send_buffer, 0, MAXIMUM_MESSAGE_SIZE);
    uint16_t crc = 0;

    memcpy(send_buffer, &msg_type, 1);
    memcpy(send_buffer + 1, &my_index, 1);
    memcpy(send_buffer + 2, &crc, 2);
    memcpy(send_buffer + 4, data, data_length - GENERAL_HEADER_LEN);

    crc = esp_crc16_le(UINT16_MAX, send_buffer, data_length + toBeAdded);
    memcpy(send_buffer + 2, &crc, 2);

    #if ENCRYPTION  // * takes about 400 us with the maximum send length of 1426 bytes
    // uint64_t timer = esp_timer_get_time();
    memset(enc_send_data, 0, data_length + toBeAdded);
    memcpy(enc_send_data, send_buffer, data_length + toBeAdded);
    btea(enc_send_data, (data_length + toBeAdded)/4, key);
    // printf("Time: %lld\n", esp_timer_get_time() - timer);

    memcpy(send_buffer, enc_send_data, data_length + toBeAdded);
    #endif

    if (wifi_send_raw(dest_mac, send_buffer, data_length + toBeAdded) != ESP_OK) {
        ESP_LOGE(TAG, "Send error: dest:" MACSTR "", MAC2STR(dest_mac));
    }
    ESP_LOGD(TAG, "Sent data to: " MACSTR " len: %d", MAC2STR(dest_mac), data_length + toBeAdded);
}

// used to decouple the sending of the timestamps from the receive-callback
void sendTask(void *pvParameter) {
    sendQueue = xQueueCreate(10, sizeof(MtoS_packet_t));
    if (sendQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create send queue");
        vTaskDelete(NULL);
    }
    MtoS_packet_t timePacket;

    while (xQueueReceive(sendQueue, &timePacket, portMAX_DELAY)) {
        vTaskDelay(5 / portTICK_PERIOD_MS);

        send_data(s_broadcast_mac, &timePacket, FOLLOW_UP, sizeof(MtoS_packet_t));
    }
}
