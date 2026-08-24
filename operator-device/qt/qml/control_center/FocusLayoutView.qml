import QtQuick
import Safety.Common

// 1 + (N-1) 포커스 레이아웃: 메인 대형 뷰 1대 + 사이드 서브 카메라 (N-1)대 스트립
Item {
    id: root
    property string activeCameraId: "CAM_01_CH_01"

    signal cameraSelected(string cameraId)

    Component.onCompleted: {
        if (cameraListModel.count > 0) {
            var firstId = cameraListModel.cameraIdAt(0);
            if (firstId && firstId.length > 0)
                root.activeCameraId = firstId;
        }
    }

    Row {
        anchors.fill: parent
        spacing: Theme.spacingMd

        // 메인 대형 카메라 영역
        Item {
            width: parent.width - 220 - Theme.spacingMd
            height: parent.height

            CameraCard {
                anchors.fill: parent
                cameraId: root.activeCameraId
                cameraName: cameraListModel.nameFor(root.activeCameraId)
                zone: cameraListModel.zoneFor(root.activeCameraId)
                riskLevel: cameraListModel.riskLevelFor(root.activeCameraId)
                exceptionState: cameraListModel.exceptionStateFor(root.activeCameraId)
                distanceM: cameraListModel.distanceMFor(root.activeCameraId)
                distanceValid: cameraListModel.distanceValidFor(root.activeCameraId)
                videoConnectionState: cameraListModel.videoConnectionStateFor(root.activeCameraId)
                onClicked: root.cameraSelected(root.activeCameraId)
            }
        }

        // 사이드 서브 카메라 (N-1)대 세로 스트립
        ListView {
            width: 220
            height: parent.height
            clip: true
            spacing: Theme.spacingSm
            model: cameraListModel

            delegate: Item {
                width: ListView.view.width
                height: visible ? 130 : 0
                visible: model.cameraId !== root.activeCameraId

                CameraCard {
                    anchors.fill: parent
                    cameraId: model.cameraId
                    cameraName: model.name
                    zone: model.zone
                    riskLevel: model.riskLevel
                    exceptionState: model.exceptionState
                    distanceM: model.distanceM
                    distanceValid: model.distanceValid
                    videoConnectionState: model.videoConnectionState
                    onClicked: root.activeCameraId = model.cameraId
                }
            }
        }
    }
}
