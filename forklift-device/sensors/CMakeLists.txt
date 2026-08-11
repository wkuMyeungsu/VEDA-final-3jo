# forklift-device/sensors 트랙의 첫 CMake 빌드 대상.
#
# main_test.cpp / test_mpu6050_real.cpp는 기존 관례대로 파일 상단 주석에 적힌
# g++ 단발 명령으로 계속 빌드한다(순수 헤더 전용 로직 검증용이라 별도 빌드
# 시스템이 필요 없었음) - 이 CMake 프로젝트에는 일부러 포함하지 않았다.
#
# sensor_uplink_publisher는 libmosquitto에 링크해야 해서 처음으로 진짜 빌드
# 시스템이 필요해졌고, server/CMakeLists.txt와 같은 pkg-config 패턴을 그대로 따른다.

cmake_minimum_required(VERSION 3.15)
project(forklift_sensors LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

# 단말->서버 센서 업링크(MQTT 발행) - sensor_uplink_publisher가 사용.
pkg_check_modules(MOSQUITTO REQUIRED IMPORTED_TARGET libmosquitto)

set(SENSORS_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(sensor_uplink_publisher
    apps/sensor_uplink_publisher/main.cpp
)
target_include_directories(sensor_uplink_publisher PRIVATE ${SENSORS_ROOT_DIR})
target_link_libraries(sensor_uplink_publisher PRIVATE PkgConfig::MOSQUITTO Threads::Threads)
