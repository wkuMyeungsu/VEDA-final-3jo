import QtQuick
import QtQuick.Controls
import Safety.Common

// Demo-only control surface, opened with Ctrl+Shift+D. Adds camera_id
// switching / handover on top of the shared risk/connection controls.
Rectangle {
    id: root
    color: Theme.colorSurfaceElevated
    border.color: Theme.colorBorderStrong
    border.width: Theme.borderWidthHairline

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

            Text {
                text: "현재 카메라: " + activeCamera.activeCameraId
                color: Theme.colorTextSecondary
                font.pixelSize: Theme.typeCaption.size
            }

            StyledComboBox {
                id: cameraCombo
                width: parent.width
                model: cameraListModel
                textRole: "name"
                valueRole: "streamId"
            }

            StyledButton {
                text: "선택한 채널로 전환"
                onClicked: {
                    var target = cameraCombo.currentValue ? cameraCombo.currentValue : cameraCombo.currentText
                    demoController.triggerHandover(activeCamera.activeCameraId, target)
                }
            }

            Text { text: "채널 원클릭 빠른 전환"; color: Theme.colorTextSecondary; font.pixelSize: Theme.typeCaption.size }

            Grid {
                columns: 2
                spacing: Theme.spacingXs
                width: parent.width

                Repeater {
                    model: [
                        { id: "CAM_01_CH_01", label: "CH 1 창고 입구" },
                        { id: "CAM_01_CH_02", label: "CH 2 적재 구역" },
                        { id: "CAM_01_CH_03", label: "CH 3 출하 게이트" },
                        { id: "CAM_01_CH_04", label: "CH 4 통로 교차로" }
                    ]
                    delegate: StyledButton {
                        width: (parent.width - Theme.spacingXs) / 2
                        text: modelData.label
                        onClicked: demoController.triggerHandover(activeCamera.activeCameraId, modelData.id)
                    }
                }
            }

            StyledSwitch {
                text: "카메라 연결"
                checked: true
                onToggled: demoController.setCameraConnected(activeCamera.activeCameraId, checked)
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
                            demoController.setCameraRisk(activeCamera.activeCameraId, index, distances[index], "NONE")
                        }
                    }
                }
            }

            StyledButton {
                text: "위험 상태 해제 (정상 복귀)"
                onClicked: demoController.clearCameraRisk(activeCamera.activeCameraId)
            }

            Text { text: "예외 상태 강제 설정"; color: Theme.colorTextSecondary; font.pixelSize: Theme.typeCaption.size }

            Flow {
                width: parent.width
                spacing: Theme.spacingXs

                Repeater {
                    model: [
                        { id: "DEAD_RECKONING", name: "자율 항법 (추측항법)" },
                        { id: "SENSOR_FAULT", name: "센서 점검" },
                        { id: "EMERGENCY_IMPACT", name: "비상 충돌" },
                        { id: "NETWORK_DISCONNECTED", name: "통신 끊김" },
                        { id: "CAMERA_DISCONNECTED", name: "카메라 끊김" }
                    ]
                    delegate: StyledButton {
                        text: modelData.name
                        onClicked: demoController.setCameraRisk(activeCamera.activeCameraId, 0, 0.0, modelData.id)
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: 1
                color: Theme.colorBorder
            }

            Text {
                text: "FPGA 안전 보드 점검 & 진단"
                color: Theme.colorTextPrimary
                font.pixelSize: Theme.typeSubheading.size
                font.bold: true
            }

            Rectangle {
                width: parent.width
                implicitHeight: warningCol.implicitHeight + Theme.spacingSm * 2
                color: Qt.rgba(1.0, 0.2, 0.2, 0.1)
                border.color: Theme.colorDanger
                border.width: Theme.borderWidthHairline
                radius: Theme.radiusSm

                Column {
                    id: warningCol
                    anchors.fill: parent
                    anchors.margins: Theme.spacingSm
                    spacing: Theme.spacingXs

                    Text {
                        text: "⚠️ 안전 주의"
                        color: Theme.colorDanger
                        font.bold: true
                        font.pixelSize: Theme.typeCaption.size
                    }
                    Text {
                        width: parent.width
                        text: "비상정지(E-Stop) 및 전진 차단 래치는 FPGA 물리 리셋 버튼으로만 해제됩니다. (본 소프트웨어 기능으로 해제 불가)"
                        color: Theme.colorTextSecondary
                        font.pixelSize: Theme.typeCaption.size
                        wrapMode: Text.WordWrap
                    }
                }
            }

            StyledButton {
                width: parent.width
                text: "FPGA 오류 플래그 해제 (CLEAR_ERROR)"
                onClicked: activeCamera.clearFpgaError()
            }

            Text {
                text: "FPGA 하드웨어 자가진단 (SELF_TEST)"
                color: Theme.colorTextSecondary
                font.pixelSize: Theme.typeCaption.size
            }

            StyledComboBox {
                id: selfTestCombo
                width: parent.width
                model: ["0 = 정지 (STOP)", "1 = LED 패턴 점검", "2 = 부저 점검", "3 = 전체 점검 (ALL)"]
                currentIndex: 3
            }

            Row {
                width: parent.width
                spacing: Theme.spacingXs

                StyledButton {
                    width: (parent.width - Theme.spacingXs) / 2
                    text: "자가진단 시작"
                    onClicked: activeCamera.runFpgaSelfTest(selfTestCombo.currentIndex)
                }

                StyledButton {
                    width: (parent.width - Theme.spacingXs) / 2
                    text: "진단 즉시 중지"
                    onClicked: activeCamera.runFpgaSelfTest(0)
                }
            }
        }
    }
}

