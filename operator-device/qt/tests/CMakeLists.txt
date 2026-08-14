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
safety_add_test(test_ttc_estimator test_ttc_estimator.cpp)

safety_add_test(test_scenario_player test_scenario_player.cpp)
# - approach_head_on.json/lateral_pass.json/network_loss.json을 진짜 저장소 파일 그대로 읽어서
#   검증하기 위한 소스 경로 (인라인 JSON 사본을 따로 안 두고, 실제로 배포되는 파일이 맞는지까지 확인)
target_compile_definitions(test_scenario_player PRIVATE SAFETY_SCENARIOS_DIR="${CMAKE_SOURCE_DIR}/config/scenarios")
