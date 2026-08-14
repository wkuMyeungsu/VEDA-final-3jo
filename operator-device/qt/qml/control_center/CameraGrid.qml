import QtQuick
import Safety.Common

// 반응형 카메라 그리드 -- 화면 폭에 따라 열 개수가 자동으로 늘어나서
// 카메라 2대든 5대든 코드 수정 없이 2x2 ~ 그 이상으로 알아서 배치됨
Item {
    id: root
    property string cameraId: "CAM_01"
    signal cameraSelected(string cameraId)
    signal backRequested()

    // 카드 하나 최소 폭 320px 기준으로 몇 열이 들어갈지 계산 (최소 2열)
    readonly property int minCardWidth: 320
    readonly property int columns: Math.max(2, Math.floor(width / minCardWidth))

    function moveLeft() { gridView.currentIndex = Math.max(0, gridView.currentIndex - 1) }
    function moveRight() { gridView.currentIndex = Math.min(gridView.count - 1, gridView.currentIndex + 1) }
    function moveUp() { gridView.currentIndex = Math.max(0, gridView.currentIndex - root.columns) }
    function moveDown() { gridView.currentIndex = Math.min(gridView.count - 1, gridView.currentIndex + root.columns) }
    function activateCurrent() {
        if (gridView.currentItem)
            root.cameraSelected(gridView.currentItem.cameraId)
    }

    // 상단 헤더 바
    Item {
        id: headerBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 36

        Row {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingSm

            Rectangle {
                width: 32
                height: 32
                radius: Theme.radiusSm
                color: Theme.colorSurfaceElevated
                border.width: 1
                border.color: Theme.colorBorder

                Text {
                    anchors.centerIn: parent
                    text: "←"
                    color: Theme.colorTextPrimary
                    font.pixelSize: Theme.typeHeading.size - 2
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.backRequested()
                }
            }

            Text {
                text: "📹 " + root.cameraId + " › 4채널 멀티센서 모니터링 (CH 1 ~ CH 4)"
                color: Theme.colorTextPrimary
                font.pixelSize: Theme.typeHeading.size - 2
                font.weight: Font.Bold
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    GridView {
        id: gridView
        anchors.top: headerBar.bottom
        anchors.topMargin: Theme.spacingSm
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        cellWidth: root.width / root.columns
        cellHeight: cellWidth * 9 / 16 + Theme.cameraCardChromeHeight  // 16:9 영상 비율 + 하단 정보줄
        model: cameraListModel

        highlightFollowsCurrentItem: true
        // 기본 스프링 애니메이션은 셀 사이에서 링이 늘어져 보임 -- 짧게 고정
        highlightMoveDuration: Theme.animationFast
        highlight: Rectangle {
            color: "transparent"
            radius: Theme.radiusMd
            border.width: 2
            border.color: Theme.colorFocusRing
        }

        // delegate 안의 "model.xxx"는 CameraListModel::roleNames()에서
        // 등록한 이름들 (cameraId, name, zone, riskLevel, ...)
        delegate: Item {
            id: delegateItem
            width: gridView.cellWidth
            height: gridView.cellHeight
            // activateCurrent()에서 gridView.currentItem.cameraId로 조회하기 위한
            // 델리게이트 루트 레벨 프로퍼티
            property string cameraId: model.cameraId

            CameraCard {
                anchors.fill: parent
                anchors.margins: Theme.spacingSm
                cameraId: model.cameraId
                cameraName: model.name
                zone: model.zone
                riskLevel: model.riskLevel
                exceptionState: model.exceptionState
                distanceM: model.distanceM
                distanceValid: model.distanceValid
                videoConnectionState: model.videoConnectionState
                onClicked: root.cameraSelected(model.cameraId)
            }
        }
    }
}
