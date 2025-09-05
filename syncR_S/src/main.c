#include "freertos/idf_additions.h"
#include "hal/gpio_types.h"
#include "nvs_flash.h"
#include "driver/gptimer.h"  // IWYU pragma: keep
#include "driver/gpio.h"

#include "main.h"

gptimer_handle_t gptimer;
uint64_t timeGPIO = 0;

uint8_t s_broadcast_mac[MAC_LEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
uint8_t my_mac[MAC_LEN];
uint32_t key[XXTEA_KEY_LEN] = {0x01234567, 0x89ABCDEF, 0xFEDCBA98, 0x76543210};

// * GPIO is used to test the synchronization by generating square waves for osci
void initGPIO(void) {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << SYNC_PIN),
        .pull_down_en = 0,
        .pull_up_en = 0
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(SYNC_PIN, 0);
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    initGPIO();

    wifi_init_sta();
    initialize_promiscuous();

    xTaskCreate(syncTimerTask, "syncTimerTask", 4096, NULL, 5, NULL);
    xTaskCreate(calc_drift_task, "calc_drift_task", 4096, NULL, 5, NULL);
}
