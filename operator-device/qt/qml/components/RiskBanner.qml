import QtQuick
import Safety.Common

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
    radius: large ? Theme.radiusMd : Theme.radiusXs
    height: large ? (contentColumn.implicitHeight + Theme.spacingSm * 2) : 22
    implicitHeight: height
    implicitWidth: large ? (contentColumn.implicitWidth + Theme.spacingLg * 2) : (compactRow.implicitWidth + 12)

    // 대형 뷰(Large HUD)일 때 Column 레이아웃
    Column {
        id: contentColumn
        anchors.centerIn: parent
        spacing: Theme.spacingXs
        visible: root.large

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.dataStale ? "UNKNOWN" : Theme.riskLabel(root.riskLevel)
            color: root.accent
            font.weight: Font.Bold
            font.pixelSize: Theme.typeTitle.size
            font.letterSpacing: 0.5
        }
        Text {
            visible: root.exceptionState !== 0
            anchors.horizontalCenter: parent.horizontalCenter
            text: Theme.exceptionLabel(root.exceptionState)
            color: Theme.colorTextSecondary
            font.pixelSize: Theme.typeBody.size
        }
    }

    // 소형/카드 뷰(Compact)일 때 단일 행(Single Row) 레이아웃
    Row {
        id: compactRow
        anchors.centerIn: parent
        spacing: 4
        visible: !root.large

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.dataStale 
                  ? Theme.exceptionLabel(root.exceptionState) 
                  : Theme.riskLabel(root.riskLevel)
            color: root.accent
            font.weight: Font.DemiBold
            font.pixelSize: 10
            font.letterSpacing: 0.2
        }
    }
}
