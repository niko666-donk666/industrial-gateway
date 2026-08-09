cmake_minimum_required(VERSION 3.21)

project(IndustrialGatewayUpperComputer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# MainWindow does not declare Q_OBJECT or custom Qt signals, so moc is unnecessary.
# Keeping AUTOMOC disabled also avoids a MinGW/CMake path-encoding failure when
# this project is stored below the Chinese directory name "D盘资料".
set(CMAKE_AUTOMOC OFF)

find_package(Qt6 6.11 REQUIRED COMPONENTS Widgets SerialPort)

qt_add_executable(industrial_gateway_upper_computer
    main.cpp
    mainwindow.cpp
    mainwindow.h
)

target_link_libraries(industrial_gateway_upper_computer PRIVATE
    Qt6::Widgets
    Qt6::SerialPort
)

set_target_properties(industrial_gateway_upper_computer PROPERTIES
    WIN32_EXECUTABLE TRUE
)
