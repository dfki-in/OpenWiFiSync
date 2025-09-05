#include "driver/gptimer.h"
#include "esp_log.h"
#include "hal/timer_types.h"
#include "soc/gpio_struct.h"

#include "main.h"

#define PULSE_WIDTH_US 1000
#define TOGGLE_DELAY_US 50000

static const char *TAG = "tim";

gptimer_handle_t gptimerRisingEdge = NULL;
gptimer_handle_t gptimerFallingEdge = NULL;

static bool IRAM_ATTR isr_oneshot_rising(gptimer_handle_t t, const gptimer_alarm_event_data_t *e, void *arg) {
    GPIO.out_w1ts = (1U << SYNC_PIN);

    gptimer_set_raw_count(gptimerFallingEdge, 0);
    gptimer_start(gptimerFallingEdge);
    gptimer_stop(t);
    return false;
}

static bool IRAM_ATTR isr_oneshot_falling(gptimer_handle_t t, const gptimer_alarm_event_data_t *e, void *arg) {
    GPIO.out_w1tc = (1U << SYNC_PIN);
    gptimer_stop(t);
    return false;
}

esp_err_t timer_open(void) {
    if (!gptimerRisingEdge) {
        gptimer_config_t cfg = {
            .clk_src       = GPTIMER_CLK_SRC_DEFAULT,
            .direction     = GPTIMER_COUNT_UP,
            .resolution_hz = 1000000
        };
        ESP_ERROR_CHECK(gptimer_new_timer(&cfg, &gptimerRisingEdge));
        gptimer_alarm_config_t alarm = {
            .alarm_count          = TOGGLE_DELAY_US,
            .flags.auto_reload_on_alarm = false,
        };
        ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimerRisingEdge, &alarm));
        gptimer_event_callbacks_t cb = { .on_alarm = isr_oneshot_rising };
        ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimerRisingEdge, &cb, NULL));
        ESP_ERROR_CHECK(gptimer_enable(gptimerRisingEdge));
    }

    if (!gptimerFallingEdge) {
        gptimer_config_t cfg = {
            .clk_src       = GPTIMER_CLK_SRC_DEFAULT,
            .direction     = GPTIMER_COUNT_UP,
            .resolution_hz = 1000000
        };
        ESP_ERROR_CHECK(gptimer_new_timer(&cfg, &gptimerFallingEdge));
        gptimer_alarm_config_t alarm = {
            .alarm_count          = PULSE_WIDTH_US,
            .flags.auto_reload_on_alarm = false,
        };
        ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimerFallingEdge, &alarm));
        gptimer_event_callbacks_t cb = { .on_alarm = isr_oneshot_falling };
        ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimerFallingEdge, &cb, NULL));
        ESP_ERROR_CHECK(gptimer_enable(gptimerFallingEdge));
    }

    return ESP_OK;
}

// using gptimer as the clock to be synchronized
void syncTimerTask(void *pvParameter) {
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,  // 1MHz, 1 tick=1us
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));
    ESP_ERROR_CHECK(gptimer_enable(gptimer));

    ESP_ERROR_CHECK(gptimer_start(gptimer));

    ESP_LOGI(TAG, "Timer started");

    vTaskDelete(NULL);
}
