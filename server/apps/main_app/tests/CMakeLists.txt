add_executable(test_onvif_metadata_parser
    test_onvif_metadata_parser.cpp
)
target_link_libraries(test_onvif_metadata_parser PRIVATE server_input)
add_test(NAME onvif_metadata_parser_test COMMAND test_onvif_metadata_parser)

add_executable(test_metadata_router
    test_metadata_router.cpp
)
target_link_libraries(test_metadata_router PRIVATE server_input)
add_test(NAME metadata_router_test COMMAND test_metadata_router)

add_executable(test_aruco_metadata_parser
    test_aruco_metadata_parser.cpp
    ${SERVER_SRC_DIR}/logging/aruco_csv_logger.cpp
)
target_link_libraries(test_aruco_metadata_parser PRIVATE server_input)
add_test(NAME aruco_metadata_parser_test COMMAND test_aruco_metadata_parser)

add_executable(test_metadata_timing test_metadata_timing.cpp)
target_include_directories(test_metadata_timing PRIVATE ${SERVER_SRC_DIR})
add_test(NAME metadata_timing_test COMMAND test_metadata_timing)

add_executable(test_bounded_queue test_bounded_queue.cpp)
target_include_directories(test_bounded_queue PRIVATE ${SERVER_SRC_DIR})
add_test(NAME bounded_queue_test COMMAND test_bounded_queue)

add_executable(test_server_command_line_options
    test_server_command_line_options.cpp
    ${SERVER_SRC_DIR}/runtime/server_command_line_options.cpp
)
target_include_directories(test_server_command_line_options PRIVATE ${SERVER_SRC_DIR})
add_test(NAME server_command_line_options_test COMMAND test_server_command_line_options)

add_executable(test_judgment_engine test_exception_trigger.cpp)
target_link_libraries(test_judgment_engine PRIVATE server_judgment Threads::Threads)
add_test(NAME judgment_engine_test COMMAND test_judgment_engine)

# 거리 경계 히스테리시스(SAFE<->CAUTION 진동 억제).
add_executable(test_distance_hysteresis test_distance_hysteresis.cpp)
target_link_libraries(test_distance_hysteresis PRIVATE server_judgment Threads::Threads)
add_test(NAME distance_hysteresis_test COMMAND test_distance_hysteresis)

add_executable(test_announcement_gate test_announcement_gate.cpp)
target_link_libraries(test_announcement_gate PRIVATE server_judgment Threads::Threads)
add_test(NAME announcement_gate_test COMMAND test_announcement_gate)

add_executable(test_qt_client_e2e test_qt_client_e2e.cpp)
target_include_directories(test_qt_client_e2e PRIVATE ${SERVER_THIRD_PARTY_DIR})
target_link_libraries(test_qt_client_e2e PRIVATE server_judgment Threads::Threads)
add_test(NAME qt_client_e2e_test COMMAND test_qt_client_e2e)

add_executable(test_judgment_pipeline test_judgment_pipeline.cpp)
target_link_libraries(test_judgment_pipeline PRIVATE server_judgment Threads::Threads)
add_test(NAME judgment_pipeline_test COMMAND test_judgment_pipeline)

add_executable(test_event_logger test_event_logger.cpp)
target_link_libraries(test_event_logger PRIVATE server_logging server_judgment Threads::Threads)
add_test(NAME event_logger_test COMMAND test_event_logger)

# 단말→서버 센서 업링크 수신기.
# 외부 mosquitto 브로커가 필요한 통합 테스트이므로 기본 단위 테스트 묶음에는 넣지 않는다.
option(ENABLE_NETWORK_INTEGRATION_TESTS
       "Enable tests that require an external MQTT broker"
       OFF)
add_executable(test_sensor_uplink_receiver test_sensor_uplink_receiver.cpp)
target_link_libraries(test_sensor_uplink_receiver PRIVATE server_network Threads::Threads)

# 수신기(MQTT 캐시) -> SensorInput 융합 접착부. 브로커 필요 조건은 위와 동일.
add_executable(test_network_sensor_reader test_network_sensor_reader.cpp)
target_link_libraries(test_network_sensor_reader PRIVATE server_network Threads::Threads)
if(ENABLE_NETWORK_INTEGRATION_TESTS)
    add_test(NAME sensor_uplink_receiver_integration_test COMMAND test_sensor_uplink_receiver)
    add_test(NAME network_sensor_reader_integration_test COMMAND test_network_sensor_reader)
endif()

add_executable(test_assignment_publisher test_assignment_publisher.cpp)
target_link_libraries(test_assignment_publisher PRIVATE server_network)
add_test(NAME assignment_publisher_test COMMAND test_assignment_publisher)

add_executable(test_result_publisher test_result_publisher.cpp)
target_link_libraries(test_result_publisher PRIVATE server_network)
add_test(NAME result_publisher_test COMMAND test_result_publisher)

add_executable(test_safety_server_config test_safety_server_config.cpp)
target_link_libraries(test_safety_server_config PRIVATE server_config)
add_test(NAME safety_server_config_test COMMAND test_safety_server_config)

add_executable(test_homography_transformer test_homography_transformer.cpp)
target_link_libraries(test_homography_transformer PRIVATE server_homography)
add_test(NAME homography_transformer_test COMMAND test_homography_transformer)

# 고정 fixture와 테스트 입력으로 실제 파이프라인 연결을 검증한다.
# 실제 장비/실시간 입력을 사용하지 않으므로 운영 E2E 지표에는 포함하지 않는다.
add_executable(test_channel3_safety_pipeline_integration
    test_channel3_safety_pipeline_integration.cpp
)
target_compile_definitions(test_channel3_safety_pipeline_integration PRIVATE
    CHANNEL3_HOMOGRAPHY_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/homography_channel_3_pipeline.json"
)
target_link_libraries(test_channel3_safety_pipeline_integration PRIVATE
    server_frame_pipeline server_logging server_network SQLite::SQLite3 Threads::Threads
)
add_test(NAME channel3_safety_pipeline_integration_test
         COMMAND test_channel3_safety_pipeline_integration)

# FOV 기반 핸드오버(마커 world 위치가 활성 화면 밖으로 나갔을 때만 전환).
# 유예 시간 경과 확인에 실제 sleep이 2회 들어간다(~1.5초).
add_executable(test_fov_handover
    test_fov_handover.cpp
)
target_compile_definitions(test_fov_handover PRIVATE
    CHANNEL3_HOMOGRAPHY_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/homography_channel_3_pipeline.json"
)
target_link_libraries(test_fov_handover PRIVATE
    server_frame_pipeline server_logging server_network SQLite::SQLite3 Threads::Threads
)
add_test(NAME fov_handover_test COMMAND test_fov_handover)

# 시간 주입(update(..., now))으로 검증하므로 lost_grace 만큼 실제로 기다리지 않는다.

add_executable(test_nearest_person_selector test_nearest_person_selector.cpp)
target_link_libraries(test_nearest_person_selector PRIVATE server_tracking)
add_test(NAME nearest_person_selector_test COMMAND test_nearest_person_selector)

set_tests_properties(
    onvif_metadata_parser_test
    metadata_router_test
    aruco_metadata_parser_test
    metadata_timing_test
    bounded_queue_test
    server_command_line_options_test
    judgment_engine_test
    distance_hysteresis_test
    announcement_gate_test
    qt_client_e2e_test
    judgment_pipeline_test
    assignment_publisher_test
    result_publisher_test
    safety_server_config_test
    homography_transformer_test
    nearest_person_selector_test
    PROPERTIES
        LABELS "unit"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)

set_tests_properties(
    event_logger_test
    PROPERTIES
        LABELS "integration"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)

set_tests_properties(
    channel3_safety_pipeline_integration_test
    PROPERTIES
        LABELS "integration"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(ENABLE_NETWORK_INTEGRATION_TESTS)
    set_tests_properties(
        sensor_uplink_receiver_integration_test
        PROPERTIES
            LABELS "integration;requires-mqtt"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
endif()
