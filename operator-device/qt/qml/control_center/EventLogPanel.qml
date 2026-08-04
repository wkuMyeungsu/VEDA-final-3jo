import QtQuick
import Safety.Common

// 하단 패널: 특이사항(위험/예외) 이벤트 감사 로그.
// model이 eventLogModel(C++ EventLogModel)이고, 최신 이벤트가 항상 맨 위(row 0).
// 평상시(SAFE + 예외없음) 이벤트는 애초에 C++ 쪽에서 안 쌓아서 여기 안 뜸.
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
                    text: model.distanceValid ? model.distanceM.toFixed(2) + " m" : "측정 불가"
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
