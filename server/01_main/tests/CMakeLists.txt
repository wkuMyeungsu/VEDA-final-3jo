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
add_executable(test_sensor_uplink_receiver test_sensor_uplink_receiver.cpp)
target_link_libraries(test_sensor_uplink_receiver PRIVATE server_network Threads::Threads)
add_test(NAME sensor_uplink_receiver_test COMMAND test_sensor_uplink_receiver)

add_executable(test_assignment_publisher test_assignment_publisher.cpp)
target_link_libraries(test_assignment_publisher PRIVATE server_network)
add_test(NAME assignment_publisher_test COMMAND test_assignment_publisher)

add_executable(test_safety_server_config test_safety_server_config.cpp)
target_link_libraries(test_safety_server_config PRIVATE server_config)
add_test(NAME safety_server_config_test COMMAND test_safety_server_config)

add_executable(test_homography_transformer test_homography_transformer.cpp)
target_link_libraries(test_homography_transformer PRIVATE server_homography)
add_test(NAME homography_transformer_test COMMAND test_homography_transformer)

# 실제 채널 3 H를 거쳐 ArUco ID·최근접 사람·판정·JSON·SQLite를 한 흐름으로 검증한다.
add_executable(test_channel3_safety_e2e test_channel3_safety_e2e.cpp)
target_compile_definitions(test_channel3_safety_e2e PRIVATE
    CHANNEL3_HOMOGRAPHY_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/homography_channel_3_mm.json"
)
target_link_libraries(test_channel3_safety_e2e PRIVATE
    server_frame_pipeline server_logging server_network SQLite::SQLite3 Threads::Threads
)
add_test(NAME channel3_safety_e2e_test COMMAND test_channel3_safety_e2e)

# 시간 주입(update(..., now))으로 검증하므로 lost_grace 만큼 실제로 기다리지 않는다.
add_executable(test_marker_channel_tracker test_marker_channel_tracker.cpp)
target_link_libraries(test_marker_channel_tracker PRIVATE server_tracking)
add_test(NAME marker_channel_tracker_test COMMAND test_marker_channel_tracker)
