import QtQuick
import Safety.Common

// 카드/리스트 행 위에 얹는 hover·press 피드백 + 클릭 처리 공용 컴포넌트.
// 각 화면이 MouseArea를 따로 만들던 걸 하나로 통일 (파이는 출력 전용이라 대상 아님).
MouseArea {
    id: root
    hoverEnabled: true
    cursorShape: Qt.PointingHandCursor
    property real overlayRadius: 0

    Rectangle {
        anchors.fill: parent
        radius: root.overlayRadius
        color: root.pressed ? Theme.colorPressOverlay
             : root.containsMouse ? Theme.colorHoverOverlay : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.animationFast } }
    }
}
