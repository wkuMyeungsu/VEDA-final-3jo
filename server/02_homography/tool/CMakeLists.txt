cmake_minimum_required(VERSION 3.15)
project(homography_tool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV REQUIRED COMPONENTS core imgproc calib3d aruco highgui imgcodecs)

add_executable(homography_tool homography_tool.cpp)
target_include_directories(homography_tool PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_link_libraries(homography_tool PRIVATE ${OpenCV_LIBS})
target_compile_definitions(homography_tool PRIVATE HOMOGRAPHY_TOOL_VERSION="1")
