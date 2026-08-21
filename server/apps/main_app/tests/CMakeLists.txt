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

add_executable(test_judgment_engine test_exception_trigger.cpp)
target_link_libraries(test_judgment_engine PRIVATE server_judgment Threads::Threads)
add_test(NAME judgment_engine_test COMMAND test_judgment_engine)

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
if(ENABLE_NETWORK_INTEGRATION_TESTS)
    add_test(NAME sensor_uplink_receiver_integration_test COMMAND test_sensor_uplink_receiver)
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

# 시간 주입(update(..., now))으로 검증하므로 lost_grace 만큼 실제로 기다리지 않는다.
add_executable(test_marker_stream_tracker test_marker_channel_tracker.cpp)
target_link_libraries(test_marker_stream_tracker PRIVATE server_tracking)
add_test(NAME marker_stream_tracker_test COMMAND test_marker_stream_tracker)

set_tests_properties(
    onvif_metadata_parser_test
    metadata_router_test
    aruco_metadata_parser_test
    metadata_timing_test
    bounded_queue_test
    judgment_engine_test
    judgment_pipeline_test
    assignment_publisher_test
    result_publisher_test
    safety_server_config_test
    homography_transformer_test
    marker_stream_tracker_test
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
