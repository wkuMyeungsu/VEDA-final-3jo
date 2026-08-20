import QtQuick
import Safety.Common

// High-tech Slim HUD Warning Frame: 2px neon hairline border + 4 L-shaped corner brackets.
// Does NOT block central camera video, but pulses vibrantly in peripheral vision on Danger/Emergency.
Item {
    id: root
    property int riskLevel: 0

    readonly property color accent: Theme.riskColor(riskLevel)
    readonly property bool isAlert: riskLevel > 0
    property real pulseFactor: 1.0

    visible: opacity > 0
    opacity: isAlert ? pulseFactor : 0.0

    Behavior on opacity { NumberAnimation { duration: 180 } }

    // 1) 2px 슬림 네온 외곽선
    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: root.accent
        border.width: root.isAlert ? 2 : 0
    }

    // 2) 4개 모서리 L자 하이테크 HUD 브라켓
    readonly property int bracketLen: 32
    readonly property int bracketThick: 3

    // 좌상단 (Top-Left)
    Item {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 4
        width: root.bracketLen
        height: root.bracketLen

        Rectangle { width: root.bracketLen; height: root.bracketThick; color: root.accent }
        Rectangle { width: root.bracketThick; height: root.bracketLen; color: root.accent }
    }

    // 우상단 (Top-Right)
    Item {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 4
        width: root.bracketLen
        height: root.bracketLen

        Rectangle { anchors.right: parent.right; width: root.bracketLen; height: root.bracketThick; color: root.accent }
        Rectangle { anchors.right: parent.right; width: root.bracketThick; height: root.bracketLen; color: root.accent }
    }

    // 좌하단 (Bottom-Left)
    Item {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 4
        width: root.bracketLen
        height: root.bracketLen

        Rectangle { anchors.bottom: parent.bottom; width: root.bracketLen; height: root.bracketThick; color: root.accent }
        Rectangle { anchors.bottom: parent.bottom; width: root.bracketThick; height: root.bracketLen; color: root.accent }
    }

    // 우하단 (Bottom-Right)
    Item {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 4
        width: root.bracketLen
        height: root.bracketLen

        Rectangle { anchors.right: parent.right; anchors.bottom: parent.bottom; width: root.bracketLen; height: root.bracketThick; color: root.accent }
        Rectangle { anchors.right: parent.right; anchors.bottom: parent.bottom; width: root.bracketThick; height: root.bracketLen; color: root.accent }
    }

    // 3) Danger / Emergency 펄스 애니메이션
    SequentialAnimation {
        running: root.riskLevel >= 2
        loops: Animation.Infinite
        onStopped: root.pulseFactor = 1.0

        NumberAnimation { target: root; property: "pulseFactor"; to: 0.35; duration: 350; easing.type: Easing.InOutQuad }
        NumberAnimation { target: root; property: "pulseFactor"; to: 1.0; duration: 350; easing.type: Easing.InOutQuad }
    }
}
