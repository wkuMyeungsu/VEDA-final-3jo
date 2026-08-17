if(NOT Python3_Interpreter_FOUND)
    return()
endif()

add_test(
    NAME homography_global_alignment_test
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test_global_alignment.py
)

set_tests_properties(
    homography_global_alignment_test
    PROPERTIES
        LABELS "unit;homography"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)
