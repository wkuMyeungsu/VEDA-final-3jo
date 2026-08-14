import QtQuick
import QtQuick.Controls
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
    color: Theme.colorBackground

    // 그리드 카드/경보/카메라 상태 목록 어디서 클릭하든 여기로 모여서 처리.
    // 이미 확대뷰인 상태에서 다른 카메라를 고르면(경보 클릭 등) push 대신
    // replace -- 스택이 계속 쌓이는 것 방지
    function showCamera(cameraId) {
        if (cameraStack.depth > 1)
            cameraStack.replace(expandedComponent, { cameraId: cameraId }, StackView.Immediate)
        else
            cameraStack.push(expandedComponent, { cameraId: cameraId })
    }

    Shortcut {
        sequence: "Ctrl+Shift+D"
        enabled: demoController.demoModeEnabled && authService.loggedIn && authService.currentRole === "supervisor"
        onActivated: demoPanel.visible = !demoPanel.visible
    }

    // 확대뷰에서 그리드로 키보드 한 번에 복귀 (뒤로가기 버튼과 별개 경로)
    Shortcut {
        sequence: "Escape"
        onActivated: if (cameraStack.depth > 1) cameraStack.pop()
    }

    // 그리드 방향키 이동 + Enter 확대.
    // GridView에 Keys 핸들러를 다는 정석 방식이 StackView 안에서 포커스가
    // 안 내려와 동작하지 않아, 창 레벨 Shortcut으로 처리.
    // gridShowing일 때만 활성화 -- 확대뷰/데모패널에서 방향키를 뺏지 않도록
    readonly property bool gridShowing: cameraStack.depth === 1 && !demoPanel.visible && authService.loggedIn

    Shortcut {
        sequence: "Left"
        enabled: window.gridShowing
        onActivated: cameraStack.currentItem.moveLeft()
    }
    Shortcut {
        sequence: "Right"
        enabled: window.gridShowing
        onActivated: cameraStack.currentItem.moveRight()
    }
    Shortcut {
        sequence: "Up"
        enabled: window.gridShowing
        onActivated: cameraStack.currentItem.moveUp()
    }
    Shortcut {
        sequence: "Down"
        enabled: window.gridShowing
        onActivated: cameraStack.currentItem.moveDown()
    }
    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: window.gridShowing
        onActivated: cameraStack.currentItem.activateCurrent()
    }

    AppBar {
        id: appBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.appBarHeight
    }

    Item {
        id: mainArea
        anchors.top: appBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: eventLogPanel.top
        anchors.margins: Theme.spacingMd

        // 그리드 <-> 확대뷰 전환을 visible 토글 대신 StackView로 처리.
        // 부수 효과 둘: 기본 슬라이드 전환 모션이 공짜로 생기고, 확대뷰가
        // push/replace마다 새로 생성돼 Component.onCompleted가 항상 최신
        // cameraId로 실행됨(이전엔 1회만 생성+visible 토글이라 카메라 전환 시
        // 이전 카메라 데이터가 잠깐 남아있던 결함이 있었음)
        StackView {
            id: cameraStack
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: rightPanel.left
            anchors.rightMargin: Theme.spacingMd
            initialItem: gridComponent
            // pop/push 후 새 화면이 키 입력을 받도록 포커스 재부여
            onCurrentItemChanged: if (currentItem) currentItem.forceActiveFocus()
        }

        RightPanel {
            id: rightPanel
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            width: Theme.rightPanelWidth
            onCameraFocusRequested: (cameraId) => window.showCamera(cameraId)
        }
    }

    property int layoutMode: 0 // 0: Grid (2x2/NxN), 1: 1+(N-1) Focus

    onLayoutModeChanged: {
        if (cameraStack.depth === 1) {
            cameraStack.replace(layoutMode === 0 ? gridComponent : focusComponent, StackView.Immediate)
        }
    }

    Component {
        id: gridComponent
        CameraGrid {
            onCameraSelected: (cameraId) => window.showCamera(cameraId)
        }
    }

    Component {
        id: focusComponent
        FocusLayoutView {
            onCameraSelected: (cameraId) => window.showCamera(cameraId)
        }
    }

    Component {
        id: expandedComponent
        ExpandedCameraView {}
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
        anchors.bottom: eventLogPanel.top
        anchors.right: parent.right
        width: 320
        visible: false
    }

    LoginOverlay {
        anchors.fill: parent
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
