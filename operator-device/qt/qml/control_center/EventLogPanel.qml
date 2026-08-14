import QtQuick
import Safety.Common

// 하단 패널: 특이사항(위험/예외) 이벤트 감사 로그.
// model이 eventLogModel(C++ EventLogModel)이고, 최신 이벤트가 항상 맨 위(row 0).
// 평상시(SAFE + 예외없음) 이벤트는 애초에 C++ 쪽에서 안 쌓아서 여기 안 뜸.
Rectangle {
    id: root
    color: Theme.colorBackground

    property int filterMode: 0 // 0: 전체, 1: 위험만(Caution/Danger/Emergency), 2: 예외만
    property string statusMessage: ""

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.borderWidthHairline
        color: Theme.colorBorder
    }

    Column {
        anchors.fill: parent
        anchors.margins: Theme.spacingSm
        spacing: Theme.spacingXs

        // 상단 제어 바: 타이틀, 필터 칩, CSV 내보내기 버튼
        Item {
            width: parent.width
            height: 28

            Row {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingSm

                Text {
                    text: "감사 이벤트 로그"
                    color: Theme.colorTextSecondary
                    font.pixelSize: Theme.typeLabel.size
                    font.weight: Font.DemiBold
                    anchors.verticalCenter: parent.verticalCenter
                }

                // 필터 칩 목록
                Row {
                    spacing: 4
                    anchors.verticalCenter: parent.verticalCenter

                    Rectangle {
                        width: filterText0.implicitWidth + 12
                        height: 22
                        radius: Theme.radiusPill
                        color: root.filterMode === 0 ? Theme.colorAccentAlpha20 : "transparent"
                        border.width: 1
                        border.color: root.filterMode === 0 ? Theme.colorAccent : Theme.colorBorder

                        Text {
                            id: filterText0
                            anchors.centerIn: parent
                            text: "전체"
                            color: root.filterMode === 0 ? Theme.colorAccent : Theme.colorTextMuted
                            font.pixelSize: Theme.typeCaption.size - 1
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.filterMode = 0
                        }
                    }

                    Rectangle {
                        width: filterText1.implicitWidth + 12
                        height: 22
                        radius: Theme.radiusPill
                        color: root.filterMode === 1 ? Theme.colorDangerBg : "transparent"
                        border.width: 1
                        border.color: root.filterMode === 1 ? Theme.colorDanger : Theme.colorBorder

                        Text {
                            id: filterText1
                            anchors.centerIn: parent
                            text: "🚨 위험경보"
                            color: root.filterMode === 1 ? Theme.colorDanger : Theme.colorTextMuted
                            font.pixelSize: Theme.typeCaption.size - 1
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.filterMode = 1
                        }
                    }

                    Rectangle {
                        width: filterText2.implicitWidth + 12
                        height: 22
                        radius: Theme.radiusPill
                        color: root.filterMode === 2 ? Theme.colorCautionBg : "transparent"
                        border.width: 1
                        border.color: root.filterMode === 2 ? Theme.colorCaution : Theme.colorBorder

                        Text {
                            id: filterText2
                            anchors.centerIn: parent
                            text: "⚠ 통신/센서 장애"
                            color: root.filterMode === 2 ? Theme.colorCaution : Theme.colorTextMuted
                            font.pixelSize: Theme.typeCaption.size - 1
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.filterMode = 2
                        }
                    }
                }
            }

            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingSm

                // CSV 내보내기 완료 알림 문구
                Text {
                    text: root.statusMessage
                    color: Theme.colorSafe
                    font.pixelSize: Theme.typeCaption.size
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.statusMessage.length > 0
                }

                // [CSV 다운로드] 버튼
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: csvBtnText.implicitWidth + 16
                    height: 24
                    radius: Theme.radiusSm
                    color: csvArea.containsMouse ? Theme.colorSurfaceElevated : Qt.rgba(1, 1, 1, 0.04)
                    border.width: 1
                    border.color: csvArea.containsMouse ? Theme.colorAccent : Theme.colorBorder

                    Text {
                        id: csvBtnText
                        anchors.centerIn: parent
                        text: "📥 일일 안전일지 CSV 저장"
                        color: csvArea.containsMouse ? Theme.colorAccent : Theme.colorTextSecondary
                        font.pixelSize: Theme.typeCaption.size - 1
                        font.weight: Font.Medium
                    }

                    MouseArea {
                        id: csvArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (eventLogModel.exportToCsv("")) {
                                root.statusMessage = "✓ 바탕화면에 안전일지 CSV가 저장되었습니다."
                                statusTimer.restart()
                            }
                        }
                    }

                    Timer {
                        id: statusTimer
                        interval: 3000
                        onTriggered: root.statusMessage = ""
                    }
                }
            }
        }

        // 테이블 헤더
        Row {
            width: parent.width
            spacing: Theme.spacingMd
            Text { text: "시간"; width: 130; color: Theme.colorTextMuted; font.pixelSize: Theme.typeLabel.size }
            Text { text: "카메라 ID"; width: 100; color: Theme.colorTextMuted; font.pixelSize: Theme.typeLabel.size }
            Text { text: "구역 (Zone)"; width: 90; color: Theme.colorTextMuted; font.pixelSize: Theme.typeLabel.size }
            Text { text: "위험단계"; width: 90; color: Theme.colorTextMuted; font.pixelSize: Theme.typeLabel.size }
            Text { text: "실측 거리"; width: 80; color: Theme.colorTextMuted; font.pixelSize: Theme.typeLabel.size }
            Text { text: "예외 상태"; color: Theme.colorTextMuted; font.pixelSize: Theme.typeLabel.size }
        }

        Rectangle { width: parent.width; height: Theme.borderWidthHairline; color: Theme.colorBorder }

        ListView {
            width: parent.width
            height: parent.height - 66
            clip: true
            model: eventLogModel

            delegate: Row {
                id: rowDelegate
                property bool matchesFilter: {
                    if (root.filterMode === 1)
                        return model.riskLevel !== 0
                    if (root.filterMode === 2)
                        return model.exceptionState !== 0
                    return true
                }

                width: ListView.view.width
                spacing: Theme.spacingMd
                height: matchesFilter ? Theme.tableRowHeight : 0
                visible: matchesFilter

                Text {
                    text: Qt.formatDateTime(model.utcTime, "HH:mm:ss")
                    width: 130
                    color: Theme.colorTextSecondary
                    font.pixelSize: Theme.typeCaption.size
                }
                Text {
                    text: model.cameraId
                    width: 100
                    color: Theme.colorTextPrimary
                    font.pixelSize: Theme.typeCaption.size
                    font.weight: Font.Medium
                }
                Text {
                    text: model.zone.length > 0 ? model.zone : "-"
                    width: 90
                    color: Theme.colorTextSecondary
                    font.pixelSize: Theme.typeCaption.size
                }
                Row {
                    width: 90
                    spacing: 4
                    Rectangle {
                        width: 6
                        height: 6
                        radius: 3
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.riskColor(model.riskLevel)
                    }
                    Text {
                        text: Theme.riskLabel(model.riskLevel)
                        color: Theme.riskColor(model.riskLevel)
                        font.pixelSize: Theme.typeCaption.size
                        font.weight: Font.Medium
                    }
                }
                Text {
                    text: model.distanceValid ? model.distanceM.toFixed(2) + " m" : "측정 불가"
                    width: 80
                    color: Theme.colorTextSecondary
                    font.pixelSize: Theme.typeCaption.size
                }
                Text {
                    text: Theme.exceptionLabel(model.exceptionState)
                    color: model.exceptionState !== 0 ? Theme.colorDanger : Theme.colorTextMuted
                    font.pixelSize: Theme.typeCaption.size
                }
            }
        }
    }
}
