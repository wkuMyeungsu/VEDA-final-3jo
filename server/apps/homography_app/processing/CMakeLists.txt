cmake_minimum_required(VERSION 3.15)
project(homography_tool LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV REQUIRED COMPONENTS core imgproc calib3d aruco highgui imgcodecs)

add_library(homography_core STATIC
    src/core/calibration.cpp
    src/io/config.cpp
    src/io/json.cpp
    src/render/marker_board.cpp
)
target_include_directories(homography_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(homography_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party
)
target_link_libraries(homography_core PUBLIC ${OpenCV_LIBS})

add_executable(homography_tool main.cpp)
target_link_libraries(homography_tool PRIVATE homography_core)
target_compile_definitions(homography_tool PRIVATE HOMOGRAPHY_TOOL_VERSION="1")

if(BUILD_TESTING)
    add_executable(homography_unit_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/unit_tests.cpp
    )
    target_link_libraries(homography_unit_tests PRIVATE homography_core)
    add_test(NAME homography_unit_tests COMMAND homography_unit_tests)
endif()
