idf_component_register(
    SRCS "matter_bridge.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_common log nvs_flash thread_host
)
