idf_component_register(
    SRCS "app_main.cpp" "boot_log.cpp" "gateway_nodes.cpp" "portal.cpp" "homekit_bridge.cpp" "matter_sniff.cpp" "peer_link.c"
    INCLUDE_DIRS "."
    PRIV_INCLUDE_DIRS "${ESP_MATTER_PATH}/examples/common/utils"
    REQUIRES nvs_flash esp_netif esp_wifi esp_http_server esp_http_client esp_timer
             esp_matter esp_matter_console esp_matter_controller openthread
             esp_hap_core esp_hap_apple_profiles espcoredump
             esp_driver_usb_serial_jtag
    LDFRAGMENTS "linker.lf"
)

if(CONFIG_SPIFFS_ATTESTATION_TRUST_STORE)
    spiffs_create_partition_image(paa_cert ${CMAKE_SOURCE_DIR}/paa_cert FLASH_IN_PROJECT)
endif()

target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
