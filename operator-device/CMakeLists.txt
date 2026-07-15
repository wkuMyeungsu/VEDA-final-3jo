cmake_minimum_required(VERSION 3.21)

project(ForkliftControlCenter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(SAFETY_BUILD_TESTS "Build unit tests for common services" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Multimedia Network)

qt_standard_project_setup(REQUIRES 6.5)

# Directory that holds the runtime JSON config files (cameras.json, control_center.json).
# The app target copies this next to its executable after build so the program
# can be re-configured (e.g. real RTSP URLs) without rebuilding.
set(SAFETY_CONFIG_SOURCE_DIR "${CMAKE_SOURCE_DIR}/config")

add_subdirectory(common)
add_subdirectory(apps/control_center)

if(SAFETY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
