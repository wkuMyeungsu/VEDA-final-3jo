import QtQuick
import Safety.Common

// Risk-level banner: icon + label (+ optional exception state), colored by
// severity. Used both compact (camera card header) and large (operator
// terminal HUD) via the `large` flag.
Rectangle {
    id: root
    property int riskLevel: 0
    property int exceptionState: 0
    property bool large: false

    readonly property color accent: Theme.riskColor(riskLevel)

    color: Theme.riskBgColor(riskLevel)
    border.color: accent
    border.width: 1
    radius: large ? Theme.radiusLg : Theme.radiusSm
    implicitHeight: large ? 68 : 30

    Row {
        anchors.centerIn: parent
        spacing: root.large ? Theme.spacingMd : Theme.spacingSm

        Text {
            text: Theme.riskIcon(root.riskLevel)
            color: root.accent
            font.pixelSize: root.large ? Theme.fontSizeXl : Theme.fontSizeMd
            font.bold: true
        }
        Text {
            text: Theme.riskLabel(root.riskLevel)
            color: root.accent
            font.bold: true
            font.pixelSize: root.large ? Theme.fontSizeXl : Theme.fontSizeMd
        }
        Text {
            visible: root.exceptionState !== 0
            text: "· " + Theme.exceptionLabel(root.exceptionState)
            color: Theme.colorTextSecondary
            font.pixelSize: root.large ? Theme.fontSizeMd : Theme.fontSizeSm
        }
    }
}
