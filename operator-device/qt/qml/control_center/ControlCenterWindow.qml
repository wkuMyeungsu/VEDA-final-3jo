import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Safety.Common

// Root window: app bar, camera grid / expanded view, right status panel,
// bottom event log, plus a demo panel reachable only via Ctrl+Shift+D and
// only when the app was launched with --demo.
ApplicationWindow {
    id: window
    visible: true
    width: 1440
    height: 900
    minimumWidth: 1000
    minimumHeight: 680
    title: systemName
    flags: Qt.FramelessWindowHint | Qt.Window
    color: "transparent"

    property int layoutMode: 0 // 0: N-카메라 그리드, 1: 1+(N-1) 포커스 레이아웃
    property bool rightPanelCollapsed: false
    property string toastMessage: ""

    function takeSnapshot() {
        var savedName = authService.takeSnapshot(window)
        if (savedName && savedName.length > 0) {
            window.toastMessage = "화면 캡처 완료 (" + savedName + ")"
            toastTimer.restart()
        }
    }

    onLayoutModeChanged: {
        if (layoutMode === 0) {
            cameraStack.pop(null)
            cameraStack.push(cameraOverviewComponent, { zoneId: "ZONE_A" })
            cameraStack.push(gridComponent, { cameraId: "CAM_01" })
        } else if (layoutMode === 1) {
            cameraStack.pop(null)
            cameraStack.push(focusLayoutComponent)
        }
    }

    Shortcut {
        sequence: "F1"
        onActivated: helpModal.toggle()
    }

    Shortcut {
        sequence: "Ctrl+Shift+D"
        enabled: demoController.demoModeEnabled && authService.loggedIn && authService.currentRole === "supervisor"
        onActivated: demoPanel.visible = !demoPanel.visible
    }

    // 확대뷰에서 그리드로 키보드 한 번에 복귀 (뒤로가기 버튼과 별개 경로)
    Shortcut {
        sequence: "Escape"
        onActivated: {
            if (helpModal.visible) helpModal.close()
            else if (cameraStack.depth > 1) cameraStack.pop()
        }
    }

    // 키보드 방향키 이동 + Enter 확대 (모든 드릴다운 뷰에서 유기적으로 동작)
    readonly property bool canNavigate: !demoPanel.visible && !helpModal.visible && authService.loggedIn && cameraStack.currentItem !== null

    Shortcut {
        sequence: "Left"
        enabled: window.canNavigate && typeof cameraStack.currentItem.moveLeft === "function"
        onActivated: cameraStack.currentItem.moveLeft()
    }
    Shortcut {
        sequence: "Right"
        enabled: window.canNavigate && typeof cameraStack.currentItem.moveRight === "function"
        onActivated: cameraStack.currentItem.moveRight()
    }
    Shortcut {
        sequence: "Up"
        enabled: window.canNavigate && typeof cameraStack.currentItem.moveUp === "function"
        onActivated: cameraStack.currentItem.moveUp()
    }
    Shortcut {
        sequence: "Down"
        enabled: window.canNavigate && typeof cameraStack.currentItem.moveDown === "function"
        onActivated: cameraStack.currentItem.moveDown()
    }
    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: window.canNavigate && typeof cameraStack.currentItem.activateCurrent === "function"
        onActivated: cameraStack.currentItem.activateCurrent()
    }

    // 전체 글래스모피즘 메인 컨테이너
    Rectangle {
        id: windowContainer
        anchors.fill: parent
        color: Theme.colorBackground
        border.color: Theme.colorBorder
        border.width: 1
        radius: Theme.radiusMd
        clip: true

        AppBar {
            id: appBar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: Theme.appBarHeight

            onSnapshotRequested: window.takeSnapshot()
            onSopRequested: helpModal.open()
        }

        Item {
            id: mainArea
            anchors.top: appBar.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: eventLogPanel.visible ? eventLogPanel.top : parent.bottom
            anchors.margins: Theme.spacingMd

            // PyDracula 스타일 애니메이션 좌측 사이드바
            NavSidebar {
                id: navSidebar
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                visible: authService.loggedIn

                onZoneOverviewRequested: {
                    cameraStack.pop(null)
                }
                onCameraGridRequested: {
                    cameraStack.pop(null)
                    cameraStack.push(cameraOverviewComponent, { zoneId: "ZONE_A" })
                    cameraStack.push(gridComponent, { cameraId: "CAM_01" })
                }
                onEventLogToggled: {
                    eventLogPanel.visible = !eventLogPanel.visible
                }
                onHelpRequested: {
                    helpModal.open()
                }
            }

            // 그리드 <-> 확대뷰 전환을 visible 토글 대신 StackView로 처리.
            StackView {
                id: cameraStack
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.left: navSidebar.visible ? navSidebar.right : parent.left
                anchors.leftMargin: navSidebar.visible ? Theme.spacingMd : 0
                anchors.right: window.rightPanelCollapsed ? parent.right : rightPanel.left
                anchors.rightMargin: window.rightPanelCollapsed ? 0 : Theme.spacingMd
                initialItem: zoneListComponent
                onCurrentItemChanged: if (currentItem) currentItem.forceActiveFocus()
            }

            // 계층 연동형 동적 우측 분석 패널
            RightPanel {
                id: rightPanel
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                isCollapsed: window.rightPanelCollapsed
                width: window.rightPanelCollapsed ? 0 : Theme.rightPanelWidth
                navDepth: cameraStack.depth
                selectedZoneId: (cameraStack.currentItem && cameraStack.currentItem.zoneId) ? cameraStack.currentItem.zoneId : "ZONE_A"
                selectedCameraId: (cameraStack.currentItem && cameraStack.currentItem.cameraId) ? cameraStack.currentItem.cameraId : "CAM_01"
                selectedStreamId: (cameraStack.currentItem && cameraStack.currentItem.cameraId) ? cameraStack.currentItem.cameraId : "CAM_01_CH_01"
                onCameraFocusRequested: (targetId) => window.showCamera(targetId)
                onToggleCollapse: window.rightPanelCollapsed = !window.rightPanelCollapsed
            }

            // 우측 패널 접혔을 때 펼치기 플로팅 버튼
            Rectangle {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 26
                height: 54
                radius: Theme.radiusXs
                color: Theme.colorSurfaceElevated
                border.width: 1
                border.color: Theme.colorAccent
                visible: window.rightPanelCollapsed
                z: 30

                Text {
                    anchors.centerIn: parent
                    text: "<"
                    color: Theme.colorAccent
                    font.pixelSize: 13
                    font.weight: Font.Bold
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: window.rightPanelCollapsed = false
                }
            }
        }

        EventLogPanel {
            id: eventLogPanel
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Theme.eventLogHeight
        }

        DemoPanel {
            id: demoPanel
            anchors.top: appBar.bottom
            anchors.bottom: eventLogPanel.visible ? eventLogPanel.top : parent.bottom
            anchors.right: parent.right
            width: 320
            visible: false
        }

        // F1 통합 도움말 / 비상 대응 SOP / 상태 코드 가이드 모달
        HelpModal {
            id: helpModal
        }

        // 스냅샷 캡처 완료 플로팅 알림 (폴더 열기 버튼 포함)
        Rectangle {
            id: toastBox
            anchors.top: parent.top
            anchors.topMargin: Theme.appBarHeight + 12
            anchors.horizontalCenter: parent.horizontalCenter
            implicitWidth: toastRow.implicitWidth + 32
            height: 48
            radius: Theme.radiusSm
            color: Theme.colorSurfaceElevated
            border.width: 1
            border.color: Theme.colorAccent
            opacity: window.toastMessage.length > 0 ? 1.0 : 0.0
            visible: opacity > 0
            z: 200

            Behavior on opacity { NumberAnimation { duration: 200 } }

            RowLayout {
                id: toastRow
                anchors.centerIn: parent
                spacing: 14

                Rectangle {
                    width: 52
                    height: 24
                    radius: 4
                    color: Theme.colorAccentAlpha20
                    border.width: 1
                    border.color: Theme.colorAccent
                    Text { anchors.centerIn: parent; text: "CAPT"; color: Theme.colorAccent; font.pixelSize: 11; font.weight: Font.Bold }
                }

                Text {
                    id: toastText
                    text: window.toastMessage
                    color: Theme.colorTextPrimary
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }

                Rectangle {
                    width: 88
                    height: 30
                    radius: Theme.radiusXs
                    color: btnFolderMouse.containsMouse ? Qt.lighter(Theme.colorAccent, 1.15) : Theme.colorAccent

                    Text {
                        anchors.centerIn: parent
                        text: "폴더 열기"
                        color: "#ffffff"
                        font.pixelSize: 12
                        font.weight: Font.Bold
                    }

                    MouseArea {
                        id: btnFolderMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            authService.openCapturesFolder()
                        }
                    }
                }
            }

            Timer {
                id: toastTimer
                interval: 8000
                onTriggered: window.toastMessage = ""
            }
        }

        LoginOverlay {
            anchors.fill: parent
        }
    }

    Component {
        id: zoneListComponent
        ZoneListView {
            onZoneSelected: (zoneId) => {
                cameraStack.push(cameraOverviewComponent, { zoneId: zoneId })
            }
        }
    }

    Component {
        id: cameraOverviewComponent
        CameraOverviewView {
            onCameraSelected: (cameraId) => {
                cameraStack.push(gridComponent, { cameraId: cameraId })
            }
            onBackRequested: cameraStack.pop()
        }
    }

    Component {
        id: gridComponent
        CameraGrid {
            onCameraSelected: (cameraId) => window.showExpanded(cameraId)
            onBackRequested: cameraStack.pop()
        }
    }

    Component {
        id: expandedComponent
        ExpandedCameraView {}
    }

    Component {
        id: focusLayoutComponent
        FocusLayoutView {
            onCameraSelected: (cameraId) => window.showExpanded(cameraId)
        }
    }

    function showExpanded(cameraId) {
        cameraStack.push(expandedComponent, { cameraId: cameraId })
    }

    function showCamera(targetId) {
        if (targetId.startsWith("ZONE_")) {
            cameraStack.pop(null)
            cameraStack.push(cameraOverviewComponent, { zoneId: targetId })
        } else if (targetId === "CAM_01") {
            cameraStack.pop(null)
            cameraStack.push(cameraOverviewComponent, { zoneId: "ZONE_A" })
            cameraStack.push(gridComponent, { cameraId: targetId })
        } else {
            cameraStack.pop(null)
            cameraStack.push(cameraOverviewComponent, { zoneId: "ZONE_A" })
            cameraStack.push(gridComponent, { cameraId: "CAM_01" })
            cameraStack.push(expandedComponent, { cameraId: targetId })
        }
    }

    Connections {
        target: authService
        function onLoggedInChanged() {
            if (!authService.loggedIn) {
                demoPanel.visible = false
                if (cameraStack.depth > 1)
                    cameraStack.pop()
            }
        }
    }
}
