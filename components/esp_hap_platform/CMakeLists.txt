# IDF 5.4 override of the sibling Zigbee gateway's esp_hap_platform.
# That component always adds an mbedtls 4 include path which does not exist
# on ESP-IDF 5.4. Sources stay in the Zigbee tree and are only compiled here.
set(HAP_PLATFORM "${CMAKE_CURRENT_LIST_DIR}/../../../esp32-icc1-zigbee-gw/components/esp_hap_platform")
get_filename_component(HAP_PLATFORM "${HAP_PLATFORM}" ABSOLUTE)

set(srcs
    "${HAP_PLATFORM}/src/esp_mfi_aes.c"
    "${HAP_PLATFORM}/src/esp_mfi_base64.c"
    "${HAP_PLATFORM}/src/esp_mfi_rand.c"
    "${HAP_PLATFORM}/src/esp_mfi_sha.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/hap_platform_httpd.c"
    "${HAP_PLATFORM}/src/hap_platform_keystore.c"
    "${HAP_PLATFORM}/src/hap_platform_memory.c"
    "${HAP_PLATFORM}/src/hap_platform_os.c"
)

if(NOT CONFIG_IDF_TARGET_ESP8266)
    list(APPEND srcs "${HAP_PLATFORM}/src/esp_mfi_i2c.c")
endif()

if("${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}" VERSION_GREATER_EQUAL "5.0")
    list(APPEND priv_req driver)
endif()

set(priv_inc)
set(mbedtls_private "${IDF_PATH}/components/mbedtls/mbedtls/tf-psa-crypto/drivers/builtin/include/mbedtls/private/aes.h")
if(EXISTS "${mbedtls_private}")
    list(APPEND priv_inc "${HAP_PLATFORM}/mbedtls_compat")
    get_filename_component(mbedtls_priv_inc "${mbedtls_private}" DIRECTORY)
    get_filename_component(mbedtls_priv_inc "${mbedtls_priv_inc}" DIRECTORY)
    get_filename_component(mbedtls_priv_inc "${mbedtls_priv_inc}" DIRECTORY)
    list(APPEND priv_inc "${mbedtls_priv_inc}")
endif()

idf_component_register(SRCS ${srcs}
                        INCLUDE_DIRS "${HAP_PLATFORM}/include"
                        PRIV_INCLUDE_DIRS ${priv_inc}
                        REQUIRES esp_http_server
                        PRIV_REQUIRES ${priv_req} mbedtls nvs_flash mdns esp_hap_core)
component_compile_options(-Wno-unused-function -std=gnu17)
