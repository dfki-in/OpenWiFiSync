#include <reent.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_crc.h"
#include "esp_random.h"
#include "esp_mac.h"
#include "driver/gptimer.h"
#include "esp_timer.h"  // IWYU pragma: keep

#include "freertos/idf_additions.h"
#include "main.h"
#include "datatypes.h"

#define sPacketBUFFERSIZE 100
#define FILTER_WINDOW_SIZE 1024

static const char *TAG = "s&r";

static bool tx_setup_done_once = false;
static bool setup_done = false;

static uint16_t sequence, apSeq = 0;
static int countArray = 0;
static uint64_t myTime = 0;

static uint8_t filterWR  = 0;
static double filterAccumulator = 0.0;

static uint8_t raw_frame[MAXIMUM_MESSAGE_SIZE + WIFI_PACKET_FIXED_SIZE + WIFI_FCS_LEN];
static uint8_t send_filterBuffer[MAXIMUM_MESSAGE_SIZE];
static uint8_t incoming_data[2048];
static double filterBuf[FILTER_WINDOW_SIZE];
static S_packet_t sPacket[sPacketBUFFERSIZE] = { 0 };
static const uint8_t DFKI_OUI[3] = { 0x19, 0x88, 0x42 };

static QueueHandle_t timeQueue = NULL;

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

void wifi_rx_cb(void *filterBuf, wifi_promiscuous_pkt_type_t type) {
    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)filterBuf;
    uint8_t *raw_wifi_packet = pkt->payload;
    int packet_len = pkt->rx_ctrl.sig_len;

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

            myTime = 0;

            gptimer_get_raw_count(gptimer, &myTime);

            countArray++;
            if (countArray >= sPacketBUFFERSIZE)
                countArray = 0;

            sPacket[countArray].count = apSeq;
            sPacket[countArray].timeAP = apTime;
            sPacket[countArray].timeS = myTime;
            sPacket[countArray].timeM = 0;
        }
        return;
    }

    if (pkt->rx_ctrl.sig_len < 32) {
        return;
    }

    int ies_start = 27;
    int ies_end   = packet_len - 4;  // * exclude the FCS
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

    if (msg_type == FOLLOW_UP) {
        MtoS_packet_t mPacket = *(MtoS_packet_t*)(data_in_recv_cb->payload);

        if (setup_done) {
            if ((sPacket[countArray].count == mPacket.count) && (sPacket[countArray].timeAP == mPacket.timeAP)) {
                sPacket[countArray].timeM = mPacket.timeM;
                sPacket[countArray].offset = sPacket[countArray].timeM - sPacket[countArray].timeS;

                if ((sPacket[countArray].timeS != 0) && (sPacket[countArray].timeM != 0)) {
                    xQueueSend(timeQueue, &countArray, portMAX_DELAY);
                }

            } else {
                ESP_LOGD(TAG, "Sequence-Num or AP-Time mismatch");
            }
        } else {
            gptimer_set_raw_count(gptimer, mPacket.timeM);
            setup_done = true;
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

    memset(send_filterBuffer, 0, MAXIMUM_MESSAGE_SIZE);
    uint16_t crc = 0;

    memcpy(send_filterBuffer, &msg_type, 1);
    memcpy(send_filterBuffer + 1, &my_index, 1);
    memcpy(send_filterBuffer + 2, &crc, 2);
    memcpy(send_filterBuffer + 4, data, data_length - GENERAL_HEADER_LEN);

    crc = esp_crc16_le(UINT16_MAX, send_filterBuffer, data_length + toBeAdded);
    memcpy(send_filterBuffer + 2, &crc, 2);

    #if ENCRYPTION  // * takes about 400 us when using maximum send length of 1426 bytes
    // uint64_t timer = esp_timer_get_time();
    memset(enc_send_data, 0, data_length + toBeAdded);
    memcpy(enc_send_data, send_filterBuffer, data_length + toBeAdded);
    btea(enc_send_data, (data_length + toBeAdded)/4, key);
    // printf("Time: %lld\n", esp_timer_get_time() - timer);

    memcpy(send_filterBuffer, enc_send_data, data_length + toBeAdded);
    #endif

    if (wifi_send_raw(dest_mac, send_filterBuffer, data_length + toBeAdded) != ESP_OK) {
        ESP_LOGE(TAG, "Send error: dest:" MACSTR "", MAC2STR(dest_mac));
    }
    ESP_LOGD(TAG, "Sent data to: " MACSTR " len: %d", MAC2STR(dest_mac), data_length + toBeAdded);
}



static inline double ma_filter_add(double sample) {
    filterAccumulator -= filterBuf[filterWR];  // * take out oldest entry
    filterBuf[filterWR] = sample;
    filterAccumulator += sample;
    filterWR = (filterWR + 1) & (FILTER_WINDOW_SIZE - 1);  // * cheap modulo
    return filterAccumulator / FILTER_WINDOW_SIZE;
}

void calc_drift_task(void *pvParameter) {
    timeQueue = xQueueCreate(10, sizeof(int));
    if (timeQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create time queue");
        vTaskDelete(NULL);
    }

    int count = 0;
    double dividend = 0;
    double divisor = 0;
    bool first_iteration = true;
    uint64_t timeNow = 0;

    while (xQueueReceive(timeQueue, &count, portMAX_DELAY) == pdTRUE) {
        if (first_iteration) {
            first_iteration = false;
            continue;
        }

        if (count > 0) {
            dividend = sPacket[count].offset - sPacket[count-1].offset;
            divisor  = sPacket[count].timeM - sPacket[count-1].timeM;
            sPacket[count].drift_unfiltered_ppm = dividend / divisor * 1.0e6;

        } else if (count == 0) {
            dividend = sPacket[count].offset - sPacket[sPacketBUFFERSIZE - 1].offset;
            divisor  = sPacket[count].timeM - sPacket[sPacketBUFFERSIZE - 1].timeM;
            sPacket[count].drift_unfiltered_ppm = dividend / divisor * 1.0e6;
        }

        sPacket[count].drift_filtered_ppm = ma_filter_add(sPacket[count].drift_unfiltered_ppm);
        timer_open();

        uint64_t toggle_time = sPacket[count].timeM + TOGGLE_DELAY_US;
        gptimer_get_raw_count(gptimer, &timeNow);
        uint64_t ttt = toggle_time + sPacket[count].offset - timeNow;                // * time left until toggle
        double skew_abs = (double)ttt * 1.0e-6 * sPacket[count].drift_filtered_ppm;  // * how much drift happens until toggletimepoint

        gptimer_alarm_config_t alarm_config = {
             .alarm_count = ttt + (int64_t)(skew_abs) - 12,  // * time estimate until toggle
        };
        gptimer_set_alarm_action(gptimerRisingEdge, &alarm_config);
        gptimer_set_raw_count(gptimerRisingEdge, 0);
        gptimer_start(gptimerRisingEdge);

        gptimer_get_raw_count(gptimer, &timeNow);
        ESP_ERROR_CHECK(gptimer_set_raw_count(gptimer, timeNow + sPacket[count].offset));  // * estimate master time now

        ESP_LOGD(TAG, "ttt: %lld us, skew_abs: %.2f us", ttt, skew_abs);

        send_data(s_broadcast_mac, &sPacket[count], SYNC_VAL_OBS, sizeof(S_packet_t));  // * for logging on observer
    }
}
