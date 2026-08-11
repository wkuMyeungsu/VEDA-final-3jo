cmake_minimum_required(VERSION 3.15)
project(forklift_safety_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/utf-8)
endif()

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)

pkg_check_modules(GST REQUIRED IMPORTED_TARGET
    gstreamer-1.0
    gstreamer-app-1.0
)
pkg_check_modules(PUGIXML REQUIRED IMPORTED_TARGET pugixml)

set(SERVER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

# 벤더링한 외부 헤더 라이브러리 (nlohmann/json single-header). 출처·버전·왜 시스템
# 패키지가 아닌지는 third_party/README.md 참고.
set(SERVER_THIRD_PARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# 설정 계층: 단말별 JSON 설정 로드
# json.hpp는 무겁고(약 900KB) 이 타깃 안에서만 필요하므로 include를 PRIVATE로 막는다.
add_library(server_config STATIC
    src/config/terminal_config.cpp
)
target_include_directories(server_config
    PUBLIC  ${SERVER_SRC_DIR}
    PRIVATE ${SERVER_THIRD_PARTY_DIR}
)

# 입력 계층: RTSP/RTP 수신, XML 재조립, 메타데이터 라우팅·파싱
add_library(server_input STATIC
    src/input/rtp_metadata_receiver.cpp
    src/input/onvif_metadata_parser.cpp
    src/input/aruco_metadata_parser.cpp
    src/input/metadata_router.cpp
)
target_include_directories(server_input PUBLIC ${SERVER_SRC_DIR})
target_link_libraries(server_input PUBLIC PkgConfig::PUGIXML)

# 로깅 계층: 감지 결과 CSV 및 판정/레이턴시 기록
add_library(server_logging STATIC
    src/logging/csv_logger.cpp
    src/logging/aruco_csv_logger.cpp
    src/logging/event_logger.cpp
    src/logging/latency_logger.cpp
)
target_include_directories(server_logging PUBLIC ${SERVER_SRC_DIR})
target_link_libraries(server_logging PUBLIC SQLite::SQLite3 Threads::Threads)

# 추적 계층
add_library(server_tracking STATIC
    src/logic/tracking/nearest_person_selector.cpp
    src/logic/tracking/marker_channel_tracker.cpp
)
target_include_directories(server_tracking PUBLIC ${SERVER_SRC_DIR})
target_link_libraries(server_tracking PUBLIC Threads::Threads)

# 판정 계층
add_library(server_judgment STATIC
    src/logic/judgment/danger_judgment_engine.cpp
    src/logic/judgment/judgment_pipeline.cpp
)
target_include_directories(server_judgment PUBLIC ${SERVER_SRC_DIR})
target_link_libraries(server_judgment PUBLIC server_tracking Threads::Threads)

add_library(server_network STATIC
    src/network/result_publisher.cpp
    src/network/sensor_uplink_receiver.cpp
)
target_include_directories(server_network PUBLIC ${SERVER_SRC_DIR})
target_link_libraries(server_network PUBLIC server_judgment Threads::Threads)

# 유일한 서버 실행 진입점. 기존 detection/object_detection/main.cpp을
# src/main.cpp으로 승격해 수신 계층을 여기서 조립한다.
add_executable(forklift_safety_server src/app/main.cpp)
target_include_directories(forklift_safety_server PRIVATE ${SERVER_SRC_DIR})
target_link_libraries(forklift_safety_server PRIVATE
    server_input
    server_logging
    server_config
    server_judgment
    server_network
    PkgConfig::GST
)

add_executable(event_log_viewer _tools/event_log_viewer_main.cpp)
target_include_directories(event_log_viewer PRIVATE ${SERVER_SRC_DIR})
target_link_libraries(event_log_viewer PRIVATE server_judgment SQLite::SQLite3)

enable_testing()
add_subdirectory(tests)
