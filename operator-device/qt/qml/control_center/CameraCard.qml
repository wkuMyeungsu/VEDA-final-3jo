import QtQuick
import Safety.Common

// 관제 그리드의 카드 1장 = 영상 + 오버레이 + (위험할 때만 뜨는) 위험 배너 +
// (위험할 때 강조되는) 테두리 + 하단 이름/구역/연결상태 줄.
// 영상 아무 데나 클릭하면 확대 화면(ExpandedCameraView)으로 전환됨 (clicked 신호)
Rectangle {
    id: root
    property string cameraId: ""
    property string cameraName: ""
    property string zone: ""
    property int riskLevel: 0
    property int exceptionState: 0
    property real distanceM: 0
    property bool distanceValid: true
    property int videoConnectionState: 0

    signal clicked()

    // riskLevel !== 0(=Safe 아님) 이거나 예외 상태가 있으면 "경보 중"으로 취급
    // -> 배너 표시 여부/테두리 굵기·색상이 전부 이 값 하나로 결정됨
    readonly property bool isAlert: riskLevel !== 0 || exceptionState !== 0

    color: Theme.colorSurface
    radius: Theme.radiusMd
    border.width: isAlert ? 2 : 1
    // exceptionState가 있으면 riskLevel은 로컬 기본값(Safe)일 수 있어 신뢰 불가 -> 초록 대신 unknown색
    border.color: exceptionState !== 0 ? Theme.colorUnknown
                  : (isAlert ? Theme.riskColor(riskLevel) : Theme.colorBorder)

    // 테두리 색이 바뀔 때 뚝 끊기지 않고 부드럽게 전환 (예: SAFE -> DANGER)
    Behavior on border.color { ColorAnimation { duration: Theme.animationNormal } }

    RiskBanner {
        id: banner
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingSm
        visible: root.isAlert
        riskLevel: root.riskLevel
        exceptionState: root.exceptionState
    }

    Item {
        id: videoArea
        anchors.top: root.isAlert ? banner.bottom : parent.top
        anchors.topMargin: Theme.spacingSm
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: headerRow.top
        anchors.bottomMargin: Theme.spacingXs
        anchors.leftMargin: Theme.spacingSm
        anchors.rightMargin: Theme.spacingSm
        clip: true

        CameraVideoView {
            anchors.fill: parent
            cameraId: root.cameraId
            distanceM: root.distanceM
            distanceValid: root.distanceValid
            riskLevel: root.riskLevel
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.clicked()
        }
    }

    Row {
        id: headerRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacingSm
        height: 18
        spacing: Theme.spacingSm

        Text {
            text: root.cameraName + " (" + root.cameraId + ")"
            color: Theme.colorTextPrimary
            font.pixelSize: Theme.fontSizeSm
            font.bold: true
            elide: Text.ElideRight
            width: 150
        }
        Text {
            text: root.zone
            color: Theme.colorTextSecondary
            font.pixelSize: Theme.fontSizeSm
        }
        Text {
            text: Theme.connectionLabel(root.videoConnectionState)
            color: Theme.connectionColor(root.videoConnectionState)
            font.pixelSize: Theme.fontSizeSm
        }
    }
}
