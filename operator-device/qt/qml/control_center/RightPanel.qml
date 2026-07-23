import QtQuick
import Safety.Common

// Right side column: active alerts on top, per-camera connection status
// below.
Item {
    id: root
    signal cameraFocusRequested(string cameraId)

    Column {
        anchors.fill: parent
        spacing: Theme.spacingMd

        SectionPanel {
            title: "현재 경보"
            width: parent.width
            height: parent.height * 0.55

            AlertListView {
                anchors.fill: parent
                onCameraFocusRequested: (cameraId) => root.cameraFocusRequested(cameraId)
            }
        }

        SectionPanel {
            title: "카메라 상태"
            width: parent.width
            height: parent.height * 0.45 - Theme.spacingMd

            CameraStatusList {
                anchors.fill: parent
            }
        }
    }
}
