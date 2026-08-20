import QtQuick
import QtQuick.Layouts
import Safety.Common

// Sleek Dark Glass Top Bar for Forklift Terminal:
// Displays Zone, Camera Name, Active Channel (1~4) Indicator, and Live System Clock.
Rectangle {
    id: root
    height: 44
    color: Qt.rgba(0.06, 0.08, 0.12, 0.85)
    border.width: 1
    border.color: Theme.colorBorder

    readonly property int activeChannelIndex: {
        var id = activeCamera.activeCameraId
        if (id.indexOf("CH_01") !== -1 || id.indexOf("CH_1") !== -1 || id === "CAM_01") return 1
        if (id.indexOf("CH_02") !== -1 || id.indexOf("CH_2") !== -1 || id === "CAM_02") return 2
        if (id.indexOf("CH_03") !== -1 || id.indexOf("CH_3") !== -1 || id === "CAM_03") return 3
        if (id.indexOf("CH_04") !== -1 || id.indexOf("CH_4") !== -1 || id === "CAM_04") return 4
        return 1
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingLg
        anchors.rightMargin: Theme.spacingLg
        spacing: 12

        // 1) Zone 태그 뱃지
        Rectangle {
            implicitWidth: zoneText.implicitWidth + 16
            height: 24
            radius: 4
            color: Theme.colorAccentAlpha20
            border.width: 1
            border.color: Theme.colorAccent

            Text {
                id: zoneText
                anchors.centerIn: parent
                text: activeCamera.zone.length > 0 ? activeCamera.zone : "ZONE_A"
                color: Theme.colorAccent
                font.pixelSize: 11
                font.bold: true
            }
        }

        // 2) 구역 및 카메라 명칭
        Text {
            text: (activeCamera.cameraName.length > 0 ? activeCamera.cameraName : "메인 하역장") + " (" + activeCamera.activeCameraId + ")"
            color: Theme.colorTextPrimary
            font.pixelSize: 13
            font.bold: true
        }

        Item { Layout.fillWidth: true }

        // 3) 4채널 멀티센서 인디케이터 (CH 01 ~ 04)
        RowLayout {
            spacing: 8

            Rectangle {
                implicitWidth: chText.implicitWidth + 12
                height: 22
                radius: 3
                color: Qt.rgba(1, 1, 1, 0.08)
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.15)

                Text {
                    id: chText
                    anchors.centerIn: parent
                    text: "CH 0" + root.activeChannelIndex
                    color: Theme.colorTextSecondary
                    font.pixelSize: 11
                    font.bold: true
                }
            }

            // 4채널 미니 도트 (● ○ ○ ○)
            Row {
                spacing: 4
                Repeater {
                    model: 4
                    Rectangle {
                        width: 6
                        height: 6
                        radius: 3
                        color: (index + 1 === root.activeChannelIndex) ? Theme.colorAccent : Qt.rgba(1, 1, 1, 0.25)
                    }
                }
            }
        }

        // 4) 실시간 시계
        Text {
            id: clockText
            color: Theme.colorTextMuted
            font.pixelSize: 12
            text: Qt.formatDateTime(new Date(), "HH:mm:ss")

            Timer {
                interval: 1000
                running: true
                repeat: true
                onTriggered: clockText.text = Qt.formatDateTime(new Date(), "HH:mm:ss")
            }
        }
    }
}
