import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Safety.Common

// F1 도움말 / 비상 대응 SOP / 상태 코드 가이드 통합 모달
Rectangle {
    id: root
    anchors.fill: parent
    color: Qt.rgba(0, 0, 0, 0.75)
    visible: false
    z: 100

    property int currentTab: 0 // 0: 비상 SOP 및 긴급 연락망, 1: 상태/경보 코드 가이드, 2: 키보드 단축키
    property string contactManager: "010-1234-5678"
    property string contactControlRoom: "내선 1199"
    property string contactEmergency: "119"
    property string contactMemo: "안전관리책임자: 홍길동 팀장 (주간/야간 상시 대기)"
    property bool isEditingContacts: false

    function open() {
        root.visible = true
    }

    function close() {
        root.visible = false
        root.isEditingContacts = false
    }

    function toggle() {
        if (root.visible) root.close()
        else root.open()
    }

    // 배경 클릭 시 닫기
    MouseArea {
        anchors.fill: parent
        onClicked: root.close()
    }

    // 중앙 모달 컨테이너
    Rectangle {
        id: modalBox
        width: Math.min(760, parent.width - 40)
        height: Math.min(620, parent.height - 40)
        anchors.centerIn: parent
        radius: Theme.radiusLg
        color: Theme.colorSurfaceElevated
        border.width: 1
        border.color: Theme.colorBorderStrong
        clip: true

        MouseArea {
            anchors.fill: parent
            onClicked: {} // 이벤트 전파 방지
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 16

            // 1. 헤더 영역 (타이틀 + 닫기)
            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Rectangle {
                    width: 38
                    height: 38
                    radius: Theme.radiusSm
                    color: Theme.colorAccentAlpha20
                    border.width: 1
                    border.color: Theme.colorAccent

                    Text {
                        anchors.centerIn: parent
                        text: "HELP"
                        color: Theme.colorAccent
                        font.pixelSize: 11
                        font.weight: Font.Bold
                    }
                }

                Column {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: "통합 관제 가이드 및 비상 대응 시스템"
                        color: Theme.colorTextPrimary
                        font.pixelSize: 17
                        font.weight: Font.Bold
                    }
                    Text {
                        text: "단축키 F1을 눌러 언제든지 본 도움말을 열고 닫을 수 있습니다."
                        color: Theme.colorTextSecondary
                        font.pixelSize: 12
                    }
                }

                // 닫기 버튼
                Rectangle {
                    width: 32
                    height: 32
                    radius: Theme.radiusSm
                    color: closeMouse.containsMouse ? Theme.colorHoverOverlay : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: "X"
                        color: Theme.colorTextSecondary
                        font.pixelSize: 14
                        font.weight: Font.Bold
                    }

                    MouseArea {
                        id: closeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.close()
                    }
                }
            }

            // 2. 탭 네비게이션 바
            Rectangle {
                Layout.fillWidth: true
                height: 36
                radius: Theme.radiusSm
                color: Theme.colorSurfaceSunken
                border.width: 1
                border.color: Theme.colorBorder

                Row {
                    anchors.fill: parent
                    anchors.margins: 3
                    spacing: 4

                    Repeater {
                        model: [
                            { name: "비상 대응 SOP 및 연락망", tab: 0 },
                            { name: "위험 및 장애 코드 가이드", tab: 1 },
                            { name: "키보드 조작 단축키", tab: 2 }
                        ]

                        Rectangle {
                            width: (parent.width - 8) / 3
                            height: parent.height
                            radius: Theme.radiusSm - 1
                            color: root.currentTab === modelData.tab ? Theme.colorSurfaceElevated : "transparent"
                            border.width: root.currentTab === modelData.tab ? 1 : 0
                            border.color: Theme.colorBorderStrong

                            Text {
                                anchors.centerIn: parent
                                text: modelData.name
                                color: root.currentTab === modelData.tab ? Theme.colorAccent : Theme.colorTextSecondary
                                font.pixelSize: 12
                                font.weight: root.currentTab === modelData.tab ? Font.Bold : Font.Normal
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.currentTab = modelData.tab
                            }
                        }
                    }
                }
            }

            // 3. 탭별 콘텐츠 영역
            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: root.currentTab

                // =================================================================
                // TAB 0: 비상 대응 SOP 및 수정 가능한 긴급 연락망
                // =================================================================
                ColumnLayout {
                    spacing: 12

                    // 긴급 연락망 카드 (크고 시원한 레이아웃 + 수정 및 입력 기능)
                    Rectangle {
                        Layout.fillWidth: true
                        height: root.isEditingContacts ? 190 : 126
                        radius: Theme.radiusMd
                        color: Theme.colorSurface
                        border.width: 1
                        border.color: Theme.colorAccent

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 10

                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: "[긴급 비상 연락망]"
                                    color: Theme.colorAccent
                                    font.pixelSize: 14
                                    font.weight: Font.Bold
                                }
                                Item { Layout.fillWidth: true }
                                Rectangle {
                                    width: 86
                                    height: 28
                                    radius: Theme.radiusXs
                                    color: btnEditMouse.containsMouse ? Theme.colorAccentAlpha20 : Qt.rgba(1, 1, 1, 0.06)
                                    border.width: 1
                                    border.color: Theme.colorAccent

                                    Text {
                                        anchors.centerIn: parent
                                        text: root.isEditingContacts ? "저장 완료" : "연락처 수정"
                                        color: Theme.colorAccent
                                        font.pixelSize: 12
                                        font.weight: Font.Bold
                                    }

                                    MouseArea {
                                        id: btnEditMouse
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (root.isEditingContacts) {
                                                root.contactManager = inputManager.text
                                                root.contactControlRoom = inputRoom.text
                                                root.contactMemo = inputMemo.text
                                                root.isEditingContacts = false
                                            } else {
                                                root.isEditingContacts = true
                                            }
                                        }
                                    }
                                }
                            }

                            // 일반 뷰 모드: 시원한 3칸 연락처 배너
                            RowLayout {
                                Layout.fillWidth: true
                                visible: !root.isEditingContacts
                                spacing: 12

                                Rectangle {
                                    Layout.fillWidth: true
                                    height: 38
                                    radius: Theme.radiusXs
                                    color: Qt.rgba(1, 1, 1, 0.04)
                                    Row {
                                        anchors.centerIn: parent
                                        spacing: 8
                                        Text { anchors.verticalCenter: parent.verticalCenter; text: "안전관리자"; color: Theme.colorTextSecondary; font.pixelSize: 12 }
                                        Text { anchors.verticalCenter: parent.verticalCenter; text: root.contactManager; color: Theme.colorTextPrimary; font.pixelSize: 13; font.weight: Font.Bold }
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    height: 38
                                    radius: Theme.radiusXs
                                    color: Qt.rgba(1, 1, 1, 0.04)
                                    Row {
                                        anchors.centerIn: parent
                                        spacing: 8
                                        Text { anchors.verticalCenter: parent.verticalCenter; text: "상황실"; color: Theme.colorTextSecondary; font.pixelSize: 12 }
                                        Text { anchors.verticalCenter: parent.verticalCenter; text: root.contactControlRoom; color: Theme.colorTextPrimary; font.pixelSize: 13; font.weight: Font.Bold }
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    height: 38
                                    radius: Theme.radiusXs
                                    color: Theme.colorDangerBg
                                    border.width: 1
                                    border.color: Theme.colorDanger
                                    Row {
                                        anchors.centerIn: parent
                                        spacing: 8
                                        Text { anchors.verticalCenter: parent.verticalCenter; text: "소방/구급"; color: Theme.colorDanger; font.pixelSize: 12; font.weight: Font.Bold }
                                        Text { anchors.verticalCenter: parent.verticalCenter; text: "119"; color: Theme.colorDanger; font.pixelSize: 15; font.weight: Font.Bold }
                                    }
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                visible: !root.isEditingContacts
                                text: "• 비고/메모: " + root.contactMemo
                                color: "#cbd5e1"
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }

                            // 편집 입력 모드
                            RowLayout {
                                Layout.fillWidth: true
                                visible: root.isEditingContacts
                                spacing: 8

                                TextField {
                                    id: inputManager
                                    Layout.fillWidth: true
                                    height: 34
                                    text: root.contactManager
                                    placeholderText: "안전관리자 번호"
                                    color: Theme.colorTextPrimary
                                    background: Rectangle { color: Theme.colorSurfaceElevated; border.color: Theme.colorBorder; radius: 4 }
                                }
                                TextField {
                                    id: inputRoom
                                    Layout.fillWidth: true
                                    height: 34
                                    text: root.contactControlRoom
                                    placeholderText: "상황실 내선"
                                    color: Theme.colorTextPrimary
                                    background: Rectangle { color: Theme.colorSurfaceElevated; border.color: Theme.colorBorder; radius: 4 }
                                }
                            }
                            TextField {
                                id: inputMemo
                                Layout.fillWidth: true
                                height: 34
                                visible: root.isEditingContacts
                                text: root.contactMemo
                                placeholderText: "비상 대기 메모 및 책임자 성명"
                                color: Theme.colorTextPrimary
                                background: Rectangle { color: Theme.colorSurfaceElevated; border.color: Theme.colorBorder; radius: 4 }
                            }
                        }
                    }

                    // 3단계 대응 절차 목록
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 8

                        Rectangle {
                            Layout.fillWidth: true
                            height: 64
                            radius: Theme.radiusSm
                            color: Theme.colorSurface
                            border.width: 1
                            border.color: Theme.colorCaution

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 10

                                Rectangle {
                                    width: 24; height: 24; radius: 12; color: Theme.colorCautionBg
                                    Text { anchors.centerIn: parent; text: "1"; color: Theme.colorCaution; font.weight: Font.Bold; font.pixelSize: 11 }
                                }
                                Column {
                                    Layout.fillWidth: true; spacing: 2
                                    Text { text: "1단계 : 접근 주의 (3000 ~ 5000 mm 근접)"; color: Theme.colorCaution; font.pixelSize: 12; font.weight: Font.Bold }
                                    Text { text: "현장 경광등 및 음성 경보 정상 송출 확인 / 지게차 운전자 서행 유도"; color: Theme.colorTextSecondary; font.pixelSize: 11 }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 64
                            radius: Theme.radiusSm
                            color: Theme.colorSurface
                            border.width: 1
                            border.color: Theme.colorDanger

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 10

                                Rectangle {
                                    width: 24; height: 24; radius: 12; color: Theme.colorDangerBg
                                    Text { anchors.centerIn: parent; text: "2"; color: Theme.colorDanger; font.weight: Font.Bold; font.pixelSize: 11 }
                                }
                                Column {
                                    Layout.fillWidth: true; spacing: 2
                                    Text { text: "2단계 : 충돌 위험 (< 3000 mm 근접)"; color: Theme.colorDanger; font.pixelSize: 12; font.weight: Font.Bold }
                                    Text { text: "해당 구역 안내 방송 송출 / 작업자 즉시 이동 유도 및 지게차 일시 정지 지시"; color: Theme.colorTextSecondary; font.pixelSize: 11 }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 64
                            radius: Theme.radiusSm
                            color: Theme.colorSurface
                            border.width: 1
                            border.color: Theme.colorEmergency

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 10

                                Rectangle {
                                    width: 24; height: 24; radius: 12; color: Theme.colorEmergencyBg
                                    Text { anchors.centerIn: parent; text: "3"; color: Theme.colorEmergency; font.weight: Font.Bold; font.pixelSize: 11 }
                                }
                                Column {
                                    Layout.fillWidth: true; spacing: 2
                                    Text { text: "3단계 : 비상 제동 확인 및 구호 조치"; color: Theme.colorEmergency; font.pixelSize: 12; font.weight: Font.Bold }
                                    Text { text: "지게차 자동 비상제동 작동 확인 / 119 긴급 신고 및 사고 현장 화면 즉시 캡처 증적 보관"; color: Theme.colorTextSecondary; font.pixelSize: 11 }
                                }
                            }
                        }
                    }
                }

                // =================================================================
                // TAB 1: 위험 단계 및 상태/장애 코드 가이드
                // =================================================================
                Flickable {
                    contentHeight: statusGuideCol.implicitHeight
                    clip: true

                    ColumnLayout {
                        id: statusGuideCol
                        width: parent.width
                        spacing: 12

                        Text { text: "[충돌 위험 단계별 정의]"; color: Theme.colorAccent; font.pixelSize: 13; font.weight: Font.Bold }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 120
                            radius: Theme.radiusSm
                            color: Theme.colorSurface
                            border.width: 1
                            border.color: Theme.colorBorder

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 6

                                RowLayout {
                                    spacing: 8
                                    Rectangle { width: 60; height: 20; radius: 3; color: Theme.colorSafeBg; Text { anchors.centerIn: parent; text: "SAFE"; color: Theme.colorSafe; font.pixelSize: 10; font.weight: Font.Bold } }
                                    Text { text: "이격 거리 5000 mm 이상 정상 작업 상태 (안전 유지)"; color: Theme.colorTextPrimary; font.pixelSize: 11 }
                                }
                                RowLayout {
                                    spacing: 8
                                    Rectangle { width: 60; height: 20; radius: 3; color: Theme.colorCautionBg; Text { anchors.centerIn: parent; text: "CAUTION"; color: Theme.colorCaution; font.pixelSize: 10; font.weight: Font.Bold } }
                                    Text { text: "접근 거리 3000 mm ~ 5000 mm 진입 (서행 및 주의 환기)"; color: Theme.colorTextPrimary; font.pixelSize: 11 }
                                }
                                RowLayout {
                                    spacing: 8
                                    Rectangle { width: 60; height: 20; radius: 3; color: Theme.colorDangerBg; Text { anchors.centerIn: parent; text: "DANGER"; color: Theme.colorDanger; font.pixelSize: 10; font.weight: Font.Bold } }
                                    Text { text: "접근 거리 3000 mm 미만 초근접 (충돌 위험 즉각 경보 발령)"; color: Theme.colorTextPrimary; font.pixelSize: 11 }
                                }
                                RowLayout {
                                    spacing: 8
                                    Rectangle { width: 60; height: 20; radius: 3; color: Theme.colorEmergencyBg; Text { anchors.centerIn: parent; text: "EMERGENCY"; color: Theme.colorEmergency; font.pixelSize: 10; font.weight: Font.Bold } }
                                    Text { text: "1000 mm 미만 극단적 근접 또는 충돌 의심 상황 (비상 제동 조치)"; color: Theme.colorTextPrimary; font.pixelSize: 11 }
                                }
                            }
                        }

                        Text { text: "[시스템 및 센서 상태 코드]"; color: Theme.colorAccent; font.pixelSize: 13; font.weight: Font.Bold }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 130
                            radius: Theme.radiusSm
                            color: Theme.colorSurface
                            border.width: 1
                            border.color: Theme.colorBorder

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 6

                                RowLayout {
                                    spacing: 8
                                    Rectangle { width: 140; height: 20; radius: 3; color: Qt.rgba(1, 1, 1, 0.05); Text { anchors.centerIn: parent; text: "DEAD_RECKONING"; color: Theme.colorAccent; font.pixelSize: 10; font.weight: Font.Bold } }
                                    Text { text: "사각지대 진입 시 지게차 자체 센서로 위치 추정 중"; color: Theme.colorTextPrimary; font.pixelSize: 11 }
                                }
                                RowLayout {
                                    spacing: 8
                                    Rectangle { width: 140; height: 20; radius: 3; color: Qt.rgba(1, 1, 1, 0.05); Text { anchors.centerIn: parent; text: "SENSOR_FAULT"; color: Theme.colorCaution; font.pixelSize: 10; font.weight: Font.Bold } }
                                    Text { text: "현장 센서 장치 일시적 신호 확인 필요"; color: Theme.colorTextPrimary; font.pixelSize: 11 }
                                }
                                RowLayout {
                                    spacing: 8
                                    Rectangle { width: 140; height: 20; radius: 3; color: Qt.rgba(1, 1, 1, 0.05); Text { anchors.centerIn: parent; text: "NETWORK_LOST"; color: Theme.colorDanger; font.pixelSize: 10; font.weight: Font.Bold } }
                                    Text { text: "지게차 단말기 무선 통신 일시 단절"; color: Theme.colorTextPrimary; font.pixelSize: 11 }
                                }
                                RowLayout {
                                    spacing: 8
                                    Rectangle { width: 140; height: 20; radius: 3; color: Qt.rgba(1, 1, 1, 0.05); Text { anchors.centerIn: parent; text: "CAMERA_DISCONNECTED"; color: Theme.colorCaution; font.pixelSize: 10; font.weight: Font.Bold } }
                                    Text { text: "해당 채널 CCTV 영상 스트림 점검 필요"; color: Theme.colorTextPrimary; font.pixelSize: 11 }
                                }
                            }
                        }
                    }
                }

                // =================================================================
                // TAB 2: 키보드 조작 단축키
                // =================================================================
                ColumnLayout {
                    spacing: 12

                    Text { text: "[관제 화면 제어 단축키]"; color: Theme.colorAccent; font.pixelSize: 13; font.weight: Font.Bold }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 180
                        radius: Theme.radiusSm
                        color: Theme.colorSurface
                        border.width: 1
                        border.color: Theme.colorBorder

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 10

                            RowLayout {
                                spacing: 12
                                Rectangle { width: 80; height: 24; radius: 4; color: Qt.rgba(1, 1, 1, 0.08); border.color: Theme.colorBorderStrong; border.width: 1; Text { anchors.centerIn: parent; text: "F1"; color: Theme.colorTextPrimary; font.weight: Font.Bold; font.pixelSize: 11 } }
                                Text { text: "도움말 / 비상 대응 SOP / 상태 코드 가이드 창 열기 및 닫기"; color: Theme.colorTextPrimary; font.pixelSize: 12 }
                            }
                            RowLayout {
                                spacing: 12
                                Rectangle { width: 80; height: 24; radius: 4; color: Qt.rgba(1, 1, 1, 0.08); border.color: Theme.colorBorderStrong; border.width: 1; Text { anchors.centerIn: parent; text: "↑ ↓ ← →"; color: Theme.colorTextPrimary; font.weight: Font.Bold; font.pixelSize: 11 } }
                                Text { text: "구역 및 카메라 그리드 방향키 이동 탐색"; color: Theme.colorTextPrimary; font.pixelSize: 12 }
                            }
                            RowLayout {
                                spacing: 12
                                Rectangle { width: 80; height: 24; radius: 4; color: Qt.rgba(1, 1, 1, 0.08); border.color: Theme.colorBorderStrong; border.width: 1; Text { anchors.centerIn: parent; text: "Enter"; color: Theme.colorTextPrimary; font.weight: Font.Bold; font.pixelSize: 11 } }
                                Text { text: "선택된 카메라 / 채널 진입 및 확대 뷰 전환"; color: Theme.colorTextPrimary; font.pixelSize: 12 }
                            }
                            RowLayout {
                                spacing: 12
                                Rectangle { width: 80; height: 24; radius: 4; color: Qt.rgba(1, 1, 1, 0.08); border.color: Theme.colorBorderStrong; border.width: 1; Text { anchors.centerIn: parent; text: "Esc"; color: Theme.colorTextPrimary; font.weight: Font.Bold; font.pixelSize: 11 } }
                                Text { text: "이전 단계 화면으로 뒤로가기"; color: Theme.colorTextPrimary; font.pixelSize: 12 }
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            // 4. 하단 닫기 버튼
            Rectangle {
                Layout.alignment: Qt.AlignRight
                width: 90
                height: 34
                radius: Theme.radiusSm
                color: Theme.colorAccent

                Text {
                    anchors.centerIn: parent
                    text: "닫기"
                    color: "#ffffff"
                    font.pixelSize: 12
                    font.weight: Font.Bold
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.close()
                }
            }
        }
    }
}
