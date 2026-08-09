idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_event esp_netif esp_wifi json mqtt nvs_flash
)
