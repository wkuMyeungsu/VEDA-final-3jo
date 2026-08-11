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

    // 예외상태가 뭐든(통신 끊김, 센서 이상, 추측항법 등) 하나라도 있으면 riskLevel을
    // 있는 그대로("SAFE" 등) 보여주지 않음 - 안전이 확정된 게 아니라 판정을 못 믿는
    // 상태라 "안전"으로 오인하게 만들면 안 됨
    readonly property bool dataStale: exceptionState !== 0
    readonly property color accent: dataStale ? Theme.colorUnknown : Theme.riskColor(riskLevel)

    color: dataStale ? Theme.colorUnknownBg : Theme.riskBgColor(riskLevel)
    border.color: accent
    border.width: 1
    radius: large ? Theme.radiusLg : Theme.radiusSm
    implicitHeight: contentColumn.implicitHeight + (large ? Theme.spacingMd : Theme.spacingXs) * 2
    implicitWidth: contentColumn.implicitWidth + (large ? Theme.spacingLg : Theme.spacingMd) * 2

    Column {
        id: contentColumn
        anchors.centerIn: parent
        spacing: root.large ? Theme.spacingXs : 2

        Row {
            id: contentRow
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: root.large ? Theme.spacingMd : Theme.spacingSm

            Text {
                text: root.dataStale ? "?" : Theme.riskIcon(root.riskLevel)
                color: root.accent
                font.pixelSize: root.large ? Theme.fontSizeXl : Theme.fontSizeMd
                font.bold: true
            }
            Text {
                text: root.dataStale ? "UNKNOWN" : Theme.riskLabel(root.riskLevel)
                color: root.accent
                font.bold: true
                font.pixelSize: root.large ? Theme.fontSizeXl : Theme.fontSizeMd
            }
        }
        Text {
            visible: root.exceptionState !== 0
            anchors.horizontalCenter: parent.horizontalCenter
            text: Theme.exceptionLabel(root.exceptionState)
            color: Theme.colorTextSecondary
            font.pixelSize: root.large ? Theme.fontSizeMd : Theme.fontSizeSm
        }
    }
}
