import QtQuick
import Safety.Common

// Per-camera video connection status, for the right panel.
// 행 클릭 시 AlertListView와 동일하게 그 카메라를 메인 화면에 띄움.
Item {
    id: root
    signal cameraFocusRequested(string cameraId)

    ListView {
        anchors.fill: parent
        clip: true
        spacing: Theme.spacingSm
        model: cameraListModel

        delegate: Item {
            width: ListView.view.width
            height: Theme.statusRowHeight

            Column {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingXxs

                Text {
                    text: model.name + " (" + model.cameraId + ")"
                    color: Theme.colorTextPrimary
                    font.pixelSize: Theme.typeCaption.size
                    font.bold: true
                }

                ConnectionIndicator {
                    label: "영상"
                    state: model.videoConnectionState
                }
            }

            HoverOverlay {
                anchors.fill: parent
                overlayRadius: Theme.radiusSm
                onClicked: root.cameraFocusRequested(model.cameraId)
            }
        }
    }
}
