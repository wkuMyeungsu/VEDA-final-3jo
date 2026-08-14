import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import Safety.Common

// Demo-only control surface, opened with Ctrl+Shift+D (see
// ControlCenterWindow.qml). Every action here calls into DemoController,
// which is a no-op-safe wrapper around the mock sources -- nothing here
// touches real hardware or protocols.
Item {
    id: root

    // 오른쪽에서 본문 위로 뜨는 패널이라 그림자 적용 (허용된 2곳 중 하나)
    // - 본문을 덮는 왼쪽으로 그림자가 지도록 음수 오프셋
    MultiEffect {
        source: panel
        anchors.fill: panel
        shadowEnabled: true
        shadowColor: "#000000"
        shadowOpacity: 0.35
        shadowBlur: 0.5
        shadowHorizontalOffset: -6
    }

    Rectangle {
        id: panel
        anchors.fill: parent
        color: Theme.colorSurfaceElevated
        border.color: Theme.colorBorderStrong
        border.width: Theme.borderWidthHairline

        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: Theme.borderWidthHairline
            color: Theme.colorHairlineTop
        }

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
                    font.pixelSize: Theme.typeHeading.size
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

                Text { text: "대상 카메라"; color: Theme.colorTextSecondary; font.pixelSize: Theme.typeCaption.size }

                ComboBox {
                    id: cameraCombo
                    width: parent.width
                    model: cameraListModel
                    textRole: "cameraId"
                    valueRole: "cameraId"
                }

                Switch {
                    text: "카메라 연결"
                    checked: true
                    onToggled: demoController.setCameraConnected(cameraCombo.currentValue, checked)
                }

                Text { text: "위험 단계 강제 설정"; color: Theme.colorTextSecondary; font.pixelSize: Theme.typeCaption.size }

                Flow {
                    width: parent.width
                    spacing: Theme.spacingXs

                    Repeater {
                        model: ["SAFE", "CAUTION", "DANGER", "EMERGENCY"]
                        delegate: Button {
                            text: modelData
                            onClicked: {
                                const distances = [5.0, 2.0, 0.9, 0.3]
                                demoController.setCameraRisk(cameraCombo.currentValue, index, distances[index], "NONE")
                            }
                        }
                    }
                }

                Button {
                    text: "위험 상태 해제 (자동으로 복귀)"
                    onClicked: demoController.clearCameraRisk(cameraCombo.currentValue)
                }

                Text { text: "예외 상태 강제 설정"; color: Theme.colorTextSecondary; font.pixelSize: Theme.typeCaption.size }

                Flow {
                    width: parent.width
                    spacing: Theme.spacingXs

                    Repeater {
                        model: ["SENSOR_FAULT", "DEAD_RECKONING", "EMERGENCY_IMPACT", "NETWORK_DISCONNECTED", "CAMERA_DISCONNECTED"]
                        delegate: Button {
                            text: modelData
                            onClicked: demoController.setCameraRisk(cameraCombo.currentValue, 0, 1.0, modelData)
                        }
                    }
                }

                Text { text: "bbox 데모"; color: Theme.colorTextSecondary; font.pixelSize: Theme.typeCaption.size }

                Button {
                    text: "person / forklift bbox 무작위 이동"
                    onClicked: {
                        demoController.setPersonBBox(cameraCombo.currentValue, Math.random() * 0.6, Math.random() * 0.6, 0.12, 0.30)
                        demoController.setForkliftBBox(cameraCombo.currentValue, Math.random() * 0.6, Math.random() * 0.6, 0.24, 0.22)
                    }
                }

                Button {
                    text: "bbox 오버라이드 해제"
                    onClicked: demoController.clearBBoxOverrides(cameraCombo.currentValue)
                }
            }
        }
    }
}
