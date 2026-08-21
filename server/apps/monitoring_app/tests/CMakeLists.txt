add_test(
    NAME monitoring_app_status_test
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_server.py
)
set_tests_properties(monitoring_app_status_test PROPERTIES LABELS "unit;monitoring")
