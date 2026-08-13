import QtQuick
import Safety.Common

// Top app bar: system name, live clock, server connection badge.
Rectangle {
    id: root
    color: Theme.colorBackground

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.borderWidthHairline
        color: Theme.colorBorder
    }

    Text {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Theme.spacingLg
        text: systemName
        color: Theme.colorTextPrimary
        font.pixelSize: Theme.typeHeading.size
        font.bold: true
    }

    Text {
        id: clockText
        anchors.centerIn: parent
        color: Theme.colorTextSecondary
        font.pixelSize: Theme.typeBody.size
        text: Qt.formatDateTime(new Date(), "yyyy-MM-dd HH:mm:ss")

        Timer {
            interval: 1000
            running: true
            repeat: true
            onTriggered: clockText.text = Qt.formatDateTime(new Date(), "yyyy-MM-dd HH:mm:ss")
        }
    }

    StatusBadge {
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.rightMargin: Theme.spacingLg
        text: "서버 " + Theme.connectionLabel(serverConnection.connectionState)
        icon: "●"
        accentColor: Theme.connectionColor(serverConnection.connectionState)
    }
}
