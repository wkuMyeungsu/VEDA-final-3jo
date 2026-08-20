import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Safety.Common

// 비상 대응 표준 절차 (SOP) 및 긴급 연락망 모달
Rectangle {
    id: root
    anchors.fill: parent
    color: Qt.rgba(0, 0, 0, 0.70)
    visible: false
    z: 100

    function open() {
        root.visible = true
    }

    function close() {
        root.visible = false
    }

    // 배경 클릭 시 닫기
    MouseArea {
        anchors.fill: parent
        onClicked: root.close()
    }

    // 중앙 모달 컨테이너
    Rectangle {
        id: modalBox
        width: Math.min(680, parent.width - 40)
        height: Math.min(560, parent.height - 40)
        anchors.centerIn: parent
        radius: Theme.radiusLg
        color: Theme.colorSurfaceElevated
        border.width: 1
        border.color: Theme.colorBorderStrong
        clip: true

        // 모달 내부 클릭 이벤트 전파 차단
        MouseArea {
            anchors.fill: parent
            onClicked: {}
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 16

            // 상단 헤더
            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Rectangle {
                    width: 36
                    height: 36
                    radius: Theme.radiusSm
                    color: Theme.colorAccentAlpha20
                    border.width: 1
                    border.color: Theme.colorAccent

                    Text {
                        anchors.centerIn: parent
                        text: "📋"
                        font.pixelSize: 18
                    }
                }

                Column {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: "비상 대응 표준 절차 (SOP)"
                        color: Theme.colorTextPrimary
                        font.pixelSize: 18
                        font.weight: Font.Bold
                    }
                    Text {
                        text: "지게차 사각지대 충돌 위험 감지 및 비상 상황 발생 시 조치 요령"
                        color: Theme.colorTextSecondary
                        font.pixelSize: 12
                    }
                }

                // 닫기 버튼
                Rectangle {
                    width: 32
                    height: 32
                    radius: Theme.radiusSm
                    color: closeMouse.containsMouse ? Theme.colorHoverOverlay : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: "✕"
                        color: Theme.colorTextSecondary
                        font.pixelSize: 16
                    }

                    MouseArea {
                        id: closeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.close()
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.colorBorder
            }

            // 비상 연락망 배너
            Rectangle {
                Layout.fillWidth: true
                height: 60
                radius: Theme.radiusMd
                color: Qt.rgba(0.95, 0.45, 0.13, 0.12)
                border.width: 1
                border.color: Theme.colorAccent

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 16

                    Text {
                        text: "🚨 긴급 연락망"
                        color: Theme.colorAccent
                        font.pixelSize: 13
                        font.weight: Font.Bold
                    }

                    Rectangle { width: 1; height: 24; color: Theme.colorBorder }

                    Text {
                        text: "• 현장 안전관리자: 010-1234-5678"
                        color: Theme.colorTextPrimary
                        font.pixelSize: 12
                    }

                    Text {
                        text: "• 상황실: 내선 1199"
                        color: Theme.colorTextPrimary
                        font.pixelSize: 12
                    }

                    Text {
                        text: "• 소방/구급: 119"
                        color: Theme.colorDanger
                        font.pixelSize: 12
                        font.weight: Font.Bold
                    }
                }
            }

            // 3단계 대응 절차 목록
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 10

                // 1단계: 주의 (CAUTION 3~5m)
                Rectangle {
                    Layout.fillWidth: true
                    height: 76
                    radius: Theme.radiusSm
                    color: Theme.colorSurface
                    border.width: 1
                    border.color: Theme.colorCaution

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 12

                        Rectangle {
                            width: 28
                            height: 28
                            radius: 14
                            color: Theme.colorCautionBg
                            Text { anchors.centerIn: parent; text: "1"; color: Theme.colorCaution; font.weight: Font.Bold }
                        }

                        Column {
                            Layout.fillWidth: true
                            spacing: 3
                            Text { text: "1단계 : 접근 주의 (3 ~ 5m 근접)"; color: Theme.colorCaution; font.pixelSize: 13; font.weight: Font.Bold }
                            Text { text: "• 현장 경광등 및 음성 경보 정상 작동 여부 확인 / 지게차 운전자 서행 유도"; color: Theme.colorTextSecondary; font.pixelSize: 12 }
                        }
                    }
                }

                // 2단계: 위험 (DANGER <3m)
                Rectangle {
                    Layout.fillWidth: true
                    height: 76
                    radius: Theme.radiusSm
                    color: Theme.colorSurface
                    border.width: 1
                    border.color: Theme.colorDanger

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 12

                        Rectangle {
                            width: 28
                            height: 28
                            radius: 14
                            color: Theme.colorDangerBg
                            Text { anchors.centerIn: parent; text: "2"; color: Theme.colorDanger; font.weight: Font.Bold }
                        }

                        Column {
                            Layout.fillWidth: true
                            spacing: 3
                            Text { text: "2단계 : 충돌 위험 (< 3m 근접)"; color: Theme.colorDanger; font.pixelSize: 13; font.weight: Font.Bold }
                            Text { text: "• 구역 안내 방송 송출 / 작업자 즉시 이동 유도 및 지게차 일시 정지 지시"; color: Theme.colorTextSecondary; font.pixelSize: 12 }
                        }
                    }
                }

                // 3단계: 비상 및 사고 발생
                Rectangle {
                    Layout.fillWidth: true
                    height: 76
                    radius: Theme.radiusSm
                    color: Theme.colorSurface
                    border.width: 1
                    border.color: Theme.colorEmergency

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 12

                        Rectangle {
                            width: 28
                            height: 28
                            radius: 14
                            color: Theme.colorEmergencyBg
                            Text { anchors.centerIn: parent; text: "3"; color: Theme.colorEmergency; font.weight: Font.Bold }
                        }

                        Column {
                            Layout.fillWidth: true
                            spacing: 3
                            Text { text: "3단계 : 긴급 비상 정지 및 구호 조치"; color: Theme.colorEmergency; font.pixelSize: 13; font.weight: Font.Bold }
                            Text { text: "• 전체 지게차 비상 정지(E-Stop) / 119 신고 및 사고 현장 증거 스냅샷 저장"; color: Theme.colorTextSecondary; font.pixelSize: 12 }
                        }
                    }
                }
            }

            // 하단 닫기 버튼
            Rectangle {
                Layout.alignment: Qt.AlignRight
                width: 100
                height: 36
                radius: Theme.radiusSm
                color: Theme.colorAccent

                Text {
                    anchors.centerIn: parent
                    text: "확인 완료"
                    color: "#ffffff"
                    font.pixelSize: 13
                    font.weight: Font.Bold
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.close()
                }
            }
        }
    }
}
