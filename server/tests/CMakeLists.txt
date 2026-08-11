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

# [2026-08-11 TCP -> MQTT 전환] 일시 제외.
# test_result_publisher.cpp는 수신부가 TCP 단말(LineClient가 19001로 connect)이고
# ResultPublisher(bind_host, port) 생성자 / LinkState::LISTENING을 그대로 쓰고 있어
# MQTT 전환 후에는 컴파일조차 되지 않는다. 재작성 방안 확정 전까지 빌드에서 뺀다.
#
# 참고: 이 파일의 테스트 7/8/10(전송 정책 - 변화 감지, 하트비트 주기, idle 프라이밍)은
# RecordingSink 람다만 쓰는 ResultDispatcher 단독 테스트라 소켓/브로커와 무관하다.
# 재작성 시 그 3개는 그대로 살릴 수 있다.
# add_executable(test_result_publisher test_result_publisher.cpp)
# target_link_libraries(test_result_publisher PRIVATE server_judgment server_network Threads::Threads)
# add_test(NAME result_publisher_test COMMAND test_result_publisher)

add_executable(test_terminal_config test_terminal_config.cpp)
target_link_libraries(test_terminal_config PRIVATE server_config)
add_test(NAME terminal_config_test COMMAND test_terminal_config)

# 시간 주입(update(..., now))으로 검증하므로 lost_grace 만큼 실제로 기다리지 않는다.
add_executable(test_marker_channel_tracker test_marker_channel_tracker.cpp)
target_link_libraries(test_marker_channel_tracker PRIVATE server_tracking)
add_test(NAME marker_channel_tracker_test COMMAND test_marker_channel_tracker)

add_executable(test_camera_assignment_server test_camera_assignment_server.cpp)
target_include_directories(test_camera_assignment_server PRIVATE ${SERVER_SRC_DIR})
target_link_libraries(test_camera_assignment_server PRIVATE Threads::Threads)
add_test(NAME camera_assignment_server_test COMMAND test_camera_assignment_server)

# hello 타임아웃(5초+) 실시간 대기 테스트라 기본 빌드/ctest 루프에서 뺀다.
# `cmake --build build --target test_camera_assignment_server_timeout`으로 명시 빌드 후 직접 실행.
add_executable(test_camera_assignment_server_timeout EXCLUDE_FROM_ALL
    test_camera_assignment_server_timeout.cpp
)
target_include_directories(test_camera_assignment_server_timeout PRIVATE ${SERVER_SRC_DIR})
target_link_libraries(test_camera_assignment_server_timeout PRIVATE Threads::Threads)
