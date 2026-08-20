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

    // QtQuick.Controls는 스타일을 "Basic"으로 고정하고 앱 팔레트도 다크로 맞췄지만(main.cpp),
    // 이 패널의 배경/테두리 톤(Theme 토큰)까지 그대로 따라오진 않으므로 여기서만 쓰는
    // 컨트롤 3종을 직접 재스타일함 -- 데모 패널 밖에는 QtQuick.Controls 사용처가 없음
    component StyledSwitch: Switch {
        id: control

        contentItem: Text {
            text: control.text
            color: Theme.colorTextPrimary
            font.pixelSize: Theme.typeBody.size
            leftPadding: control.indicator.width + control.spacing
            verticalAlignment: Text.AlignVCenter
        }

        indicator: Rectangle {
            implicitWidth: 44
            implicitHeight: 24
            x: control.leftPadding
            y: control.topPadding + (control.availableHeight - height) / 2
            radius: Theme.radiusPill
            color: control.checked ? Theme.colorAccent : Theme.colorSurface
            border.width: Theme.borderWidthHairline
            border.color: control.checked ? Theme.colorAccent : Theme.colorBorderStrong

            Rectangle {
                x: control.checked ? parent.width - width - 2 : 2
                y: 2
                width: 20
                height: 20
                radius: Theme.radiusPill
                color: Theme.colorTextPrimary
                Behavior on x { NumberAnimation { duration: Theme.animationFast } }
            }
        }
    }

    component StyledComboBox: ComboBox {
        id: control

        contentItem: Text {
            text: control.displayText
            color: Theme.colorTextPrimary
            font.pixelSize: Theme.typeBody.size
            leftPadding: Theme.spacingSm
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            implicitHeight: Theme.statusRowHeight
            radius: Theme.radiusSm
            color: Theme.colorSurface
            border.width: Theme.borderWidthHairline
            border.color: control.activeFocus ? Theme.colorAccent : Theme.colorBorder
        }

        indicator: Text {
            x: control.width - width - Theme.spacingSm
            y: control.topPadding + (control.availableHeight - height) / 2
            text: "▾"
            color: Theme.colorTextSecondary
            font.pixelSize: Theme.typeBody.size
        }

        popup: Popup {
            y: control.height
            width: control.width
            implicitHeight: contentItem.implicitHeight
            padding: Theme.borderWidthHairline

            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: control.delegateModel
                currentIndex: control.highlightedIndex
            }

            background: Rectangle {
                color: Theme.colorSurfaceElevated
                border.width: Theme.borderWidthHairline
                border.color: Theme.colorBorderStrong
                radius: Theme.radiusSm
            }
        }
    }

    component StyledButton: Button {
        id: control

        contentItem: Text {
            text: control.text
            color: Theme.colorTextPrimary
            font.pixelSize: Theme.typeBody.size
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            radius: Theme.radiusSm
            color: control.down ? Theme.colorSurfaceSunken : Theme.colorSurface
            border.width: Theme.borderWidthHairline
            border.color: control.hovered ? Theme.colorAccent : Theme.colorBorder
        }
    }

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

                StyledSwitch {
                    text: "Mock 이벤트 자동 재생"
                    checked: demoController.autoPlay
                    onToggled: demoController.autoPlay = checked
                }

                StyledSwitch {
                    text: "서버 연결"
                    checked: serverConnection.connectionState !== 0
                    onToggled: demoController.setServerConnected(checked)
                }

                Text { text: "대상 카메라"; color: Theme.colorTextSecondary; font.pixelSize: Theme.typeCaption.size }

                StyledComboBox {
                    id: cameraCombo
                    width: parent.width
                    model: cameraListModel
                    textRole: "name"
                    valueRole: "cameraId"
                }

                StyledSwitch {
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
                        delegate: StyledButton {
                            text: modelData
                            onClicked: {
                                const distances = [5.0, 2.0, 0.9, 0.3]
                                demoController.setCameraRisk(cameraCombo.currentValue, index, distances[index], "NONE")
                            }
                        }
                    }
                }

                StyledButton {
                    text: "위험 상태 해제 (자동으로 복귀)"
                    onClicked: demoController.clearCameraRisk(cameraCombo.currentValue)
                }

                Text { text: "예외 상태 강제 설정"; color: Theme.colorTextSecondary; font.pixelSize: Theme.typeCaption.size }

                Flow {
                    width: parent.width
                    spacing: Theme.spacingXs

                    Repeater {
                        model: ["SENSOR_FAULT", "DEAD_RECKONING", "EMERGENCY_IMPACT", "NETWORK_DISCONNECTED", "CAMERA_DISCONNECTED"]
                        delegate: StyledButton {
                            text: modelData
                            onClicked: demoController.setCameraRisk(cameraCombo.currentValue, 0, 1.0, modelData)
                        }
                    }
                }

                Text { text: "bbox 데모"; color: Theme.colorTextSecondary; font.pixelSize: Theme.typeCaption.size }

                StyledButton {
                    text: "person / forklift bbox 무작위 이동"
                    onClicked: {
                        demoController.setPersonBBox(cameraCombo.currentValue, Math.random() * 0.6, Math.random() * 0.6, 0.12, 0.30)
                        demoController.setForkliftBBox(cameraCombo.currentValue, Math.random() * 0.6, Math.random() * 0.6, 0.24, 0.22)
                    }
                }

                StyledButton {
                    text: "bbox 오버라이드 해제"
                    onClicked: demoController.clearBBoxOverrides(cameraCombo.currentValue)
                }
            }
        }
    }
}
