import QtQuick
import Safety.Common

// Full-screen alert for FPGA hardware safety states (ESTOP button pressed,
// forward-movement relay engaged). Distinct from EdgeWarningFrame's
// software risk-level border: these are latched at the FPGA hardware level
// and stay on even if the on-screen risk level returns to safe -- only the
// physical reset button on the FPGA board clears them.
Item {
    id: root
    property bool estopActive: false
    property bool movementCutoffActive: false

    visible: root.estopActive || root.movementCutoffActive

    Rectangle {
        anchors.fill: parent
        color: Theme.colorScrim
    }

    Column {
        anchors.centerIn: parent
        spacing: Theme.spacingMd

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            // - ESTOP이 항상 우선 표시: ESTOP 활성 시 전진 차단도 항상 함께 걸리므로(PROTOCOL.md 3.2절)
            //   더 심각한 쪽 문구만 보여주면 충분하다.
            text: root.estopActive ? "⛔ 비상정지" : "🚫 전진 차단"
            color: Theme.colorEmergency
            font.pixelSize: Theme.typeDisplay.size
            font.bold: true
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "물리 리셋 버튼을 눌러야 해제됩니다"
            color: Theme.colorTextPrimary
            font.pixelSize: Theme.typeBody.size
        }
    }
}
