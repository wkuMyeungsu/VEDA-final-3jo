import QtQuick
import Safety.Common

// Card container with a consistent surface color, border, radius and
// padding, plus an optional small caps title. Used for every panel in the
// control-center right/bottom areas.
Rectangle {
    id: root
    default property alias content: contentArea.data
    property string title: ""

    color: Theme.colorSurface
    radius: Theme.radiusMd
    border.color: Theme.colorBorder
    border.width: 1

    Text {
        id: titleText
        visible: root.title.length > 0
        text: root.title
        color: Theme.colorTextSecondary
        font.pixelSize: Theme.fontSizeSm
        font.bold: true
        font.letterSpacing: 1
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingMd
    }

    Item {
        id: contentArea
        anchors.top: root.title.length > 0 ? titleText.bottom : parent.top
        anchors.topMargin: root.title.length > 0 ? Theme.spacingSm : Theme.spacingMd
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.spacingMd
        anchors.rightMargin: Theme.spacingMd
        anchors.bottomMargin: Theme.spacingMd
    }
}
