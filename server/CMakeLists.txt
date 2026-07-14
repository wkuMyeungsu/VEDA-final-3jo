cmake_minimum_required(VERSION 3.15)
project(sunapi_onvif_parser)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC는 기본적으로 소스 파일이 UTF-8이어도, 컴파일된 문자열 리터럴을
# 로컬 시스템 ANSI 코드페이지(한글 Windows면 CP949)로 변환해버림.
# 그래서 소스는 UTF-8인데 실행 파일 안의 문자열 바이트는 UTF-8이 아니게 되고,
# 콘솔 출력 쪽을 아무리 고쳐도(SetConsoleOutputCP, WriteConsoleW 등) 항상 깨지는
# 원인이 됨. /utf-8 플래그로 소스/실행 문자셋을 둘 다 UTF-8로 고정.
if (MSVC)
    add_compile_options(/utf-8)
endif()

find_package(pugixml CONFIG REQUIRED)

# GStreamer는 vcpkg가 아니라 별도 설치본이라 pkg-config로 찾음.
# 빌드 전에 PKG_CONFIG_PATH 환경변수가 GStreamer의 pkgconfig 폴더를 가리키고 있어야 함.
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET
    gstreamer-1.0
    gstreamer-app-1.0
)

# 기존 파서 검증용 실행 파일 (mock 데이터 테스트, 그대로 유지)
add_executable(parser_test
    onvif_metadata_parser.cpp
    test_main.cpp
)
target_link_libraries(parser_test PRIVATE pugixml::pugixml)

# 실제 카메라 RTSP 연결 + 재조립 + 파싱까지 하는 실행 파일 (새로 추가)
add_executable(rtp_metadata_receiver
    onvif_metadata_parser.cpp
    rtp_metadata_receiver.cpp
    csv_logger.cpp
    main.cpp
)
target_link_libraries(rtp_metadata_receiver PRIVATE
    pugixml::pugixml
    PkgConfig::GST
)