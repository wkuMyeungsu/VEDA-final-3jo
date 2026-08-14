import QtQuick
import Safety.Common

// Small icon + text row, reused wherever a labeled value needs a glyph
// (status rows, detail panels) instead of repeating a Row+Text pair.
Row {
    id: root
    property string icon: ""
    property string text: ""
    property color textColor: Theme.colorTextSecondary
    property int pixelSize: Theme.typeBody.size

    spacing: Theme.spacingXs

    Text {
        visible: root.icon.length > 0
        text: root.icon
        color: root.textColor
        font.pixelSize: root.pixelSize
    }
    Text {
        text: root.text
        color: root.textColor
        font.pixelSize: root.pixelSize
    }
}
