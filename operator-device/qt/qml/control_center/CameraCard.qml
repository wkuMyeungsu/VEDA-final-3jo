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

    readonly property bool isAlert: riskLevel !== 0 || exceptionState !== 0

    color: Theme.colorSurface
    radius: Theme.radiusMd
    border.width: Theme.borderWidthHairline
    border.color: root.isAlert ? Theme.alertBorderColor(riskLevel, exceptionState) : Theme.colorBorder

    Behavior on border.color { ColorAnimation { duration: Theme.animationNormal } }

    Item {
        id: videoArea
        anchors.fill: parent
        anchors.margins: 1
        clip: true

        Rectangle {
            id: videoWell
            anchors.fill: parent
            radius: Theme.radiusMd - 1
            color: Theme.colorSurfaceSunken
        }

        CameraVideoView {
            anchors.fill: parent
            cameraId: root.cameraId
            distanceM: root.distanceM
            distanceValid: root.distanceValid
            riskLevel: root.riskLevel
        }

        // 상단 반투명 헤더 바 (카메라 정보 + 알약 뱃지)
        Rectangle {
            id: topFloatingBar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: Theme.cameraCardChromeHeight
            color: Qt.rgba(0.04, 0.06, 0.10, 0.72)

            Row {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacingSm
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingXs

                Rectangle {
                    width: 6
                    height: 6
                    radius: 3
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.connectionColor(root.videoConnectionState)
                }

                Text {
                    text: root.cameraName.length > 0 ? root.cameraName : root.cameraId
                    color: Theme.colorTextPrimary
                    font.pixelSize: Theme.typeCaption.size
                    font.weight: Font.Medium
                }

                Text {
                    text: "· " + root.zone
                    color: Theme.colorTextMuted
                    font.pixelSize: Theme.typeCaption.size
                }
            }

            RiskBanner {
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingSm
                anchors.verticalCenter: parent.verticalCenter
                visible: root.isAlert
                riskLevel: root.riskLevel
                exceptionState: root.exceptionState
            }
        }

        // 하단 실시간 거리 오버레이 (위험 발생 시 또는 거리 유효 시)
        Rectangle {
            id: bottomFloatingBar
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: Theme.cameraCardFooterHeight
            color: Qt.rgba(0.04, 0.06, 0.10, 0.65)
            visible: root.isAlert || root.distanceValid

            Row {
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingSm
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingXs

                Text {
                    text: "접근 거리:"
                    color: Theme.colorTextMuted
                    font.pixelSize: Theme.typeCaption.size - 1
                }
                Text {
                    text: root.distanceValid ? root.distanceM.toFixed(2) + " m" : "측정 불가"
                    color: root.isAlert ? Theme.riskColor(root.riskLevel) : Theme.colorTextPrimary
                    font.pixelSize: Theme.typeCaption.size
                    font.weight: Font.Medium
                }
            }
        }

        HoverOverlay {
            anchors.fill: parent
            overlayRadius: Theme.radiusMd
            onClicked: root.clicked()
        }
    }
}
