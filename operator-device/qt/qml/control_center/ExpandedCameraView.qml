import QtQuick
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
    signal backRequested()

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

    Rectangle {
        anchors.fill: parent
        color: Theme.colorSurface
        radius: Theme.radiusMd
        border.color: root.riskLevel !== 0 ? Theme.riskColor(root.riskLevel) : Theme.colorBorder
        border.width: root.riskLevel !== 0 ? 2 : 1

        Item {
            id: header
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: Theme.spacingMd
            height: 36

            Rectangle {
                id: backButton
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 36
                height: 36
                radius: Theme.radiusSm
                color: Theme.colorSurfaceElevated
                border.color: Theme.colorBorder
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "←"
                    color: Theme.colorTextPrimary
                    font.pixelSize: Theme.fontSizeLg
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.backRequested()
                }
            }

            Text {
                id: idText
                anchors.left: backButton.right
                anchors.leftMargin: Theme.spacingMd
                anchors.verticalCenter: parent.verticalCenter
                text: root.cameraId
                color: Theme.colorTextPrimary
                font.pixelSize: Theme.fontSizeLg
                font.bold: true
            }

            RiskBanner {
                anchors.left: idText.right
                anchors.leftMargin: Theme.spacingMd
                anchors.verticalCenter: parent.verticalCenter
                visible: root.riskLevel !== 0
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

            CameraVideoView {
                anchors.fill: parent
                cameraId: root.cameraId
                personBBox: root.personBBox
                forkliftBBox: root.forkliftBBox
                distanceM: root.distanceM
                distanceValid: root.distanceValid
            }
        }
    }
}
