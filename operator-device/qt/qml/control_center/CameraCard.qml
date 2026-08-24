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
            color: Qt.rgba(0.04, 0.06, 0.10, 0.85)

            RiskBanner {
                id: riskBanner
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingSm
                anchors.verticalCenter: parent.verticalCenter
                visible: root.isAlert
                riskLevel: root.riskLevel
                exceptionState: root.exceptionState
            }

            Item {
                id: topInfoArea
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacingSm
                anchors.right: riskBanner.visible ? riskBanner.left : parent.right
                anchors.rightMargin: Theme.spacingSm
                anchors.verticalCenter: parent.verticalCenter
                height: 22

                Row {
                    id: fixedPrefixRow
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6

                    // 연결 상태 표시 점
                    Rectangle {
                        width: 7
                        height: 7
                        radius: 3.5
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.connectionColor(root.videoConnectionState)
                    }

                    // 1단계: 구역 뱃지 (Zone)
                    Rectangle {
                        implicitWidth: zoneText.implicitWidth + 10
                        height: 22
                        radius: 4
                        color: Qt.rgba(1, 1, 1, 0.12)
                        anchors.verticalCenter: parent.verticalCenter

                        Text {
                            id: zoneText
                            anchors.centerIn: parent
                            text: root.zone.length > 0 ? root.zone : "ZONE"
                            color: Theme.colorTextSecondary
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                    }

                    // 구분자
                    Text {
                        text: "›"
                        color: Theme.colorTextMuted
                        font.pixelSize: 13
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                // 2단계 & 3단계: 카메라 & 채널 명칭 (폭 초과 시 깔끔한 말줄임 ...)
                Text {
                    id: cameraNameText
                    anchors.left: fixedPrefixRow.right
                    anchors.leftMargin: 6
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.cameraName.length > 0 ? root.cameraName : root.cameraId
                    color: Theme.colorTextPrimary
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
            }
        }

        // 하단 실시간 거리 오버레이: 이상/위험(Caution/Danger/Emergency/장애) 발생 시에만 팝업
        Rectangle {
            id: bottomFloatingBar
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: Theme.cameraCardFooterHeight
            color: Qt.rgba(0.04, 0.06, 0.10, 0.85)
            visible: root.isAlert
            opacity: root.isAlert ? 1.0 : 0.0

            Behavior on opacity { NumberAnimation { duration: 180 } }

            Row {
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingSm
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingXs

                Text {
                    text: "접근 거리:"
                    color: Theme.colorTextMuted
                    font.pixelSize: 12
                }
                Text {
                    text: root.distanceValid ? Math.round(root.distanceM * 1000) + " mm" : "측정 불가"
                    color: Theme.riskColor(root.riskLevel)
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
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
