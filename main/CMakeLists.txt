idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_event freertos wifi_manager web thread_host matter_bridge
)
