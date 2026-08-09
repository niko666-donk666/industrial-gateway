cmake_minimum_required(VERSION 3.16)

# ESP-IDF on Windows may otherwise use the active GBK locale when this
# project resides under a Chinese user profile path. The generated config
# files are UTF-8, so force UTF-8 mode before ESP-IDF launches Python.
if(WIN32)
    set(ENV{PYTHONUTF8} "1")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_gateway)
