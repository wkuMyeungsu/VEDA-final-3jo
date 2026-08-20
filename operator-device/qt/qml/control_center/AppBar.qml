import QtQuick
import Safety.Common

// Top app bar: system name, live clock, server connection badge.
Rectangle {
    id: root
    color: Theme.colorBackground

    signal snapshotRequested()
    signal sopRequested()

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.borderWidthHairline
        color: Theme.colorBorder
    }

    Text {
        id: systemNameText
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Theme.spacingLg
        text: systemName
        color: Theme.colorTextPrimary
        font.pixelSize: Theme.typeHeading.size
        font.weight: Font.DemiBold
    }

    // 상단 퀵 액션 버튼 바: 화면 캡처 & 도움말 / 비상 SOP (F1)
    Row {
        z: 10
        anchors.left: systemNameText.right
        anchors.leftMargin: Theme.spacingLg
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8
        visible: authService.loggedIn

        // 즉시 스냅샷 캡처 버튼
        Rectangle {
            implicitWidth: snapRow.implicitWidth + 20
            height: 32
            radius: Theme.radiusSm
            color: btnSnapMouse.containsMouse ? Theme.colorHoverOverlay : Qt.rgba(1, 1, 1, 0.05)
            border.width: 1
            border.color: btnSnapMouse.containsMouse ? Theme.colorAccent : Theme.colorBorder

            Row {
                id: snapRow
                anchors.centerIn: parent
                spacing: 6

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "[CAPT]"
                    color: Theme.colorAccent
                    font.pixelSize: 11
                    font.weight: Font.Bold
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "화면 캡처"
                    color: btnSnapMouse.containsMouse ? Theme.colorAccent : Theme.colorTextPrimary
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
            }

            MouseArea {
                id: btnSnapMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.snapshotRequested()
            }
        }

        // F1 도움말 & 비상 대응 SOP 버튼
        Rectangle {
            implicitWidth: sopRow.implicitWidth + 20
            height: 32
            radius: Theme.radiusSm
            color: btnSopMouse.containsMouse ? Theme.colorAccentAlpha20 : Qt.rgba(0.95, 0.45, 0.13, 0.12)
            border.width: 1
            border.color: Theme.colorAccent

            Row {
                id: sopRow
                anchors.centerIn: parent
                spacing: 6

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "[HELP]"
                    color: Theme.colorAccent
                    font.pixelSize: 11
                    font.weight: Font.Bold
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "도움말 / SOP (F1)"
                    color: Theme.colorAccent
                    font.pixelSize: 12
                    font.weight: Font.Bold
                }
            }

            MouseArea {
                id: btnSopMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.sopRequested()
            }
        }
    }

    Text {
        id: clockText
        anchors.centerIn: parent
        color: Theme.colorTextSecondary
        font.pixelSize: Theme.typeCaption.size
        font.weight: Font.Normal
        text: Qt.formatDateTime(new Date(), "yyyy-MM-dd HH:mm:ss")

        Timer {
            interval: 1000
            running: true
            repeat: true
            onTriggered: clockText.text = Qt.formatDateTime(new Date(), "yyyy-MM-dd HH:mm:ss")
        }
    }

    // 창 이동 드래그 및 더블클릭 최대화/복원 핸들러
    MouseArea {
        anchors.fill: parent
        z: -1
        onPressed: (mouse) => {
            if (mouse.button === Qt.LeftButton) {
                window.startSystemMove()
            }
        }
        onDoubleClicked: {
            if (window.visibility === Window.Maximized) {
                window.showNormal()
            } else {
                window.showMaximized()
            }
        }
    }

    Row {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.rightMargin: 0
        spacing: Theme.spacingMd

        // 로그인한 운영자 정보 및 로그아웃 버튼 (로그인 상태에서만 노출)
        Row {
            id: operatorInfoRow
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingMd
            visible: authService.loggedIn

            // 역할 태그 뱃지
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                height: 22
                width: roleText.implicitWidth + Theme.spacingSm * 2
                radius: Theme.radiusSm
                color: authService.currentRole === "supervisor"
                       ? Qt.rgba(0.31, 0.55, 0.97, 0.2)
                       : Theme.colorSurfaceElevated
                border.width: Theme.borderWidthHairline
                border.color: authService.currentRole === "supervisor"
                              ? Theme.colorAccent
                              : Theme.colorBorder

                Text {
                    id: roleText
                    anchors.centerIn: parent
                    text: authService.currentRole === "supervisor" ? "감독관" : "조작자"
                    color: authService.currentRole === "supervisor"
                           ? Theme.colorAccent
                           : Theme.colorTextSecondary
                    font.pixelSize: Theme.typeCaption.size
                    font.bold: true
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: authService.currentOperatorName
                color: Theme.colorTextPrimary
                font.pixelSize: Theme.typeBody.size
                font.bold: true
            }

            Text {
                id: logoutButton
                anchors.verticalCenter: parent.verticalCenter
                text: "로그아웃"
                color: logoutArea.containsMouse ? Theme.colorAccent : Theme.colorTextMuted
                font.pixelSize: Theme.typeCaption.size
                font.underline: logoutArea.containsMouse

                MouseArea {
                    id: logoutArea
                    anchors.fill: parent
                    anchors.margins: -4
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: authService.logout()
                }
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 1
                height: 14
                color: Theme.colorBorder
            }
        }

        StatusBadge {
            anchors.verticalCenter: parent.verticalCenter
            text: "서버 " + Theme.connectionLabel(serverConnection.connectionState)
            icon: "●"
            accentColor: Theme.connectionColor(serverConnection.connectionState)
        }

        // PyDracula 스타일 윈도우 조작 버튼 그룹 (최소화, 최대화/복원, 닫기)
        Row {
            anchors.verticalCenter: parent.verticalCenter
            height: parent.height
            spacing: 2

            // 최소화 버튼
            Rectangle {
                width: 40
                height: parent.height
                color: minMouse.containsMouse ? Theme.colorHoverOverlay : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "🗕"
                    color: minMouse.containsMouse ? Theme.colorTextPrimary : Theme.colorTextSecondary
                    font.pixelSize: 12
                }

                MouseArea {
                    id: minMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: window.showMinimized()
                }
            }

            // 최대화 / 이전 크기로 복원 버튼
            Rectangle {
                width: 40
                height: parent.height
                color: maxMouse.containsMouse ? Theme.colorHoverOverlay : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: window.visibility === Window.Maximized ? "🗗" : "🗖"
                    color: maxMouse.containsMouse ? Theme.colorTextPrimary : Theme.colorTextSecondary
                    font.pixelSize: 12
                }

                MouseArea {
                    id: maxMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (window.visibility === Window.Maximized) {
                            window.showNormal()
                        } else {
                            window.showMaximized()
                        }
                    }
                }
            }

            // 닫기 버튼
            Rectangle {
                width: 44
                height: parent.height
                color: closeMouse.containsMouse ? "#EF4444" : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "✕"
                    color: closeMouse.containsMouse ? "#FFFFFF" : Theme.colorTextSecondary
                    font.pixelSize: 12
                }

                MouseArea {
                    id: closeMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: window.close()
                }
            }
        }
    }
}
