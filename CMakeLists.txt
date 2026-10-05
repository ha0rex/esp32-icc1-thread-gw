# ESP32-S3 + IKEA ICC-1-A OpenThread RCP standalone Matter controller.
cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{ESP_MATTER_PATH})
    message(FATAL_ERROR
        "ESP_MATTER_PATH is not set. Run 'source scripts/export-sdk.sh' "
        "after scripts/bootstrap.sh.")
endif()

set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)

# ESP-Matter must be added before project() so its Kconfig and components are
# visible while sdkconfig is generated.
# HomeKit SDK lives in the sibling Zigbee gateway tree. This build only reads
# those components; it does not modify that project.
set(ICC_HAP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../esp32-icc1-zigbee-gw/components")
if(NOT EXISTS "${ICC_HAP_ROOT}/esp_hap_core/CMakeLists.txt")
    message(FATAL_ERROR
        "HomeKit components were not found at ${ICC_HAP_ROOT}. "
        "The Zigbee gateway checkout must sit next to this repo.")
endif()

set(EXTRA_COMPONENT_DIRS
    "${MATTER_SDK_PATH}/config/esp32/components"
    "${ESP_MATTER_PATH}/components"
    "${ICC_HAP_ROOT}/esp_hap_core"
    "${ICC_HAP_ROOT}/esp_hap_apple_profiles"
    "${ICC_HAP_ROOT}/esp_hap_extras"
    "${ICC_HAP_ROOT}/mu_srp"
    "${ICC_HAP_ROOT}/hkdf-sha"
)

set(PROJECT_VER "0.2.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_icc1_thread_gw)

idf_build_set_property(
    CXX_COMPILE_OPTIONS
    "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual;-Wno-error=unused-result"
    APPEND
)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)

# This controller provisions the Thread dataset itself; it is not a Matter
# Thread commissionee.
idf_build_set_property(
    COMPILE_OPTIONS
    "-D_NO_GENERIC_THREAD_NETWORK_COMMISSIONING_DRIVER_;-Wno-format-nonliteral;-Wno-format-security;-Wformat=0"
    APPEND
)
