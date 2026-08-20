qt_add_executable(operator_terminal
    WIN32
    main.cpp
    ActiveCameraController.h
    ActiveCameraController.cpp
    OperatorDemoController.h
    OperatorDemoController.cpp
)

target_link_libraries(operator_terminal PRIVATE
    safety_common
    safety_commonplugin
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)

set(OPERATOR_TERMINAL_QML_FILES
    ${CMAKE_SOURCE_DIR}/qml/operator_terminal/OperatorWindow.qml
    ${CMAKE_SOURCE_DIR}/qml/operator_terminal/TopBar.qml
    ${CMAKE_SOURCE_DIR}/qml/operator_terminal/StatusStrip.qml
    ${CMAKE_SOURCE_DIR}/qml/operator_terminal/RiskHud.qml
    ${CMAKE_SOURCE_DIR}/qml/operator_terminal/EdgeWarningFrame.qml
    ${CMAKE_SOURCE_DIR}/qml/operator_terminal/EstopOverlay.qml
    ${CMAKE_SOURCE_DIR}/qml/operator_terminal/DemoPanel.qml
)
foreach(qml_file ${OPERATOR_TERMINAL_QML_FILES})
    get_filename_component(qml_alias ${qml_file} NAME)
    set_source_files_properties(${qml_file} PROPERTIES QT_RESOURCE_ALIAS ${qml_alias})
endforeach()

qt_add_qml_module(operator_terminal
    URI Safety.OperatorTerminal
    VERSION 1.0
    # See common/CMakeLists.txt for why NO_CACHEGEN is needed here (QML
    # files live under the top-level qml/ dir, not next to this target).
    NO_CACHEGEN
    QML_FILES
        ${OPERATOR_TERMINAL_QML_FILES}
)

add_custom_command(TARGET operator_terminal POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${SAFETY_CONFIG_SOURCE_DIR}
        $<TARGET_FILE_DIR:operator_terminal>/config
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/fonts
        $<TARGET_FILE_DIR:operator_terminal>/fonts
)
