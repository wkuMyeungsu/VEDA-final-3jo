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

    // 통신/카메라 끊김(RiskTypes.ExceptionState: 4 NetworkDisconnected, 5 CameraDisconnected)이면
    // riskLevel이 서버 판정이 아니라 로컬에서 채운 기본값(Safe)이라 신뢰할 수 없음 - 그대로
    // 보여주면 끊긴 상태를 "안전"으로 오인하게 됨
    readonly property bool dataStale: exceptionState === 4 || exceptionState === 5
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
