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
    ${CMAKE_SOURCE_DIR}/operator-device/qml/control_center/ControlCenterWindow.qml
    ${CMAKE_SOURCE_DIR}/operator-device/qml/control_center/AppBar.qml
    ${CMAKE_SOURCE_DIR}/operator-device/qml/control_center/CameraGrid.qml
    ${CMAKE_SOURCE_DIR}/operator-device/qml/control_center/CameraCard.qml
    ${CMAKE_SOURCE_DIR}/operator-device/qml/control_center/ExpandedCameraView.qml
    ${CMAKE_SOURCE_DIR}/operator-device/qml/control_center/RightPanel.qml
    ${CMAKE_SOURCE_DIR}/operator-device/qml/control_center/AlertListView.qml
    ${CMAKE_SOURCE_DIR}/operator-device/qml/control_center/CameraStatusList.qml
    ${CMAKE_SOURCE_DIR}/operator-device/qml/control_center/EventLogPanel.qml
    ${CMAKE_SOURCE_DIR}/operator-device/qml/control_center/DemoPanel.qml
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
