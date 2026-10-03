idf_component_register(
    SRCS "spinel_host.c"
    INCLUDE_DIRS "include"
    REQUIRES icc_uart esp_timer freertos esp_common log
)
