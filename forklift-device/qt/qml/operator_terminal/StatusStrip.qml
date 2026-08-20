import QtQuick
import QtQuick.Layouts
import Safety.Common

// Exception-driven Quiet Status Banner:
// Invisible during normal operation (0% visual clutter).
// Smoothly pops up at bottom-center ONLY when a real disconnection or hardware fault occurs.
Rectangle {
    id: root

    readonly property bool hasServerFault: serverConnection.connectionState !== 2
    readonly property bool hasVideoFault: activeCamera.videoConnectionState !== 2
    readonly property bool hasFpgaFault: activeCamera.fpgaErrorLatched
    readonly property bool hasAnyFault: hasServerFault || hasVideoFault || hasFpgaFault

    visible: opacity > 0
    opacity: hasAnyFault ? 1.0 : 0.0

    Behavior on opacity { NumberAnimation { duration: 250 } }

    implicitWidth: faultRow.implicitWidth + 32
    height: 36
    radius: Theme.radiusPill
    color: Qt.rgba(0.12, 0.04, 0.05, 0.92)
    border.width: 1.5
    border.color: Theme.colorCaution

    RowLayout {
        id: faultRow
        anchors.centerIn: parent
        spacing: 10

        Rectangle {
            width: 52
            height: 20
            radius: 3
            color: Theme.colorCaution

            Text {
                anchors.centerIn: parent
                text: "SYSTEM"
                color: "#000000"
                font.pixelSize: 10
                font.bold: true
            }
        }

        Text {
            color: Theme.colorTextPrimary
            font.pixelSize: 12
            font.bold: true
            text: {
                if (root.hasServerFault) return "관제 시스템 무선 연결 재시도 중..."
                if (root.hasVideoFault) return "CCTV 영상 스트림 수신 대기 중..."
                if (root.hasFpgaFault) return "FPGA 제동 제어기 오류 누적 (정비 점검 필요)"
                return "시스템 점검"
            }
        }
    }
}
