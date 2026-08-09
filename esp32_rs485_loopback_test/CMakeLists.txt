cmake_minimum_required(VERSION 3.16)

if(WIN32)
    set(ENV{PYTHONUTF8} "1")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_rs485_loopback_test)
