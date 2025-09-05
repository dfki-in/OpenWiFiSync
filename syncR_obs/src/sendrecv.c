#include <reent.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_crc.h"
#include "esp_mac.h"

#include "main.h"
#include "datatypes.h"

#define BUFFERSIZE 600

static const char *TAG = "s&r";

static int count, last_count = 0;

static uint8_t incoming_data[2048];
static const uint8_t DFKI_OUI[3] = { 0x19, 0x88, 0x42 };

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

void wifi_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
    uint8_t *raw_wifi_packet = pkt->payload;
    int packet_len = pkt->rx_ctrl.sig_len;

    if (pkt->rx_ctrl.sig_len < 32) {
        return;
    }

    int ies_start = 27;
    int ies_end   = packet_len - 4;  // exclude the FCS
    int offset = ies_start;

    size_t combined_len = 0;

    while ((offset + 2) <= ies_end) {
        uint8_t ie_id  = raw_wifi_packet[offset];
        uint8_t ie_len = raw_wifi_packet[offset + 1];
        int ie_total   = 2 + ie_len;

        if ((offset + ie_total) > ies_end) {
            // * should only happen if length field is set incorrectly
            break;
        }

        if (ie_id == 0xDD && ie_len >= 3) {
            const uint8_t *ie_oui = &raw_wifi_packet[offset + 2];
            if (memcmp(ie_oui, DFKI_OUI, 3) == 0) {
                size_t chunk_len = ie_len - 3;
                const uint8_t *chunk_data = &raw_wifi_packet[offset + 2 + 3];

                if ((combined_len + chunk_len) < sizeof(incoming_data)) {
                    memcpy(&incoming_data[combined_len], chunk_data, chunk_len);
                    combined_len += chunk_len;

                } else {
                    ESP_LOGE("TAG", "incoming message too large");
                    return;
                }
            }
        }

        offset += ie_total;
    }

    if (combined_len == 0) {
        return;
    }

    const uint8_t *src_addr = &raw_wifi_packet[10];

    const uint8_t *dest_addr = &raw_wifi_packet[4];
    ESP_LOGD(TAG, "Received data from: " MACSTR " to: " MACSTR " len: %d", MAC2STR(src_addr), MAC2STR(dest_addr), combined_len);

    if ((memcmp(dest_addr, my_mac, MAC_LEN) != 0 && memcmp(dest_addr, s_broadcast_mac, MAC_LEN) != 0)) {
        ESP_LOGD(TAG, "Destination address not mine");
        return;
    }

    #if ENCRYPTION
    if (combined_len % 4 != 0) {
        ESP_LOGE("rx_cb", "Combined len not multiple of 4");
        return;
    }
    if (combined_len > 2048) {
        ESP_LOGE("rx_cb", "Too large for enc_recv_data");
        return;
    }

    memset(enc_recv_data, 0, sizeof(enc_recv_data));
    memcpy(enc_recv_data, incoming_data, combined_len);

    btea(enc_recv_data, -(combined_len/4), key);

    tokentrain_data_t *data_in_recv_cb = (tokentrain_data_t *) enc_recv_data;
    #else
    tokentrain_data_t *data_in_recv_cb = (tokentrain_data_t *) incoming_data;
    #endif

    uint16_t crc, crc_cal = 0;
    crc = data_in_recv_cb->crc;
    data_in_recv_cb->crc = 0;
    crc_cal = esp_crc16_le(UINT16_MAX, (uint8_t const *)data_in_recv_cb, combined_len);
    if (!(crc_cal == crc)) {
        ESP_LOGE(TAG, "CRC-Check Fail: %d, %d", crc, crc_cal);
        return;
    }

    msg_type_t msg_type = data_in_recv_cb->msg_type;

    if (msg_type == SYNC_VAL_OBS) {
        S_packet_t sPacket = *(S_packet_t*)(data_in_recv_cb->payload);
        if (sPacket.count == last_count) {
            ESP_LOGI(TAG, "Received duplicate packet with count %ld", sPacket.count);
            return;
        } else {
            last_count = sPacket.count;
        }

        printf("S[%d]: %lld %f %f\n", count, sPacket.offset, sPacket.drift_filtered_ppm, sPacket.drift_unfiltered_ppm);
        count++;
    }
    return;
}
