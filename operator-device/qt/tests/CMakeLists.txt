find_package(Qt6 REQUIRED COMPONENTS Test)

function(safety_add_test test_name source_file)
    qt_add_executable(${test_name} ${source_file})
    target_link_libraries(${test_name} PRIVATE safety_common Qt6::Test Qt6::Core)
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

safety_add_test(test_config_loader test_config_loader.cpp)
safety_add_test(test_mock_metadata_source test_mock_metadata_source.cpp)
safety_add_test(test_bbox_aspect_fit test_bbox_aspect_fit.cpp)
safety_add_test(test_demo_source test_demo_source.cpp)
