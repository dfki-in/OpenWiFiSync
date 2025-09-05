#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "esp_wifi_types_generic.h"

#include "main.h"

static const char *TAG = "main";

uint32_t wifi_rate = WIFI_PHY_RATE_11M_L;
uint32_t bandwidth = WIFI_BW_HT20;
uint32_t max_tx_power = 80;  // 20 dBm

uint8_t s_broadcast_mac[MAC_LEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
uint8_t my_mac[MAC_LEN];
uint32_t key[XXTEA_KEY_LEN] = {0x01234567, 0x89ABCDEF, 0xFEDCBA98, 0x76543210};


static void wifi_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_wifi_config_80211_tx_rate(ESP_IF_WIFI_STA, wifi_rate);

    ESP_ERROR_CHECK(esp_wifi_set_storage((wifi_storage_t)WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode((wifi_interface_t)WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    int8_t power;
    ESP_ERROR_CHECK(esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE));
    esp_wifi_set_max_tx_power(max_tx_power);
    esp_wifi_get_max_tx_power(&power);
    esp_wifi_set_bandwidth(ESP_IF_WIFI_STA, (wifi_bandwidth_t)bandwidth);
    ESP_LOGI(TAG, "Max TX power: %d dBm", power/4);
}

static void initialize_promiscuous(void) {
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(wifi_rx_cb));
    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT
    };
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_filter(&filter));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));

    esp_wifi_get_mac((wifi_interface_t)WIFI_IF_STA, my_mac);
    ESP_LOGI(TAG, "My MAC-Address is: "MACSTR"", MAC2STR(my_mac));
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init();
    initialize_promiscuous();
}
