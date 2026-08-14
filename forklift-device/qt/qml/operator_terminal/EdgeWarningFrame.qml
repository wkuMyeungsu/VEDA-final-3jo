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
    readonly property real baseOpacity: riskLevel === 0 ? 0 : 0.9  // - 위험도에 따른 기본 불투명도
    property real pulseFactor: 1.0                                  // - 펄스 배율: 애니메이션이 이 값만 변경

    Rectangle {
        id: frame
        anchors.fill: parent
        color: "transparent"
        border.color: root.accent
        border.width: root.frameWidth
        // - 기본값 x 배율: 애니메이션이 opacity를 직접 쓰면 바인딩이 끊겨 위험도 복귀 시 값이 고정됨
        opacity: root.baseOpacity * root.pulseFactor

        Behavior on border.width { NumberAnimation { duration: Theme.animationNormal } }
        Behavior on border.color { ColorAnimation { duration: Theme.animationNormal } }
    }

    SequentialAnimation {
        running: root.riskLevel >= 2
        loops: Animation.Infinite
        onStopped: root.pulseFactor = 1.0 // - 펄스 종료 시 배율 복귀

        NumberAnimation { target: root; property: "pulseFactor"; to: 0.5; duration: Theme.animationSlow; easing.type: Theme.easingStandard }
        NumberAnimation { target: root; property: "pulseFactor"; to: 1.0; duration: Theme.animationSlow; easing.type: Theme.easingStandard }
    }
}
