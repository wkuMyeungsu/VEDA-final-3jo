qt_add_executable(control_center
    WIN32
    main.cpp
)

target_link_libraries(control_center PRIVATE
    safety_common
    safety_commonplugin
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)

set(CONTROL_CENTER_QML_FILES
    ${CMAKE_SOURCE_DIR}/qml/control_center/ControlCenterWindow.qml
    ${CMAKE_SOURCE_DIR}/qml/control_center/AppBar.qml
    ${CMAKE_SOURCE_DIR}/qml/control_center/CameraGrid.qml
    ${CMAKE_SOURCE_DIR}/qml/control_center/CameraCard.qml
    ${CMAKE_SOURCE_DIR}/qml/control_center/HoverOverlay.qml
    ${CMAKE_SOURCE_DIR}/qml/control_center/ExpandedCameraView.qml
    ${CMAKE_SOURCE_DIR}/qml/control_center/RightPanel.qml
    ${CMAKE_SOURCE_DIR}/qml/control_center/AlertListView.qml
    ${CMAKE_SOURCE_DIR}/qml/control_center/CameraStatusList.qml
    ${CMAKE_SOURCE_DIR}/qml/control_center/EventLogPanel.qml
    ${CMAKE_SOURCE_DIR}/qml/control_center/DemoPanel.qml
    ${CMAKE_SOURCE_DIR}/qml/control_center/LoginOverlay.qml
    ${CMAKE_SOURCE_DIR}/qml/control_center/ZoneHierarchyView.qml
    ${CMAKE_SOURCE_DIR}/qml/control_center/ZoneListView.qml
    ${CMAKE_SOURCE_DIR}/qml/control_center/CameraOverviewView.qml
    ${CMAKE_SOURCE_DIR}/qml/control_center/FocusLayoutView.qml
)
foreach(qml_file ${CONTROL_CENTER_QML_FILES})
    get_filename_component(qml_alias ${qml_file} NAME)
    set_source_files_properties(${qml_file} PROPERTIES QT_RESOURCE_ALIAS ${qml_alias})
endforeach()

qt_add_qml_module(control_center
    URI Safety.ControlCenter
    VERSION 1.0
    # See common/CMakeLists.txt for why NO_CACHEGEN is needed here (QML
    # files live under the top-level qml/ dir, not next to this target).
    NO_CACHEGEN
    QML_FILES
        ${CONTROL_CENTER_QML_FILES}
)

# Runtime config (cameras.json etc.) is read from the filesystem, not
# baked into a resource, so it can be edited without rebuilding. Keep a
# copy next to the executable.
add_custom_command(TARGET control_center POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${SAFETY_CONFIG_SOURCE_DIR}
        $<TARGET_FILE_DIR:control_center>/config
)

# - mosquitto 의존 DLL들을 빌드 후 실행 파일 옆에 복사 (없으면 창 없이 조용히 종료)
if(WIN32 AND MOSQUITTO_RUNTIME_DLL)
    add_custom_command(TARGET control_center POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${MOSQUITTO_RUNTIME_DLL}
            $<TARGET_FILE_DIR:control_center>
    )
endif()
if(WIN32 AND MOSQUITTO_RUNTIME_EXTRA_DLLS)
    add_custom_command(TARGET control_center POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${MOSQUITTO_RUNTIME_EXTRA_DLLS}
            $<TARGET_FILE_DIR:control_center>
    )
endif()

# - Qt DLL은 복사 대신 PATH로 참조 (windeployqt 복사본은 이 PC에서 불안정했음)
# - 탐색기 더블클릭용 래퍼: Qt/GStreamer 경로를 PATH에 얹어서 실행
if(WIN32)
    file(GENERATE OUTPUT $<TARGET_FILE_DIR:control_center>/run_control_center.bat
        CONTENT "@echo off\r\nset \"PATH=C:\\Qt\\Tools\\mingw1310_64\\bin;${QT6_INSTALL_PREFIX}/bin;C:\\Program Files\\gstreamer\\1.0\\mingw_x86_64\\bin;C:\\Program Files\\mosquitto;%PATH%\"\r\nset \"QML_IMPORT_PATH=%~dp0qml;%~dp0..;%QML_IMPORT_PATH%\"\r\ncd /d \"%~dp0\"\r\nstart \"\" \"%~dp0control_center.exe\" %*\r\n"
    )
endif()
