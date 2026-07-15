import QtQuick
import Safety.Common

// Full-screen edge highlight: a colored border that thickens at higher
// risk and pulses at Danger/Emergency, so risk is visible in peripheral
// vision without reading text.
Item {
    id: root
    property int riskLevel: 0

    readonly property color accent: Theme.riskColor(riskLevel)
    readonly property int frameWidth: riskLevel === 0 ? 0 : (riskLevel >= 2 ? 14 : 8)

    Rectangle {
        id: frame
        anchors.fill: parent
        color: "transparent"
        border.color: root.accent
        border.width: root.frameWidth
        opacity: root.riskLevel === 0 ? 0 : 0.9

        Behavior on border.width { NumberAnimation { duration: Theme.animationNormal } }
        Behavior on border.color { ColorAnimation { duration: Theme.animationNormal } }
    }

    SequentialAnimation {
        running: root.riskLevel >= 2
        loops: Animation.Infinite

        NumberAnimation { target: frame; property: "opacity"; to: 0.45; duration: 420; easing.type: Easing.InOutQuad }
        NumberAnimation { target: frame; property: "opacity"; to: 0.95; duration: 420; easing.type: Easing.InOutQuad }
    }
}
