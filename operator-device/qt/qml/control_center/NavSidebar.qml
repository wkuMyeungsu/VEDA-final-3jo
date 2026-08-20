import QtQuick
import QtQuick.Controls
import Safety.Common

// PyDracula 스타일 애니메이션 좌측 사이드바
// 햄버거 토글 시 너비(56px <-> 200px)가 부드럽게 전환되며,
// 구역/카메라 뷰 및 로그 패널 전환 인터페이스를 제공합니다.
Rectangle {
    id: root

    property bool isExpanded: false
    property int selectedIndex: 0 // 0: 구역 목록, 1: 카메라 그리드

    signal zoneOverviewRequested()
    signal cameraGridRequested()
    signal eventLogToggled()
    signal helpRequested()

    width: isExpanded ? 200 : 56
    color: Theme.colorSurfaceGlass
    border.color: Theme.colorBorder
    border.width: 1
    radius: Theme.radiusMd
    clip: true

    Behavior on width {
        NumberAnimation {
            duration: 220
            easing.type: Easing.InOutCubic
        }
    }

    Column {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 6

        // 1. 햄버거 메뉴 토글 버튼
        Rectangle {
            width: parent.width
            height: 40
            radius: Theme.radiusSm
            color: toggleMouse.containsMouse ? Theme.colorHoverOverlay : "transparent"

            Row {
                anchors.fill: parent
                spacing: 8

                Item {
                    width: 44
                    height: parent.height

                    Text {
                        anchors.centerIn: parent
                        text: "☰"
                        color: Theme.colorAccent
                        font.pixelSize: 15
                        font.weight: Font.Bold
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "메뉴 축소"
                    color: Theme.colorTextSecondary
                    font.pixelSize: Theme.typeLabel.size
                    font.weight: Font.DemiBold
                    visible: root.isExpanded
                    opacity: root.isExpanded ? 1.0 : 0.0
                    Behavior on opacity {
                        NumberAnimation { duration: 150 }
                    }
                }
            }

            MouseArea {
                id: toggleMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.isExpanded = !root.isExpanded
            }
        }

        // 구분선
        Rectangle {
            width: parent.width
            height: 1
            color: Theme.colorBorder
        }

        // 2. 내비게이션 아이템 목록
        // 아이템 1: 구역 목록 / 모니터링
        NavButton {
            width: parent.width
            iconBadge: "ZN"
            labelText: "구역 모니터링"
            isSelected: root.selectedIndex === 0
            isExpanded: root.isExpanded
            onClicked: {
                root.selectedIndex = 0
                root.zoneOverviewRequested()
            }
        }

        // 아이템 2: 카메라 그리드
        NavButton {
            width: parent.width
            iconBadge: "GD"
            labelText: "카메라 그리드"
            isSelected: root.selectedIndex === 1
            isExpanded: root.isExpanded
            onClicked: {
                root.selectedIndex = 1
                root.cameraGridRequested()
            }
        }

        // 구분선
        Rectangle {
            width: parent.width
            height: 1
            color: Theme.colorBorder
        }

        // 아이템 3: 이벤트 로그 토글
        NavButton {
            width: parent.width
            iconBadge: "LOG"
            labelText: "감사 로그 패널"
            isSelected: false
            isExpanded: root.isExpanded
            onClicked: root.eventLogToggled()
        }

        // 아이템 4: F1 도움말 / 가이드 & 비상 대응 SOP
        NavButton {
            width: parent.width
            iconBadge: "?"
            labelText: "도움말 / SOP (F1)"
            isSelected: false
            isExpanded: root.isExpanded
            onClicked: root.helpRequested()
        }
    }

    // 내부 NavButton 컴포넌트
    component NavButton: Rectangle {
        id: btn
        property string iconBadge: ""
        property string labelText: ""
        property bool isSelected: false
        property bool isExpanded: false
        signal clicked()

        height: 40
        radius: Theme.radiusSm
        color: isSelected 
               ? Theme.colorAccentAlpha20 
               : (btnMouse.containsMouse ? Theme.colorHoverOverlay : "transparent")
        border.width: isSelected ? 1 : 0
        border.color: Theme.colorAccent

        // 선택 시 좌측 액센트 바
        Rectangle {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 3
            height: 20
            radius: 2
            color: Theme.colorAccent
            visible: btn.isSelected
        }

        Row {
            anchors.fill: parent
            spacing: 8

            // 고정폭 아이콘 영역 (축소 상태에서도 완벽하게 중앙 정렬)
            Item {
                width: 44
                height: parent.height

                Rectangle {
                    anchors.centerIn: parent
                    width: btn.iconBadge.length > 2 ? 32 : 24
                    height: 22
                    radius: Theme.radiusXs
                    color: btn.isSelected ? Theme.colorAccent : Qt.rgba(1, 1, 1, 0.06)
                    border.width: 1
                    border.color: btn.isSelected ? Theme.colorAccent : Theme.colorBorder

                    Text {
                        anchors.centerIn: parent
                        text: btn.iconBadge
                        font.pixelSize: btn.iconBadge === "?" ? 13 : 10
                        font.weight: Font.Bold
                        color: btn.isSelected ? "#ffffff" : Theme.colorTextSecondary
                    }
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: btn.labelText
                font.pixelSize: Theme.typeLabel.size
                font.weight: btn.isSelected ? Font.Bold : Font.Normal
                color: btn.isSelected ? Theme.colorTextPrimary : Theme.colorTextSecondary
                visible: btn.isExpanded
                opacity: btn.isExpanded ? 1.0 : 0.0
                elide: Text.ElideRight
                Behavior on opacity {
                    NumberAnimation { duration: 150 }
                }
            }
        }

        MouseArea {
            id: btnMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: btn.clicked()
        }
    }
}
