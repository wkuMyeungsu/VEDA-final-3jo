import QtQuick
import Safety.Common

// Large, glanceable risk level + distance readout for the driver. No small
// text, no menus -- just the state that matters.
Column {
    id: root
    property int riskLevel: 0
    property int exceptionState: 0
    property real distanceM: 0
    property bool distanceValid: true

    spacing: Theme.spacingSm

    RiskBanner {
        anchors.horizontalCenter: parent.horizontalCenter
        large: true
        riskLevel: root.riskLevel
        exceptionState: root.exceptionState
    }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        visible: root.riskLevel !== 0
        text: root.distanceValid ? (root.distanceM.toFixed(2) + " m") : "측정 불가"
        color: Theme.riskColor(root.riskLevel)
        font.pixelSize: Theme.fontSizeHuge
        font.bold: true

        Behavior on color { ColorAnimation { duration: Theme.animationNormal } }
    }
}
