import QtQuick
import Safety.Common

// 반응형 카메라 그리드 -- 화면 폭에 따라 열 개수가 자동으로 늘어나서
// 카메라 2대든 5대든 코드 수정 없이 2x2 ~ 그 이상으로 알아서 배치됨
Item {
    id: root
    signal cameraSelected(string cameraId)

    // 카드 하나 최소 폭 320px 기준으로 몇 열이 들어갈지 계산 (최소 2열)
    readonly property int minCardWidth: 320
    readonly property int columns: Math.max(2, Math.floor(width / minCardWidth))

    // 방향키 이동은 ControlCenterWindow의 Shortcut이 호출함.
    // GridView에 Keys 핸들러를 다는 방식은 StackView 안에서 포커스가
    // 안 내려와 동작하지 않았음 -- 창 레벨 Shortcut이 확실히 동작하는 경로
    function moveLeft() { gridView.currentIndex = Math.max(0, gridView.currentIndex - 1) }
    function moveRight() { gridView.currentIndex = Math.min(gridView.count - 1, gridView.currentIndex + 1) }
    function moveUp() { gridView.currentIndex = Math.max(0, gridView.currentIndex - root.columns) }
    function moveDown() { gridView.currentIndex = Math.min(gridView.count - 1, gridView.currentIndex + root.columns) }
    function activateCurrent() {
        if (gridView.currentItem)
            root.cameraSelected(gridView.currentItem.cameraId)
    }

    // GridView: QML의 격자 목록 컴포넌트. model에 C++ 쪽 CameraListModel을
    // 그대로 꽂으면, 모델의 행 하나마다 delegate(아래 Item)가 하나씩 자동 생성됨
    GridView {
        id: gridView
        anchors.fill: parent
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
