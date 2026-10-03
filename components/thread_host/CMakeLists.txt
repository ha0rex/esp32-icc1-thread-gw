idf_component_register(
    SRCS "thread_host.c"
    INCLUDE_DIRS "include"
    REQUIRES spinel_host freertos esp_common log nvs_flash
)
