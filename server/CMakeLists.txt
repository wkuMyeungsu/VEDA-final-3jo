cmake_minimum_required(VERSION 3.15)
project(veda_server LANGUAGES CXX)

include(CTest)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/utf-8)
endif()

find_package(Python3 COMPONENTS Interpreter)
add_subdirectory(apps/main_app)
if(Python3_Interpreter_FOUND)
    add_subdirectory(apps/homography_app/tests)
    add_subdirectory(apps/monitoring_app/tests)
endif()

# homography processing의 C++ 단위 테스트. OpenCV가 없는 환경에서도 서버 빌드는
# 계속되도록 QUIET으로 찾고, 있을 때만 하위 프로젝트(자체 find_package REQUIRED)를 붙인다.
find_package(OpenCV QUIET COMPONENTS core imgproc calib3d aruco imgcodecs)
if(OpenCV_FOUND)
    add_subdirectory(apps/homography_app/processing)
endif()
