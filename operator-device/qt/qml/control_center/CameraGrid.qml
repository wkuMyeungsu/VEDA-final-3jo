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

    // GridView: QML의 격자 목록 컴포넌트. model에 C++ 쪽 CameraListModel을
    // 그대로 꽂으면, 모델의 행 하나마다 delegate(아래 Item)가 하나씩 자동 생성됨
    GridView {
        id: gridView
        anchors.fill: parent
        clip: true
        cellWidth: root.width / root.columns
        cellHeight: cellWidth * 9 / 16 + Theme.cameraCardChromeHeight  // 16:9 영상 비율 + 하단 정보줄
        model: cameraListModel

        // delegate 안의 "model.xxx"는 CameraListModel::roleNames()에서
        // 등록한 이름들 (cameraId, name, zone, riskLevel, ...)
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
                distanceValid: model.distanceValid
                videoConnectionState: model.videoConnectionState
                onClicked: root.cameraSelected(model.cameraId)
            }
        }
    }
}
