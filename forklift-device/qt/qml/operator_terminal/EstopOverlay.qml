import QtQuick
import Safety.Common

// High-contrast Full-screen Alert for FPGA Hardware Interlock States
// (Hardware ESTOP button pressed or Forward-movement relay cutoff engaged).
// Zero emojis, 100% industrial safety typography with prominent action guide.
Item {
    id: root
    property bool estopActive: false
    property bool movementCutoffActive: false

    visible: root.estopActive || root.movementCutoffActive

    // 1) 딤 배경
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0.04, 0.05, 0.08, 0.88)
    }

    // 2) 비상 상태 모던 알림 카드
    Rectangle {
        anchors.centerIn: parent
        implicitWidth: estopCol.implicitWidth + 64
        implicitHeight: estopCol.implicitHeight + 48
        radius: Theme.radiusMd
        color: Qt.rgba(0.12, 0.05, 0.07, 0.95)
        border.width: 2
        border.color: Theme.colorEmergency

        Column {
            id: estopCol
            anchors.centerIn: parent
            spacing: 16

            // 상단 하이테크 비상 태그
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 140
                height: 28
                radius: 4
                color: Theme.colorEmergency

                Text {
                    anchors.centerIn: parent
                    text: root.estopActive ? "HARDWARE ESTOP" : "MOVEMENT CUTOFF"
                    color: "#ffffff"
                    font.pixelSize: 11
                    font.bold: true
                }
            }

            // 메인 한글 경보 텍스트
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.estopActive ? "하드웨어 비상 정지 가동" : "전진 구동 차단 (릴레이 작동)"
                color: Theme.colorEmergency
                font.pixelSize: 24
                font.bold: true
            }

            // 하단 조치 안내
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                implicitWidth: guideText.implicitWidth + 24
                height: 32
                radius: 6
                color: Qt.rgba(1, 1, 1, 0.08)
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.15)

                Text {
                    id: guideText
                    anchors.centerIn: parent
                    text: "지게차 본체의 물리 리셋 버튼을 눌러야 인터락이 해제됩니다"
                    color: Theme.colorTextPrimary
                    font.pixelSize: 13
                    font.bold: true
                }
            }
        }
    }
}
