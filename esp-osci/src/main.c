#include <stdbool.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "esp_timer.h"
#include "rom/ets_sys.h"

#define GPIO_M 4
#define GPIO_S 2
#define ROUNDABOUT_ITVL_US 10000

static volatile int64_t tS_prev  = -1;
static volatile int64_t tM_prev = -1;
static volatile int64_t tS_last = -1;
static volatile int64_t count  = 0;

static volatile bool waitM  = false;


static void isr_master(void *arg) {
    const int64_t now = esp_timer_get_time();
    tM_prev = now;

    if ((waitM) && (llabs(now - tS_last) <= ROUNDABOUT_ITVL_US)) {
        ets_printf("S[%lld]: %lld\n", count, now - tS_last);
        ++count;
        waitM = false;
    }
}

static void isr_slave(void *arg) {
    static bool first = true;
    const int64_t now = esp_timer_get_time();

    if (first) {
        first = false;
        return;
    }

    tS_prev = tS_last;
    tS_last = now;

    if (llabs(tM_prev - tS_last) <= ROUNDABOUT_ITVL_US) {
        ets_printf("S[%lld]: %lld\n", count, tM_prev - tS_last);
        ++count;

    } else {
        waitM = true;
    }
}

static void init_input(gpio_num_t pin, gpio_isr_t handler, bool pulldown) {
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode         = GPIO_MODE_INPUT,
        .intr_type    = GPIO_INTR_POSEDGE
    };
    gpio_config(&cfg);

    if (pulldown) {
        gpio_pullup_dis(pin);
        gpio_pulldown_en(pin);
    }

    gpio_isr_handler_add(pin, handler, NULL);
}

void app_main(void) {
    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);

    init_input(GPIO_M, isr_master, true);
    init_input(GPIO_S, isr_slave,  true);

    ets_printf("Watching GPIO %d (M) and %d (S)…\n", GPIO_M, GPIO_S);
}
