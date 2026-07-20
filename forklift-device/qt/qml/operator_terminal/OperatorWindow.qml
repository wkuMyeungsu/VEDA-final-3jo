import QtQuick
import QtQuick.Controls
import QtQuick.Window
import Safety.Common

// Root window for the Raspberry Pi driver terminal: fullscreen video with
// a large risk HUD, edge highlighting, and status strip. Small text and
// menus are deliberately avoided -- everything here must be readable at a
// glance. The demo panel is reachable only via Ctrl+Shift+D.
ApplicationWindow {
    id: window
    visible: true
    visibility: Window.FullScreen
    width: 1024
    height: 600
    color: Theme.colorBackground
    title: qsTr("Forklift Safety - Operator Terminal")

    Shortcut {
        sequence: "Ctrl+Shift+D"
        enabled: demoController.demoModeEnabled
        onActivated: demoPanel.visible = !demoPanel.visible
    }

    // Escape toggles fullscreen so the demo can be inspected in a window
    // on a desktop machine; irrelevant on a kiosk-mode Raspberry Pi.
    Shortcut {
        sequence: "Escape"
        onActivated: window.visibility = (window.visibility === Window.FullScreen) ? Window.Windowed
                                                                                     : Window.FullScreen
    }

    CameraVideoView {
        id: video
        anchors.fill: parent
        cameraId: activeCamera.activeCameraId
        personBBox: activeCamera.personBBox
        forkliftBBox: activeCamera.forkliftBBox
        distanceM: activeCamera.distanceM
    }

    EdgeWarningFrame {
        anchors.fill: parent
        riskLevel: activeCamera.riskLevel
    }

    TopBar {
        id: topBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 64
    }

    RiskHud {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: topBar.bottom
        anchors.topMargin: Theme.spacingLg
        riskLevel: activeCamera.riskLevel
        exceptionState: activeCamera.exceptionState
        distanceM: activeCamera.distanceM
    }

    StatusStrip {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacingLg
    }

    DemoPanel {
        id: demoPanel
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: 340
        visible: false
    }
}
