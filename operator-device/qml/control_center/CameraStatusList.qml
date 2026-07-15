import QtQuick
import Safety.Common

// Per-camera video connection status, for the right panel.
Item {
    id: root

    ListView {
        anchors.fill: parent
        clip: true
        spacing: Theme.spacingSm
        model: cameraListModel

        delegate: Item {
            width: ListView.view.width
            height: 44

            Column {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2

                Text {
                    text: model.name + " (" + model.cameraId + ")"
                    color: Theme.colorTextPrimary
                    font.pixelSize: Theme.fontSizeSm
                    font.bold: true
                }

                ConnectionIndicator {
                    label: "영상"
                    state: model.videoConnectionState
                }
            }
        }
    }
}
