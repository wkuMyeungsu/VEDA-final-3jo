import QtQuick
import Safety.Common

// Server / camera / sensor connection status, large and touch-friendly,
// anchored to a corner over the video.
Rectangle {
    id: root
    width: 240
    height: contentCol.implicitHeight + Theme.spacingMd * 2
    radius: Theme.radiusMd
    color: Qt.rgba(Theme.colorSurface.r, Theme.colorSurface.g, Theme.colorSurface.b, 0.82)
    border.color: Theme.colorBorder
    border.width: 1

    Column {
        id: contentCol
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingSm

        ConnectionIndicator {
            label: "서버"
            state: serverConnection.connectionState
        }
        ConnectionIndicator {
            label: "카메라"
            state: activeCamera.videoConnectionState
        }
        ConnectionIndicator {
            label: "센서"
            // No dedicated sensor channel yet: sensor health rides on the
            // metadata pipeline, degraded explicitly by a SENSOR_FAULT
            // exception.
            state: activeCamera.exceptionState === 1 ? 0 : metadataDistributor.connectionState
        }
    }
}
