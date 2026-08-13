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

# risk_event 발행(ResultPublisher)과 센서 업링크 수신(SensorUplinkReceiver)이 공통으로 쓰는 MQTT 클라이언트 라이브러리.
pkg_check_modules(MOSQUITTO REQUIRED IMPORTED_TARGET libmosquitto)

set(SERVER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

# 벤더링한 외부 헤더 라이브러리 (nlohmann/json single-header). 출처·버전·왜 시스템
# 패키지가 아닌지는 third_party/README.md 참고.
set(SERVER_THIRD_PARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# 설정 계층: 모든 지게차가 공유하는 안전 서버 JSON 설정 로드
# json.hpp는 무겁고(약 900KB) 이 타깃 안에서만 필요하므로 include를 PRIVATE로 막는다.
add_library(server_config STATIC
    src/config/safety_server_config.cpp
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
    src/logic/tracking/cross_camera_reid.cpp
    src/logic/tracking/marker_channel_tracker.cpp
)
target_include_directories(server_tracking PUBLIC ${SERVER_SRC_DIR})
target_link_libraries(server_tracking PUBLIC Threads::Threads)

add_library(server_homography STATIC
    src/logic/homography/homography_transformer.cpp
)
target_include_directories(server_homography
    PUBLIC ${SERVER_SRC_DIR}
    PRIVATE ${SERVER_THIRD_PARTY_DIR}
)
target_link_libraries(server_homography PUBLIC server_config)

# 판정 계층
add_library(server_judgment STATIC
    src/logic/judgment/danger_judgment_engine.cpp
    src/logic/judgment/judgment_pipeline.cpp
)
target_include_directories(server_judgment PUBLIC ${SERVER_SRC_DIR})
target_link_libraries(server_judgment PUBLIC server_tracking server_config Threads::Threads)

# 프레임 파이프라인: ArUco ID 식별부터 H 변환·최근접 선택·위험 판정까지 한 경로로 묶는다.
add_library(server_frame_pipeline STATIC
    src/logic/pipeline/safety_frame_pipeline.cpp
)
target_include_directories(server_frame_pipeline PUBLIC ${SERVER_SRC_DIR})
target_link_libraries(server_frame_pipeline PUBLIC
    server_input server_homography server_judgment server_tracking
)

add_library(server_network STATIC
    src/network/result_publisher.cpp
    src/network/sensor_uplink_receiver.cpp
    src/network/network_sensor_reader.cpp
)
target_include_directories(server_network PUBLIC ${SERVER_SRC_DIR})
# libmosquitto는 PUBLIC이다 - ResultPublisher가 헤더 온리라서 이 헤더를 include하는 쪽
# (main.cpp, 테스트)도 mosquitto.h와 링크가 함께 필요하다.
target_link_libraries(server_network PUBLIC server_judgment Threads::Threads PkgConfig::MOSQUITTO)

# 유일한 서버 실행 진입점. 기존 detection/object_detection/main.cpp을
# src/main.cpp으로 승격해 수신 계층을 여기서 조립한다.
add_executable(forklift_safety_server main.cpp)
target_include_directories(forklift_safety_server PRIVATE ${SERVER_SRC_DIR})
target_link_libraries(forklift_safety_server PRIVATE
    server_input
    server_logging
    server_config
    server_homography
    server_judgment
    server_frame_pipeline
    server_network
    PkgConfig::GST
)

add_executable(event_log_viewer tools/event_log_viewer_main.cpp)
target_include_directories(event_log_viewer PRIVATE ${SERVER_SRC_DIR})
target_link_libraries(event_log_viewer PRIVATE server_judgment SQLite::SQLite3)

enable_testing()
add_subdirectory(tests)
