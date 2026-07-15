import QtQuick
import Safety.Common

// Rows for every camera currently at CAUTION or above. Clicking a row
// expands that camera in the main area.
Item {
    id: root
    signal cameraFocusRequested(string cameraId)

    ListView {
        id: listView
        anchors.fill: parent
        clip: true
        spacing: Theme.spacingXs
        model: alertListModel

        delegate: Rectangle {
            width: ListView.view.width
            height: 52
            radius: Theme.radiusSm
            color: Theme.riskBgColor(model.riskLevel)
            border.color: Theme.riskColor(model.riskLevel)
            border.width: 1

            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: Theme.spacingSm
                spacing: 2

                Text {
                    text: model.name + " (" + model.cameraId + ") · " + model.zone
                    color: Theme.colorTextPrimary
                    font.pixelSize: Theme.fontSizeSm
                    font.bold: true
                    elide: Text.ElideRight
                    width: parent.width
                }
                Text {
                    text: Theme.riskLabel(model.riskLevel) + " · " + model.distanceM.toFixed(2) + " m"
                          + (model.exceptionState !== 0 ? " · " + Theme.exceptionLabel(model.exceptionState) : "")
                    color: Theme.riskColor(model.riskLevel)
                    font.pixelSize: Theme.fontSizeSm
                    elide: Text.ElideRight
                    width: parent.width
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.cameraFocusRequested(model.cameraId)
            }
        }
    }

    Text {
        anchors.centerIn: parent
        visible: listView.count === 0
        text: "현재 경보 없음"
        color: Theme.colorTextMuted
        font.pixelSize: Theme.fontSizeSm
    }
}
