cmake_minimum_required(VERSION 3.15)
project(sunapi_onvif_parser)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    add_compile_options(/utf-8)
endif()

find_package(PkgConfig REQUIRED)

pkg_check_modules(GST REQUIRED IMPORTED_TARGET
    gstreamer-1.0
    gstreamer-app-1.0
)

# ---- pugixml: Windows(vcpkg) vs Linux(apt) 분기 ----
if (WIN32)
    find_package(pugixml CONFIG REQUIRED)
    set(PUGIXML_TARGET pugixml::pugixml)
else()
    pkg_check_modules(PUGIXML REQUIRED IMPORTED_TARGET pugixml)
    set(PUGIXML_TARGET PkgConfig::PUGIXML)
endif()

add_executable(parser_test
    onvif_metadata_parser.cpp
    test_main.cpp
)
target_link_libraries(parser_test PRIVATE ${PUGIXML_TARGET})

add_executable(rtp_metadata_receiver
    onvif_metadata_parser.cpp
    rtp_metadata_receiver.cpp
    csv_logger.cpp
    metadata_router.cpp
    ../aruco_detection/aruco_metadata_parser.cpp
    ../aruco_detection/aruco_csv_logger.cpp
    main.cpp
)
target_link_libraries(rtp_metadata_receiver PRIVATE
    ${PUGIXML_TARGET}
    PkgConfig::GST
)

add_executable(metadata_router_test
    metadata_router.cpp
    test_metadata_router.cpp
)
target_link_libraries(metadata_router_test PRIVATE ${PUGIXML_TARGET})

enable_testing()
add_test(NAME metadata_router_test COMMAND metadata_router_test)
