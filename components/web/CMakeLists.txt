idf_component_register(
    SRCS "web_server.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server nvs_flash freertos esp_timer esp_app_format wifi_manager
             thread_host matter_bridge spinel_host
)
