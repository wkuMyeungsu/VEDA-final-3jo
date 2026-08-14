import QtQuick
import Safety.Common

// Dot + label for a connection state (server / camera / sensor). Used in
// both apps' status strips/lists.
Row {
    id: root
    property string label: ""
    property int state: 0 // RiskTypes.ConnectionState
    // - true: 점 색상 + 라벨만 (연결 상태 영문 단어 생략), 좁은 공간용
    property bool compact: false

    spacing: root.compact ? Theme.spacingXs : Theme.spacingSm
    height: Math.max(dot.height, labelText.implicitHeight)

    Rectangle {
        id: dot
        width: Theme.connectionDotSize
        height: Theme.connectionDotSize
        radius: Theme.connectionDotSize / 2
        color: Theme.connectionColor(root.state)
        y: (root.height - height) / 2
    }

    Text {
        id: labelText
        text: root.compact ? root.label : (root.label + " · " + Theme.connectionLabel(root.state))
        color: Theme.colorTextSecondary
        font.pixelSize: Theme.typeCaption.size
        y: (root.height - implicitHeight) / 2
    }
}
