import QtQuick
import Safety.Common

// Responsive camera grid: column count grows with available width so the
// layout scales from 2x2 up to 5+ cameras without any code changes.
Item {
    id: root
    signal cameraSelected(string cameraId)

    readonly property int minCardWidth: 320
    readonly property int columns: Math.max(2, Math.floor(width / minCardWidth))

    GridView {
        id: gridView
        anchors.fill: parent
        clip: true
        cellWidth: root.width / root.columns
        cellHeight: cellWidth * 9 / 16 + 40
        model: cameraListModel

        delegate: Item {
            width: gridView.cellWidth
            height: gridView.cellHeight

            CameraCard {
                anchors.fill: parent
                anchors.margins: Theme.spacingSm
                cameraId: model.cameraId
                cameraName: model.name
                zone: model.zone
                riskLevel: model.riskLevel
                exceptionState: model.exceptionState
                distanceM: model.distanceM
                videoConnectionState: model.videoConnectionState
                onClicked: root.cameraSelected(model.cameraId)
            }
        }
    }
}
