import QtQuick
import Safety.Common

// Small pill showing an icon + text in a given accent color. Used for
// connection state and risk level tags wherever a compact indicator is
// needed. Color is never the only signal -- text (and usually an icon)
// always accompanies it.
Rectangle {
    id: root
    property string text: ""
    property string icon: ""
    property color accentColor: Theme.colorTextMuted

    radius: height / 2
    color: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.16)
    border.color: accentColor
    border.width: 1
    implicitHeight: row.implicitHeight + Theme.spacingSm
    implicitWidth: row.implicitWidth + Theme.spacingMd * 2

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.spacingXs

        Text {
            visible: root.icon.length > 0
            text: root.icon
            color: root.accentColor
            font.pixelSize: Theme.fontSizeSm
        }
        Text {
            text: root.text
            color: root.accentColor
            font.pixelSize: Theme.fontSizeSm
            font.bold: true
        }
    }
}
