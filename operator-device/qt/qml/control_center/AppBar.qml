import QtQuick
import Safety.Common

// Top app bar: system name, live clock, server connection badge.
Rectangle {
    id: root
    color: Theme.colorBackground

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

    // N-카메라 레이아웃 전환 버튼 바
    Row {
        anchors.left: systemNameText.right
        anchors.leftMargin: Theme.spacingLg
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
        visible: authService.loggedIn

        Rectangle {
            width: 30
            height: 26
            radius: Theme.radiusXs
            color: window.layoutMode === 0 ? Theme.colorAccentAlpha20 : Qt.rgba(1, 1, 1, 0.03)
            border.width: window.layoutMode === 0 ? 1 : 0
            border.color: Theme.colorAccent

            Text {
                anchors.centerIn: parent
                text: "⊞"
                color: window.layoutMode === 0 ? Theme.colorAccent : Theme.colorTextMuted
                font.pixelSize: 13
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: window.layoutMode = 0
            }
        }

        Rectangle {
            width: 30
            height: 26
            radius: Theme.radiusXs
            color: window.layoutMode === 1 ? Theme.colorAccentAlpha20 : Qt.rgba(1, 1, 1, 0.03)
            border.width: window.layoutMode === 1 ? 1 : 0
            border.color: Theme.colorAccent

            Text {
                anchors.centerIn: parent
                text: "🗖"
                color: window.layoutMode === 1 ? Theme.colorAccent : Theme.colorTextMuted
                font.pixelSize: 13
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: window.layoutMode = 1
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

    Row {
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.rightMargin: Theme.spacingLg
        spacing: Theme.spacingLg

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
    }
}
