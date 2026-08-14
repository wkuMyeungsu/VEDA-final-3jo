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

    // - 예외상태가 하나라도 있으면 riskLevel 신뢰 불가 (안전으로 오인 방지)
    readonly property bool dataStale: exceptionState !== 0
    readonly property color accent: dataStale ? Theme.colorUnknown : Theme.riskColor(riskLevel)

    color: dataStale ? Theme.colorUnknownBg : Theme.riskBgColor(riskLevel)
    border.color: accent
    border.width: Theme.borderWidthHairline
    radius: large ? Theme.radiusMd : Theme.radiusPill
    implicitHeight: contentColumn.implicitHeight + (large ? Theme.spacingSm : Theme.spacingXs) * 2
    implicitWidth: contentColumn.implicitWidth + (large ? Theme.spacingLg : Theme.spacingMd) * 2

    Column {
        id: contentColumn
        anchors.centerIn: parent
        spacing: root.large ? Theme.spacingXs : 2

        Row {
            id: contentRow
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: root.large ? Theme.spacingSm : Theme.spacingXs

            Text {
                text: root.dataStale ? "?" : Theme.riskIcon(root.riskLevel)
                color: root.accent
                font.pixelSize: root.large ? Theme.typeTitle.size : Theme.typeCaption.size
                font.weight: Font.DemiBold
            }
            Text {
                text: root.dataStale ? "UNKNOWN" : Theme.riskLabel(root.riskLevel)
                color: root.accent
                font.weight: Font.DemiBold
                font.pixelSize: root.large ? Theme.typeTitle.size : Theme.typeCaption.size
                font.letterSpacing: 0.5
            }
        }
        Text {
            visible: root.exceptionState !== 0
            anchors.horizontalCenter: parent.horizontalCenter
            text: Theme.exceptionLabel(root.exceptionState)
            color: Theme.colorTextSecondary
            font.pixelSize: root.large ? Theme.typeBody.size : (Theme.typeCaption.size - 1)
        }
    }
}
