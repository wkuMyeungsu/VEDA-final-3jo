import QtQuick
import QtQuick.Controls
import Safety.Common

// Demo-only control surface, opened with Ctrl+Shift+D. Adds camera_id
// switching / handover on top of the shared risk/connection controls.
Rectangle {
    id: root
    color: Theme.colorSurfaceElevated
    border.color: Theme.colorBorderStrong
    border.width: 1

    Flickable {
        anchors.fill: parent
        anchors.margins: Theme.spacingMd
        contentHeight: content.implicitHeight
        clip: true

        Column {
            id: content
            width: parent.width
            spacing: Theme.spacingMd

            Text {
                text: "데모 패널"
                color: Theme.colorTextPrimary
                font.pixelSize: Theme.fontSizeLg
                font.bold: true
            }

            Switch {
                text: "Mock 이벤트 자동 재생"
                checked: demoController.autoPlay
                onToggled: demoController.autoPlay = checked
            }

            Switch {
                text: "서버 연결"
                checked: serverConnection.connectionState !== 0
                onToggled: demoController.setServerConnected(checked)
            }

            Text {
                text: "현재 카메라: " + activeCamera.activeCameraId
                color: Theme.colorTextSecondary
                font.pixelSize: Theme.fontSizeSm
            }

            ComboBox {
                id: cameraCombo
                width: parent.width
                model: cameraListModel
                textRole: "cameraId"
                valueRole: "cameraId"
            }

            Button {
                text: "이 카메라로 전환 (handover)"
                onClicked: demoController.triggerHandover(activeCamera.activeCameraId, cameraCombo.currentValue)
            }

            Switch {
                text: "카메라 연결"
                checked: true
                onToggled: demoController.setCameraConnected(activeCamera.activeCameraId, checked)
            }

            Text { text: "위험 단계 강제 설정"; color: Theme.colorTextSecondary; font.pixelSize: Theme.fontSizeSm }

            Flow {
                width: parent.width
                spacing: Theme.spacingXs

                Repeater {
                    model: ["SAFE", "CAUTION", "DANGER", "EMERGENCY"]
                    delegate: Button {
                        text: modelData
                        onClicked: {
                            const distances = [5.0, 2.0, 0.9, 0.3]
                            demoController.setCameraRisk(activeCamera.activeCameraId, index, distances[index], "NONE")
                        }
                    }
                }
            }

            Button {
                text: "위험 상태 해제"
                onClicked: demoController.clearCameraRisk(activeCamera.activeCameraId)
            }

            Text { text: "예외 상태 강제 설정"; color: Theme.colorTextSecondary; font.pixelSize: Theme.fontSizeSm }

            Flow {
                width: parent.width
                spacing: Theme.spacingXs

                Repeater {
                    model: ["SENSOR_FAULT", "DEAD_RECKONING", "EMERGENCY_IMPACT", "NETWORK_DISCONNECTED", "CAMERA_DISCONNECTED"]
                    delegate: Button {
                        text: modelData
                        onClicked: demoController.setCameraRisk(activeCamera.activeCameraId, 2, 1.0, modelData)
                    }
                }
            }
        }
    }
}
