#pragma once

#include <stdint.h>

#include "main.h"

typedef struct {
    uint32_t count;
    int64_t timeM;
    int64_t timeAP;
} __attribute__((packed)) MtoS_packet_t;

typedef struct {
    uint32_t count;
    int64_t timeAP;
    int64_t timeM;
    int64_t timeS;
    int64_t offset;
    double drift_filtered_ppm;
    double drift_unfiltered_ppm;
} S_packet_t;

// structure of every insystem message
typedef struct {
    uint8_t msg_type;
    uint8_t sla_num;
    uint16_t crc;
    uint8_t payload[MAXIMUM_MESSAGE_SIZE - GENERAL_HEADER_LEN];
} __attribute__((packed)) tokentrain_data_t;
