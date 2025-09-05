FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(SRCS "sendrecv.c" "main.c"
                    INCLUDE_DIRS "../include"
                    PRIV_REQUIRES nvs_flash esp_adc esp_event esp_netif esp_wifi esp_timer driver)

