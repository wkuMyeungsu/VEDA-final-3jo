import QtQuick
import Safety.Common

// 1 + (N-1) 포커스 레이아웃: 메인 대형 뷰 1대 + 사이드 서브 카메라 (N-1)대 스트립
Item {
    id: root
    property string activeCameraId: cameraListModel.count > 0 ? cameraListModel.data(cameraListModel.index(0, 0), CameraListModel.CameraIdRole) : ""

    signal cameraSelected(string cameraId)

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
                cameraName: {
                    for (var i = 0; i < cameraListModel.count; ++i) {
                        var idx = cameraListModel.index(i, 0)
                        if (cameraListModel.data(idx, CameraListModel.CameraIdRole) === root.activeCameraId)
                            return cameraListModel.data(idx, CameraListModel.NameRole)
                    }
                    return root.activeCameraId
                }
                zone: {
                    for (var i = 0; i < cameraListModel.count; ++i) {
                        var idx = cameraListModel.index(i, 0)
                        if (cameraListModel.data(idx, CameraListModel.CameraIdRole) === root.activeCameraId)
                            return cameraListModel.data(idx, CameraListModel.ZoneRole)
                    }
                    return ""
                }
                riskLevel: {
                    for (var i = 0; i < cameraListModel.count; ++i) {
                        var idx = cameraListModel.index(i, 0)
                        if (cameraListModel.data(idx, CameraListModel.CameraIdRole) === root.activeCameraId)
                            return cameraListModel.data(idx, CameraListModel.RiskLevelRole)
                    }
                    return 0
                }
                distanceM: {
                    for (var i = 0; i < cameraListModel.count; ++i) {
                        var idx = cameraListModel.index(i, 0)
                        if (cameraListModel.data(idx, CameraListModel.CameraIdRole) === root.activeCameraId)
                            return cameraListModel.data(idx, CameraListModel.DistanceRole)
                    }
                    return 0
                }
                distanceValid: {
                    for (var i = 0; i < cameraListModel.count; ++i) {
                        var idx = cameraListModel.index(i, 0)
                        if (cameraListModel.data(idx, CameraListModel.CameraIdRole) === root.activeCameraId)
                            return cameraListModel.data(idx, CameraListModel.DistanceValidRole)
                    }
                    return true
                }
                videoConnectionState: {
                    for (var i = 0; i < cameraListModel.count; ++i) {
                        var idx = cameraListModel.index(i, 0)
                        if (cameraListModel.data(idx, CameraListModel.CameraIdRole) === root.activeCameraId)
                            return cameraListModel.data(idx, CameraListModel.VideoConnectionStateRole)
                    }
                    return 0
                }
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
