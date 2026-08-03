import QtQuick
import Safety.Common

// 현재 CAUTION 이상인 카메라만 한 줄씩 보여주는 경보 목록.
// 행을 클릭하면 그 카메라를 메인 화면(ExpandedCameraView)에 띄움.
// model이 alertListModel(C++ AlertListModel)이라, 카메라가 SAFE로 돌아오면
// C++ 쪽에서 자동으로 행이 사라짐 -- 이 QML은 그냥 지금 있는 걸 그릴 뿐
Item {
    id: root
    signal cameraFocusRequested(string cameraId)

    ListView {
        id: listView
        anchors.fill: parent
        clip: true
        spacing: Theme.spacingXs
        model: alertListModel

        // model.xxx는 AlertListModel::roleNames()에 등록된 이름들
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
                    text: Theme.riskLabel(model.riskLevel) + " · "
                          + (model.distanceValid ? model.distanceM.toFixed(2) + " m" : "측정 불가")
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
