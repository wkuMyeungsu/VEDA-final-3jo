add_executable(test_common_types common_types_test.cpp)
target_include_directories(test_common_types PRIVATE ${SERVER_SRC_DIR})
add_test(NAME common_types_test COMMAND test_common_types)

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

add_executable(test_result_publisher test_result_publisher.cpp)
target_link_libraries(test_result_publisher PRIVATE server_judgment Threads::Threads)
add_test(NAME result_publisher_test COMMAND test_result_publisher)
