import QtQuick
import Safety.Common

// Dot + label for a connection state (server / camera / sensor). Used in
// both apps' status strips/lists.
Row {
    id: root
    property string label: ""
    property int state: 0 // RiskTypes.ConnectionState

    spacing: Theme.spacingSm
    height: Math.max(dot.height, labelText.implicitHeight)

    Rectangle {
        id: dot
        width: 10
        height: 10
        radius: 5
        color: Theme.connectionColor(root.state)
        y: (root.height - height) / 2
    }

    Text {
        id: labelText
        text: root.label + " · " + Theme.connectionLabel(root.state)
        color: Theme.colorTextSecondary
        font.pixelSize: Theme.fontSizeSm
        y: (root.height - implicitHeight) / 2
    }
}
