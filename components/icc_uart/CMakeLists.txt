idf_component_register(
    SRCS "icc_uart.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_uart freertos esp_common log
)
