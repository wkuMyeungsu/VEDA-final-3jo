import QtQuick
import Safety.Common

// Soft Ambient Edge Light Glow:
// Radiates a smooth, cinematic warning glow inward from all 4 edges of the screen.
// Zero hard lines, zero brackets — purely soft light that gently breathes on Danger/Emergency.
Item {
    id: root
    property int riskLevel: 0

    readonly property color accent: Theme.riskColor(riskLevel)
    readonly property bool isAlert: riskLevel > 0
    property real pulseFactor: 1.0

    visible: opacity > 0
    opacity: isAlert ? pulseFactor : 0.0

    Behavior on opacity { NumberAnimation { duration: 250 } }

    readonly property int glowDepth: 52
    readonly property real maxAlpha: riskLevel >= 2 ? 0.65 : 0.45

    // 상단 앰비언트 글로우 (Top Edge Glow)
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.glowDepth
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, root.maxAlpha) }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    // 하단 앰비언트 글로우 (Bottom Edge Glow)
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.glowDepth
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, root.maxAlpha) }
        }
    }

    // 좌측 앰비언트 글로우 (Left Edge Glow)
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: root.glowDepth
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, root.maxAlpha) }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    // 우측 앰비언트 글로우 (Right Edge Glow)
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: root.glowDepth
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, root.maxAlpha) }
        }
    }

    // Danger / Emergency 부드러운 숨쉬기(Breathing Light) 펄스 애니메이션
    SequentialAnimation {
        running: root.riskLevel >= 2
        loops: Animation.Infinite
        onStopped: root.pulseFactor = 1.0

        NumberAnimation { target: root; property: "pulseFactor"; to: 0.35; duration: 400; easing.type: Easing.InOutSine }
        NumberAnimation { target: root; property: "pulseFactor"; to: 1.0; duration: 400; easing.type: Easing.InOutSine }
    }
}
