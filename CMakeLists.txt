# ESP32 + IKEA ICC-1-A Thread RCP → Matter / OpenThread Border Router host
cmake_minimum_required(VERSION 3.16)

# Embedded in esp_app_desc; compared against GitHub OTA manifest "version".
set(PROJECT_VER "0.1.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_icc1_thread_gw)
