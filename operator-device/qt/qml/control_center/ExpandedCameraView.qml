import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import Safety.Common

// 그리드 카드 클릭 시 뜨는 단일 카메라 확대 화면.
// CameraCard와 달리 CameraListModel을 안 거치고, metadataDistributor를 직접
// 구독해서(Connections) 이 카메라의 원본 RiskMetadata를 통째로 받아 씀.
// 값 채우는 경로가 2가지:
// 1) Component.onCompleted -- 화면이 뜨는 "그 순간" 마지막 값을 즉시 표시
//    (다음 이벤트까지 화면이 비어있는 걸 방지)
// 2) Connections/onMetadataUpdated -- 이후 실시간으로 값이 바뀔 때마다 갱신
Item {
    id: root
    property string cameraId: ""

    property real distanceM: 0
    property bool distanceValid: true
    property bbox personBBox
    property bbox forkliftBBox
    property int riskLevel: 0
    property int exceptionState: 0

    Connections {
        target: metadataDistributor
        function onMetadataUpdated(metadata) {
            // metadataUpdated는 "모든" 카메라 이벤트가 다 들어옴 -- 지금 보고 있는
            // 카메라(root.cameraId)의 이벤트가 아니면 무시
            if (metadata.cameraId !== root.cameraId)
                return
            root.distanceM = metadata.distanceM
            root.distanceValid = metadata.distanceValid
            root.personBBox = metadata.personBBox
            root.forkliftBBox = metadata.forkliftBBox
            root.riskLevel = metadata.riskLevel
            root.exceptionState = metadata.exceptionState
        }
    }

    Component.onCompleted: {
        const latest = metadataDistributor.latestFor(root.cameraId)
        root.distanceM = latest.distanceM
        root.distanceValid = latest.distanceValid
        root.personBBox = latest.personBBox
        root.forkliftBBox = latest.forkliftBBox
        root.riskLevel = latest.riskLevel
        root.exceptionState = latest.exceptionState
    }

    // 그리드에서 확대되며 뜨는 패널이라 부유감을 주는 그림자 적용 (허용된 2곳 중 하나)
    MultiEffect {
        source: card
        anchors.fill: card
        shadowEnabled: true
        shadowColor: "#000000"
        shadowOpacity: 0.35
        shadowBlur: 0.5
        shadowVerticalOffset: 6
    }

    Rectangle {
        id: card
        anchors.fill: parent
        color: Theme.colorSurface
        radius: Theme.radiusMd
        // CameraCard와 동일 규칙(Theme.alertBorderColor/Width) -- 한쪽만 고치는
        // 드리프트 방지, Behavior도 CameraCard와 동일하게 맞춤(굵기 변화가 색만큼
        // 부드럽게 안 가던 결함 수정)
        border.color: Theme.alertBorderColor(root.riskLevel, root.exceptionState)
        border.width: Theme.alertBorderWidth(root.riskLevel, root.exceptionState)

        Behavior on border.color { ColorAnimation { duration: Theme.animationNormal } }
        Behavior on border.width { NumberAnimation { duration: Theme.animationNormal } }

        Item {
            id: header
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: Theme.spacingMd
            height: Theme.iconButtonSize

            Rectangle {
                id: backButton
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.iconButtonSize
                height: Theme.iconButtonSize
                radius: Theme.radiusSm
                color: Theme.colorSurfaceElevated
                border.color: Theme.colorBorder
                border.width: Theme.borderWidthHairline

                Text {
                    anchors.centerIn: parent
                    text: "←"
                    color: Theme.colorTextPrimary
                    font.pixelSize: Theme.typeHeading.size
                }

                HoverOverlay {
                    anchors.fill: parent
                    overlayRadius: Theme.radiusSm
                    // StackView가 push한 아이템에 자동으로 붙여주는 첨부 프로퍼티로
                    // 직접 pop -- window까지 신호를 두 단계 거치지 않음
                    onClicked: root.StackView.view.pop()
                }
            }

            Text {
                id: idText
                anchors.left: backButton.right
                anchors.leftMargin: Theme.spacingMd
                anchors.verticalCenter: parent.verticalCenter
                text: root.cameraId
                color: Theme.colorTextPrimary
                font.pixelSize: Theme.typeHeading.size
                font.bold: true
            }

            RiskBanner {
                anchors.left: idText.right
                anchors.leftMargin: Theme.spacingMd
                anchors.verticalCenter: parent.verticalCenter
                visible: root.riskLevel !== 0 || root.exceptionState !== 0
                riskLevel: root.riskLevel
                exceptionState: root.exceptionState
            }
        }

        Item {
            anchors.top: header.bottom
            anchors.topMargin: Theme.spacingMd
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.spacingMd
            clip: true

            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusSm
                color: Theme.colorSurfaceSunken

                Rectangle {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: Theme.borderWidthHairline
                    color: Theme.colorHairlineTop
                }
            }

            CameraVideoView {
                anchors.fill: parent
                anchors.margins: 2
                cameraId: root.cameraId
                personBBox: root.personBBox
                forkliftBBox: root.forkliftBBox
                distanceM: root.distanceM
                distanceValid: root.distanceValid
                riskLevel: root.riskLevel
            }
        }
    }
}
