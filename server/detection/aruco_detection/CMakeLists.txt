# aruco_detection / CMakeLists.txt
#
# ArUco MarkerDetected 메타데이터 파서.
# 이 폴더만의 독립 빌드 설정. 다른 폴더 CMake 는 건드리지 않는다.
#
# 순수 파싱 로직 + CSV 로거 + 단위 테스트만 빌드한다 (RTSP/GStreamer 의존 없음).
cmake_minimum_required(VERSION 3.15)
project(aruco_metadata_parser CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC 한글 소스 인코딩 (주석 깨짐 방지)
if (MSVC)
    add_compile_options(/utf-8)
endif()

# ---- pugixml: Windows(vcpkg) vs Linux(apt) 분기 ----
if (WIN32)
    find_package(pugixml CONFIG REQUIRED)
    set(PUGIXML_TARGET pugixml::pugixml)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PUGIXML REQUIRED IMPORTED_TARGET pugixml)
    set(PUGIXML_TARGET PkgConfig::PUGIXML)
endif()

add_executable(aruco_parser_test
    aruco_metadata_parser.cpp
    aruco_csv_logger.cpp
    test_aruco_parser.cpp
)
target_link_libraries(aruco_parser_test PRIVATE ${PUGIXML_TARGET})

enable_testing()
add_test(NAME aruco_parser_test COMMAND aruco_parser_test)
