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
        network/IWarningDevice.h
        network/NoopWarningDevice.h
        network/NoopWarningDevice.cpp

        config/ConfigTypes.h
        config/ConfigLoader.h
        config/ConfigLoader.cpp

        services/ServerConnectionService.h
        services/ServerConnectionService.cpp
        services/MetadataService.h
        services/MetadataService.cpp
        services/CameraListModel.h
        services/CameraListModel.cpp
        services/EventLogModel.h
        services/EventLogModel.cpp
        services/AlertListModel.h
        services/AlertListModel.cpp
        services/DemoController.h
        services/DemoController.cpp
)

target_link_libraries(safety_common PRIVATE Qt6::QuickControls2 PUBLIC PkgConfig::GST)
