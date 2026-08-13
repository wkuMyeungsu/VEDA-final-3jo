# Shared C++ backend (models/services/video/network/config) plus the shared
# QML theme and component library, both exposed to the app targets as a
# single "Safety.Common" QML module + safety_common link target.

# RtspVideoSource가 실제 GStreamer 파이프라인을 쓰므로 링크 필요. server/detection과
# 동일한 pkg-config 패턴 사용 (PKG_CONFIG_PATH가 GStreamer 설치본의 pkgconfig
# 폴더를 가리키고 있어야 함).
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET
    gstreamer-1.0
    gstreamer-app-1.0
)

# RiskEventSource가 MQTT(libmosquitto)로 위험 판정 데이터를 구독하므로 링크 필요.
# 리눅스는 pkg-config로 바로 잡히지만 mosquitto 공식 Windows 설치본에는 .pc 파일이
# 없으므로, pkg-config가 실패하면 헤더/라이브러리를 직접 찾는 경로를 함께 둔다.
pkg_check_modules(MOSQUITTO IMPORTED_TARGET libmosquitto)

add_library(mosquitto_dep INTERFACE)                                       # - MQTT 의존성 창구: 찾는 방법이 달라도 사용하는 쪽은 이 이름만 씀

if(MOSQUITTO_FOUND)
    target_link_libraries(mosquitto_dep INTERFACE PkgConfig::MOSQUITTO)    # - pkg-config로 찾은 경우: 헤더/라이브러리 경로가 이미 다 들어있음
else()
    set(MOSQUITTO_ROOT "$ENV{MOSQUITTO_ROOT}" CACHE PATH "libmosquitto 설치 경로 (mosquitto.h가 있는 폴더의 상위)")

    find_path(MOSQUITTO_INCLUDE_DIR mosquitto.h                            # - 헤더 탐색: mosquitto.h 위치 찾기
        HINTS ${MOSQUITTO_ROOT} "C:/Program Files/mosquitto" "C:/mosquitto"
        PATH_SUFFIXES devel include
    )
    find_library(MOSQUITTO_LIBRARY                                         # - 라이브러리 탐색: import 라이브러리 우선
        NAMES mosquitto libmosquitto mosquitto.lib
        HINTS ${MOSQUITTO_ROOT} "C:/Program Files/mosquitto" "C:/mosquitto"
        PATH_SUFFIXES devel lib
    )
    if(WIN32 AND NOT MOSQUITTO_LIBRARY)
        # MinGW는 DLL에 직접 링크할 수 있으므로, MSVC용 .lib이 안 잡히면 DLL을 그대로 쓴다.
        find_file(MOSQUITTO_LIBRARY NAMES mosquitto.dll
            HINTS ${MOSQUITTO_ROOT} "C:/Program Files/mosquitto" "C:/mosquitto"
            PATH_SUFFIXES "" bin
        )
    endif()

    if(NOT MOSQUITTO_INCLUDE_DIR OR NOT MOSQUITTO_LIBRARY)
        message(FATAL_ERROR
            "libmosquitto를 찾지 못했습니다 (RiskEventSource의 MQTT 수신에 필요).\n"
            "  리눅스 : sudo apt install libmosquitto-dev\n"
            "  Windows: mosquitto 공식 설치본 설치 후 필요하면 경로를 직접 지정\n"
            "           cmake -DMOSQUITTO_ROOT=\"C:/Program Files/mosquitto\" ...\n"
            "  탐색 결과: include=${MOSQUITTO_INCLUDE_DIR} lib=${MOSQUITTO_LIBRARY}")
    endif()

    target_include_directories(mosquitto_dep INTERFACE ${MOSQUITTO_INCLUDE_DIR}) # - 헤더 경로 전달: #include <mosquitto.h> 해결용
    target_link_libraries(mosquitto_dep INTERFACE ${MOSQUITTO_LIBRARY})          # - 라이브러리 연결: mosquitto_* 함수 실체 연결
endif()

if(WIN32)
    # - 실행 파일 옆에 복사할 mosquitto 런타임 의존 DLL 목록 (mosquitto.dll, cjson.dll 등)
    find_file(MOSQUITTO_RUNTIME_DLL NAMES mosquitto.dll
        HINTS ${MOSQUITTO_ROOT} "C:/Program Files/mosquitto" "C:/mosquitto"
        PATH_SUFFIXES "" bin
    )
    set(MOSQUITTO_RUNTIME_EXTRA_DLLS "")
    foreach(dep_dll mosquitto_common.dll libssl-3-x64.dll libcrypto-3-x64.dll pthreadVC3.dll cjson.dll)
        find_file(MOSQUITTO_DEP_${dep_dll} NAMES ${dep_dll}
            HINTS ${MOSQUITTO_ROOT} "C:/Program Files/mosquitto" "C:/mosquitto"
            PATH_SUFFIXES "" bin
        )
        if(MOSQUITTO_DEP_${dep_dll})
            list(APPEND MOSQUITTO_RUNTIME_EXTRA_DLLS ${MOSQUITTO_DEP_${dep_dll}})
        endif()
    endforeach()
    # apps/control_center는 common의 형제 디렉터리라 일반 변수는 안 넘어간다 (add_subdirectory는
    # 스코프가 자식 쪽으로만 흐름) - INTERNAL 캐시로 만들어 프로젝트 전역에서 보이게 한다.
    set(MOSQUITTO_RUNTIME_EXTRA_DLLS "${MOSQUITTO_RUNTIME_EXTRA_DLLS}" CACHE INTERNAL "")
endif()

qt_add_library(safety_common STATIC)

target_include_directories(safety_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# qmltyperegistrar emits bare #include <ClassName.h> for every QML_ELEMENT /
# QML_VALUE_TYPE it finds, expecting each header to be reachable directly on
# the include path (not via its models/video/... subdirectory). Add every
# subdirectory so that generated file resolves without relocating headers.
target_include_directories(safety_common PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/models
    ${CMAKE_CURRENT_SOURCE_DIR}/video
    ${CMAKE_CURRENT_SOURCE_DIR}/network
    ${CMAKE_CURRENT_SOURCE_DIR}/config
    ${CMAKE_CURRENT_SOURCE_DIR}/services
)

target_link_libraries(safety_common
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::Multimedia
        Qt6::Network
)

# Shared QML files (qml/theme, qml/components) live one level up from this
# CMakeLists.txt. Alias each to its bare filename so they register flat
# under the "Safety.Common" module (import Safety.Common) instead of
# inheriting a resource path derived from the ".." on-disk location.
set(SAFETY_COMMON_QML_FILES
    ${CMAKE_SOURCE_DIR}/qml/theme/Theme.qml
    ${CMAKE_SOURCE_DIR}/qml/components/StatusBadge.qml
    ${CMAKE_SOURCE_DIR}/qml/components/RiskBanner.qml
    ${CMAKE_SOURCE_DIR}/qml/components/CameraVideoView.qml
    ${CMAKE_SOURCE_DIR}/qml/components/IconLabel.qml
    ${CMAKE_SOURCE_DIR}/qml/components/SectionPanel.qml
    ${CMAKE_SOURCE_DIR}/qml/components/ConnectionIndicator.qml
)
foreach(qml_file ${SAFETY_COMMON_QML_FILES})
    get_filename_component(qml_alias ${qml_file} NAME)
    set_source_files_properties(${qml_file} PROPERTIES QT_RESOURCE_ALIAS ${qml_alias})
endforeach()

# Theme.qml's "pragma Singleton" isn't auto-detected once the file has been
# relocated via QT_RESOURCE_ALIAS above, so qmldir generation would
# otherwise register it as a plain (non-singleton) type -- every
# Theme.colorX / Theme.someFunction() reference would then silently resolve
# to an empty object instead of the real singleton. Force it explicitly.
set_source_files_properties(${CMAKE_SOURCE_DIR}/qml/theme/Theme.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(safety_common
    URI Safety.Common
    VERSION 1.0
    # The shared QML files physically live in qml/theme and qml/components
    # (one level up from this CMakeLists.txt) rather than under common/, so
    # qmlcachegen's relative-path bookkeeping produces a ".." it can't turn
    # into a valid intermediate build path. QT_RESOURCE_ALIAS above already
    # gives every file a clean, flat resource path; NO_CACHEGEN just skips
    # ahead-of-time bytecode compilation (QML is still interpreted/JIT'd
    # normally at runtime).
    NO_CACHEGEN
    QML_FILES
        ${SAFETY_COMMON_QML_FILES}
    SOURCES
        models/Types.h
        models/Types.cpp
        models/BBox.h
        models/BBox.cpp
        models/RiskMetadata.h
        models/RiskMetadata.cpp
        models/CameraInfo.h
        models/CameraInfo.cpp

        video/AspectFit.h
        video/IVideoSource.h
        video/MockVideoSource.h
        video/MockVideoSource.cpp
        video/LocalFileVideoSource.h
        video/LocalFileVideoSource.cpp
        video/RtspVideoSource.h
        video/RtspVideoSource.cpp
        video/VideoSourceManager.h
        video/VideoSourceManager.cpp
        video/VideoStream.h
        video/VideoStream.cpp
        video/DetectionOverlay.h
        video/DetectionOverlay.cpp

        network/IMetadataSource.h
        network/MockMetadataSource.h
        network/MockMetadataSource.cpp
        network/RiskEventSource.h
        network/RiskEventSource.cpp

        config/ConfigTypes.h
        config/ConfigLoader.h
        config/ConfigLoader.cpp

        services/ServerConnectionService.h
        services/ServerConnectionService.cpp
        services/MetadataDistributor.h
        services/MetadataDistributor.cpp
        services/CameraListModel.h
        services/CameraListModel.cpp
        services/EventLogModel.h
        services/EventLogModel.cpp
        services/AlertListModel.h
        services/AlertListModel.cpp
        services/DemoController.h
        services/DemoController.cpp
)

target_link_libraries(safety_common PRIVATE Qt6::QuickControls2 PUBLIC PkgConfig::GST mosquitto_dep)
