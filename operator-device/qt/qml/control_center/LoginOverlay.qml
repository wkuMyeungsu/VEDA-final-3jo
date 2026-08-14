import QtQuick
import QtQuick.Controls
import Safety.Common

// Full-window gate shown until authService.loggedIn. Sits above every other
// child of ControlCenterWindow (z above DemoPanel/EventLogPanel) and is
// fully opaque, so nothing behind it is visible or clickable while it's up.
// ControlCenterWindow.qml also guards its keyboard Shortcuts on
// authService.loggedIn, so grid navigation can't be driven from behind it.
Item {
    id: root
    visible: !authService.loggedIn

    // 뒤 화면으로 클릭/휠이 새지 않게 전체를 먹는 불투명 배경
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onWheel: (wheel) => wheel.accepted = true
    }

    // 이 화면 밖에서는 QtQuick.Controls를 안 쓰므로(DemoPanel과 동일한 이유),
    // Theme 토큰을 그대로 따르도록 TextField/Button을 직접 재스타일함
    component StyledTextField: TextField {
        id: control
        implicitHeight: Theme.statusRowHeight
        color: Theme.colorTextPrimary
        placeholderTextColor: Theme.colorTextMuted
        font.pixelSize: Theme.typeBody.size
        selectByMouse: true

        background: Rectangle {
            radius: Theme.radiusSm
            color: Theme.colorSurface
            border.width: Theme.borderWidthHairline
            border.color: control.activeFocus ? Theme.colorAccent : Theme.colorBorder
        }
    }

    component StyledButton: Button {
        id: control

        contentItem: Text {
            text: control.text
            color: Theme.colorTextPrimary
            font.pixelSize: Theme.typeBody.size
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.WordWrap
        }

        background: Rectangle {
            radius: Theme.radiusSm
            color: control.down ? Theme.colorSurfaceSunken : (control.enabled ? Theme.colorAccent : Theme.colorSurface)
            border.width: Theme.borderWidthHairline
            border.color: control.enabled ? Theme.colorAccent : Theme.colorBorder
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.colorBackground
    }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: 360
        height: content.implicitHeight + Theme.spacingXl * 2
        radius: Theme.radiusLg
        color: Theme.colorSurfaceElevated
        border.width: Theme.borderWidthHairline
        border.color: Theme.colorBorderStrong

        Column {
            id: content
            anchors.centerIn: parent
            width: parent.width - Theme.spacingXl * 2
            spacing: Theme.spacingMd

            Text {
                width: parent.width
                text: systemName
                color: Theme.colorTextPrimary
                font.pixelSize: Theme.typeHeading.size
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }

            Text {
                width: parent.width
                text: "운영자 로그인"
                color: Theme.colorTextSecondary
                font.pixelSize: Theme.typeBody.size
                horizontalAlignment: Text.AlignHCenter
            }

            Item { width: 1; height: Theme.spacingXs }

            Text {
                id: noOperatorsWarning
                width: parent.width
                color: Theme.colorCaution
                font.pixelSize: Theme.typeCaption.size
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: "등록된 운영자 계정이 없습니다.\nconfig/operators.json 설정을 확인하세요."
                visible: !authService.hasOperators
            }

            StyledTextField {
                id: idField
                width: parent.width
                placeholderText: "운영자 ID"
                enabled: !authService.locked && authService.hasOperators
                onAccepted: pinField.forceActiveFocus()
            }

            StyledTextField {
                id: pinField
                width: parent.width
                placeholderText: "PIN"
                echoMode: TextInput.Password
                enabled: !authService.locked && authService.hasOperators
                onAccepted: loginButton.clicked()
            }

            Text {
                id: errorText
                width: parent.width
                color: Theme.colorDanger
                font.pixelSize: Theme.typeCaption.size
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: text.length > 0
                height: visible ? implicitHeight : 0
            }

            StyledButton {
                id: loginButton
                width: parent.width
                enabled: !authService.locked && authService.hasOperators
                text: !authService.hasOperators
                      ? "계정 설정 필요"
                      : (authService.locked
                         ? ("잠금 · " + authService.lockRemainingSeconds + "초 후 재시도")
                         : "로그인")
                onClicked: {
                    if (authService.login(idField.text, pinField.text)) {
                        idField.text = ""
                        pinField.text = ""
                        errorText.text = ""
                    } else {
                        pinField.text = ""
                        pinField.forceActiveFocus()
                    }
                }
            }
        }
    }

    Connections {
        target: authService
        function onLoginFailed(reason) { errorText.text = reason }
    }

    // 잠금 카운트다운 문구(버튼 텍스트)가 매초 갱신되도록
    Connections {
        target: authService
        function onLockRemainingSecondsChanged() {}
    }

    function resetFields() {
        idField.text = ""
        pinField.text = ""
        errorText.text = ""
        if (visible && authService.hasOperators)
            idField.forceActiveFocus()
    }

    onVisibleChanged: if (visible) resetFields()
    Component.onCompleted: if (visible) resetFields()
}
