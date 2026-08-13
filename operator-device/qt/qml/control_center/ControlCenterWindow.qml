import QtQuick
import QtQuick.Controls
import Safety.Common

// Root window: app bar, camera grid / expanded view, right status panel,
// bottom event log, plus a demo panel reachable only via Ctrl+Shift+D and
// only when the app was launched with --demo.
ApplicationWindow {
    id: window
    visible: true
    width: 1440
    height: 900
    minimumWidth: 1000
    minimumHeight: 680
    title: systemName
    color: Theme.colorBackground

    property string selectedCameraId: ""

    Shortcut {
        sequence: "Ctrl+Shift+D"
        enabled: demoController.demoModeEnabled
        onActivated: demoPanel.visible = !demoPanel.visible
    }

    AppBar {
        id: appBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.appBarHeight
    }

    Item {
        id: mainArea
        anchors.top: appBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: eventLogPanel.top
        anchors.margins: Theme.spacingMd

        CameraGrid {
            id: cameraGrid
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: rightPanel.left
            anchors.rightMargin: Theme.spacingMd
            visible: window.selectedCameraId === ""
            onCameraSelected: (cameraId) => window.selectedCameraId = cameraId
        }

        ExpandedCameraView {
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: rightPanel.left
            anchors.rightMargin: Theme.spacingMd
            visible: window.selectedCameraId !== ""
            cameraId: window.selectedCameraId
            onBackRequested: window.selectedCameraId = ""
        }

        RightPanel {
            id: rightPanel
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            width: Theme.rightPanelWidth
            onCameraFocusRequested: (cameraId) => window.selectedCameraId = cameraId
        }
    }

    EventLogPanel {
        id: eventLogPanel
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Theme.eventLogHeight
    }

    DemoPanel {
        id: demoPanel
        anchors.top: appBar.bottom
        anchors.bottom: eventLogPanel.top
        anchors.right: parent.right
        width: 320
        visible: false
    }
}
