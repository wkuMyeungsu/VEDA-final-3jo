import QtQuick
import Safety.Common

// Bottom panel: rolling audit log of noteworthy events (time, camera, risk
// level, distance, exception state).
Rectangle {
    id: root
    color: Theme.colorSurface
    border.color: Theme.colorBorder
    border.width: 1

    Column {
        anchors.fill: parent
        anchors.margins: Theme.spacingSm
        spacing: Theme.spacingXs

        Text {
            text: "이벤트 로그"
            color: Theme.colorTextSecondary
            font.pixelSize: Theme.fontSizeSm
            font.bold: true
            font.letterSpacing: 1
        }

        Row {
            width: parent.width
            spacing: Theme.spacingMd
            Text { text: "시간"; width: 140; color: Theme.colorTextMuted; font.pixelSize: Theme.fontSizeSm; font.bold: true }
            Text { text: "카메라"; width: 100; color: Theme.colorTextMuted; font.pixelSize: Theme.fontSizeSm; font.bold: true }
            Text { text: "위험단계"; width: 100; color: Theme.colorTextMuted; font.pixelSize: Theme.fontSizeSm; font.bold: true }
            Text { text: "거리"; width: 80; color: Theme.colorTextMuted; font.pixelSize: Theme.fontSizeSm; font.bold: true }
            Text { text: "예외상태"; color: Theme.colorTextMuted; font.pixelSize: Theme.fontSizeSm; font.bold: true }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.colorBorder }

        ListView {
            width: parent.width
            height: parent.height - 62
            clip: true
            model: eventLogModel

            delegate: Row {
                width: ListView.view.width
                spacing: Theme.spacingMd
                height: 22

                Text {
                    text: Qt.formatDateTime(model.utcTime, "HH:mm:ss")
                    width: 140
                    color: Theme.colorTextSecondary
                    font.pixelSize: Theme.fontSizeSm
                }
                Text {
                    text: model.cameraId
                    width: 100
                    color: Theme.colorTextSecondary
                    font.pixelSize: Theme.fontSizeSm
                }
                Text {
                    text: Theme.riskLabel(model.riskLevel)
                    width: 100
                    color: Theme.riskColor(model.riskLevel)
                    font.pixelSize: Theme.fontSizeSm
                    font.bold: true
                }
                Text {
                    text: model.distanceM.toFixed(2) + " m"
                    width: 80
                    color: Theme.colorTextSecondary
                    font.pixelSize: Theme.fontSizeSm
                }
                Text {
                    text: Theme.exceptionLabel(model.exceptionState)
                    color: model.exceptionState !== 0 ? Theme.colorDanger : Theme.colorTextMuted
                    font.pixelSize: Theme.fontSizeSm
                }
            }
        }
    }
}
