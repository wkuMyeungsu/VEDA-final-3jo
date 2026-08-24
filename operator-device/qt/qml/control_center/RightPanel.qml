import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Safety.Common

// 계층 연동형(Zone ➔ Cam ➔ Channel) 반응형 우측 분석 패널
Rectangle {
    id: root
    signal cameraFocusRequested(string cameraId)
    signal toggleCollapse()

    property bool isCollapsed: false
    property int navDepth: 1 // 1: 구역 목록, 2: 카메라 목록, 3: 4채널 그리드, 4: 단일 채널 확대
    property string selectedZoneId: "ZONE_A"
    property string selectedCameraId: "CAM_01"
    property string selectedStreamId: "CAM_01_CH_01"
    property string selectedCameraName: "창고 입구 (전방)"

    color: Theme.colorSurfaceGlass
    border.width: 1
    border.color: Theme.colorBorder
    radius: Theme.radiusMd
    clip: true

    Behavior on width {
        NumberAnimation { duration: 250; easing.type: Easing.InOutCubic }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingSm
        visible: root.width > 100 // 접혔을 때 렌더링 억제

        // 패널 상단 브레드크럼 & 접기 버튼 바
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            Rectangle {
                width: 48
                height: 22
                radius: Theme.radiusXs
                color: Theme.colorAccentAlpha20
                border.width: 1
                border.color: Theme.colorAccent

                Text {
                    anchors.centerIn: parent
                    text: root.navDepth === 1 ? "SITE" : (root.navDepth === 2 ? "ZONE" : (root.navDepth === 3 ? "CAM" : "CH-AI"))
                    color: Theme.colorAccent
                    font.pixelSize: 10
                    font.weight: Font.Bold
                }
            }

            Column {
                Layout.fillWidth: true
                spacing: 1

                Text {
                    text: root.navDepth === 1 ? "사업장 전체 관제" 
                        : (root.navDepth === 2 ? root.selectedZoneId + " 구역 상세"
                        : (root.navDepth === 3 ? root.selectedCameraId + " 카메라 상세"
                        : "채널 실시간 AI 정밀 분석"))
                    color: Theme.colorTextPrimary
                    font.pixelSize: 14
                    font.weight: Font.Bold
                }

                Text {
                    text: root.navDepth === 1 ? "전체 안전 구역 요약"
                        : (root.navDepth === 2 ? "구역 내 카메라 장치 관리"
                        : (root.navDepth === 3 ? "4개 센서 채널 통합 상태"
                        : root.selectedCameraName))
                    color: Theme.colorTextSecondary
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }

            // 우측 패널 접기 버튼
            Rectangle {
                width: 26
                height: 26
                radius: Theme.radiusXs
                color: btnCollapseMouse.containsMouse ? Theme.colorHoverOverlay : Qt.rgba(1, 1, 1, 0.04)
                border.width: 1
                border.color: Theme.colorBorder

                Text {
                    anchors.centerIn: parent
                    text: ">"
                    color: Theme.colorTextSecondary
                    font.pixelSize: 12
                    font.weight: Font.Bold
                }

                MouseArea {
                    id: btnCollapseMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.toggleCollapse()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.colorBorder
        }

        // =========================================================================
        // LEVEL 1: 사업장 전체 구역 요약 (전체 구역 뷰일 때)
        // =========================================================================
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingMd
            visible: root.navDepth === 1

            // 핵심 상태 요약 배너
            Rectangle {
                Layout.fillWidth: true
                height: 56
                radius: Theme.radiusSm
                color: alertListModel.count > 0 ? Theme.colorDangerBg : Theme.colorSurfaceElevated
                border.width: 1
                border.color: alertListModel.count > 0 ? Theme.colorDanger : Theme.colorBorder

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12

                    Rectangle {
                        width: 28
                        height: 28
                        radius: Theme.radiusXs
                        color: alertListModel.count > 0 ? Theme.colorDangerBg : Theme.colorSafeBg
                        border.width: 1
                        border.color: alertListModel.count > 0 ? Theme.colorDanger : Theme.colorSafe

                        Text {
                            anchors.centerIn: parent
                            text: alertListModel.count > 0 ? "!" : "OK"
                            color: alertListModel.count > 0 ? Theme.colorDanger : Theme.colorSafe
                            font.pixelSize: 11
                            font.weight: Font.Bold
                        }
                    }

                    Column {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: alertListModel.count > 0 ? "실시간 위험/이상 감지: " + alertListModel.count + "건" : "전체 구역 안전 상태 유지 중"
                            color: alertListModel.count > 0 ? Theme.colorDanger : Theme.colorSafe
                            font.pixelSize: 13
                            font.weight: Font.Bold
                        }
                        Text {
                            text: alertListModel.count > 0 ? "총 1개 구역 · " + alertListModel.count + "개 경보/이상 상태 발생" : "총 1개 구역 · 4개 채널 스트림 정상 수신"
                            color: Theme.colorTextSecondary
                            font.pixelSize: 11
                        }
                    }
                }
            }

            Text {
                text: "구역별 안전 현황"
                color: Theme.colorTextPrimary
                font.pixelSize: 13
                font.weight: Font.Bold
            }

            // 구역 카드
            Rectangle {
                Layout.fillWidth: true
                height: 110
                radius: Theme.radiusSm
                color: Theme.colorSurface
                border.width: 1
                border.color: Theme.colorBorder

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "ZONE_A (창고 메인 구역)"; color: Theme.colorTextPrimary; font.weight: Font.Bold; font.pixelSize: 13 }
                        Item { Layout.fillWidth: true }
                        Rectangle {
                            width: 54
                            height: 20
                            radius: Theme.radiusXs
                            color: alertListModel.count > 0 ? Theme.colorDangerBg : Theme.colorSafeBg
                            Text {
                                anchors.centerIn: parent
                                text: alertListModel.count > 0 ? "위험 감지" : "정상"
                                color: alertListModel.count > 0 ? Theme.colorDanger : Theme.colorSafe
                                font.pixelSize: 10
                                font.weight: Font.Bold
                            }
                        }
                    }

                    Text {
                        text: "• 설치 카메라: CAM_01 (4채널 멀티센서)\n• 실시간 접근 모니터링 및 자동 충돌 경보 가동 중"
                        color: Theme.colorTextSecondary
                        font.pixelSize: 11
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }

        // =========================================================================
        // LEVEL 2: 선택 구역 상세 정보 (카메라 목록 뷰일 때)
        // =========================================================================
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingMd
            visible: root.navDepth === 2

            Rectangle {
                Layout.fillWidth: true
                height: 80
                radius: Theme.radiusSm
                color: Theme.colorSurface
                border.width: 1
                border.color: Theme.colorBorder

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 4
                    Text { text: "[구역 정보] " + root.selectedZoneId; color: Theme.colorAccent; font.weight: Font.Bold; font.pixelSize: 13 }
                    Text { text: "• 위치: 제1 물류센터 메인 하역장\n• 안전 담당자: 안전관리팀 (내선 1024)"; color: Theme.colorTextSecondary; font.pixelSize: 11 }
                }
            }

            Text {
                text: "구역 내 카메라 장치"
                color: Theme.colorTextPrimary
                font.pixelSize: 13
                font.weight: Font.Bold
            }

            // 카메라 진입 카드 (버튼 클릭 연동 완료)
            Rectangle {
                id: camCardItem
                Layout.fillWidth: true
                height: 68
                radius: Theme.radiusSm
                color: cardMouse.containsMouse ? Theme.colorSurfaceSunken : Theme.colorSurfaceElevated
                border.width: 1
                border.color: cardMouse.containsMouse ? Theme.colorAccent : Theme.colorBorderStrong

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10

                    Rectangle {
                        width: 42
                        height: 24
                        radius: Theme.radiusXs
                        color: Theme.colorAccentAlpha20
                        border.width: 1
                        border.color: Theme.colorAccent
                        Text { anchors.centerIn: parent; text: "CAM"; color: Theme.colorAccent; font.pixelSize: 10; font.weight: Font.Bold }
                    }

                    Column {
                        Layout.fillWidth: true
                        spacing: 2
                        Text { text: "CAM_01 (4채널 멀티센서)"; color: Theme.colorTextPrimary; font.weight: Font.Bold; font.pixelSize: 12 }
                        Text { text: "창고 입구 / 적재 구역 / 출하 / 통로"; color: Theme.colorTextSecondary; font.pixelSize: 11 }
                    }

                    Rectangle {
                        width: 58
                        height: 26
                        radius: Theme.radiusXs
                        color: Theme.colorAccent
                        Text { anchors.centerIn: parent; text: "진입 →"; color: "#ffffff"; font.pixelSize: 11; font.weight: Font.Bold }
                    }
                }

                MouseArea {
                    id: cardMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.cameraFocusRequested("CAM_01")
                }
            }

            Item { Layout.fillHeight: true }
        }

        // =========================================================================
        // LEVEL 3: 4채널 그리드 뷰 상태
        // =========================================================================
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingSm
            visible: root.navDepth === 3

            Text {
                text: "4개 센서 채널 실시간 상태"
                color: Theme.colorTextPrimary
                font.pixelSize: 13
                font.weight: Font.Bold
            }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 6
                clip: true
                model: cameraListModel

                delegate: Rectangle {
                    width: ListView.view.width
                    height: 52
                    radius: Theme.radiusSm
                    readonly property bool hasException: model.exceptionState > 0
                    readonly property bool hasRisk: model.riskLevel > 0
                    color: hasException ? Theme.colorUnknownBg : (hasRisk ? Theme.colorDangerBg : Theme.colorSurface)
                    border.width: 1
                    border.color: hasException ? Theme.colorUnknown : (hasRisk ? Theme.colorDanger : Theme.colorBorder)

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 8

                        Rectangle {
                            width: 8
                            height: 8
                            radius: 4
                            color: Theme.connectionColor(model.videoConnectionState)
                        }

                        Column {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: model.name.length > 0 ? model.name : model.cameraId
                                color: Theme.colorTextPrimary
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Text {
                                text: hasException
                                      ? Theme.exceptionLabel(model.exceptionState)
                                      : (hasRisk
                                         ? ("접근 경보: " + (model.distanceValid ? Math.round(model.distanceM * 1000) + " mm" : "위험"))
                                         : (model.distanceValid ? "정상 안전 거리 유지" : "센서 대기 (미배정)"))
                                color: hasException ? Theme.colorUnknown : (hasRisk ? Theme.colorDanger : Theme.colorTextSecondary)
                                font.pixelSize: 11
                            }
                        }

                        Text {
                            text: hasException ? "확인 불가" : (hasRisk ? "위험" : (model.distanceValid ? "정상" : "대기"))
                            color: hasException ? Theme.colorUnknown : (hasRisk ? Theme.colorDanger : Theme.colorTextMuted)
                            font.pixelSize: 11
                            font.weight: Font.Bold
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.cameraFocusRequested(model.cameraId)
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }

        // =========================================================================
        // LEVEL 4: 선택 스트림(카메라 채널) 상세 분석 뷰
        // =========================================================================
        ColumnLayout {
            id: level4Column
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingSm
            visible: root.navDepth === 4

            property double currentDist: 0.0
            property int currentChRiskLevel: 0
            property int currentChException: 0
            property bool chDistValid: false
            readonly property bool hasChException: currentChException > 0
            readonly property bool hasChRisk: currentChRiskLevel > 0

            function updateCurrentValues() {
                if (root.selectedStreamId && root.selectedStreamId.length > 0) {
                    level4Column.currentDist = cameraListModel.distanceMFor(root.selectedStreamId);
                    level4Column.currentChRiskLevel = cameraListModel.riskLevelFor(root.selectedStreamId);
                    level4Column.currentChException = cameraListModel.exceptionStateFor(root.selectedStreamId);
                    level4Column.chDistValid = cameraListModel.distanceValidFor(root.selectedStreamId);
                }
            }

            Connections {
                target: cameraListModel
                function onRiskUpdated(cameraId, rLevel, exState, dist) {
                    if (cameraId === root.selectedStreamId) {
                        level4Column.currentChRiskLevel = rLevel;
                        level4Column.currentChException = exState;
                        level4Column.currentDist = dist;
                        level4Column.chDistValid = cameraListModel.distanceValidFor(cameraId);
                    }
                }
            }

            Connections {
                target: root
                function onSelectedStreamIdChanged() {
                    level4Column.updateCurrentValues();
                }
                function onNavDepthChanged() {
                    if (root.navDepth === 4)
                        level4Column.updateCurrentValues();
                }
            }

            Component.onCompleted: level4Column.updateCurrentValues()

            // 1) 실시간 접근 거리 게이지 바
            Rectangle {
                Layout.fillWidth: true
                height: 94
                radius: Theme.radiusSm
                color: Theme.colorSurface
                border.width: 1
                border.color: level4Column.hasChException ? Theme.colorUnknown : (level4Column.hasChRisk ? Theme.colorDanger : Theme.colorBorder)

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "실시간 접근 거리"
                            color: Theme.colorTextSecondary
                            font.pixelSize: 12
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: level4Column.hasChException
                                  ? Theme.exceptionLabel(level4Column.currentChException)
                                  : (level4Column.hasChRisk
                                     ? (level4Column.chDistValid ? Math.round(level4Column.currentDist * 1000) + " mm" : "위험 접근")
                                     : (level4Column.chDistValid
                                        ? (level4Column.currentDist > 0 ? "안전 거리 확보됨 (> 5000 mm)" : "정상 모니터링")
                                        : "센서 대기 (미배정)"))
                            color: level4Column.hasChException
                                   ? Theme.colorUnknown
                                   : (level4Column.hasChRisk
                                      ? Theme.colorDanger
                                      : (level4Column.chDistValid ? Theme.colorSafe : Theme.colorTextMuted))
                            font.pixelSize: (level4Column.hasChException || level4Column.hasChRisk) ? 16 : 13
                            font.weight: Font.Bold
                        }
                    }

                    // 거리 프로그레스 게이지 바 (0 ~ 10000 mm 기준)
                    Rectangle {
                        Layout.fillWidth: true
                        height: 10
                        radius: 5
                        color: Theme.colorSurfaceSunken

                        Rectangle {
                            width: (level4Column.hasChException || !level4Column.chDistValid)
                                   ? parent.width
                                   : (level4Column.hasChRisk
                                      ? Math.min(parent.width, Math.max(8, parent.width * (Math.min(10.0, level4Column.currentDist) / 10.0)))
                                      : parent.width)
                            height: parent.height
                            radius: 5
                            color: (level4Column.hasChException || !level4Column.chDistValid)
                                   ? Theme.colorUnknown
                                   : (level4Column.currentChRiskLevel === 2 ? Theme.colorDanger 
                                      : (level4Column.currentChRiskLevel === 1 ? Theme.colorCaution : Theme.colorSafe))

                            Behavior on width { NumberAnimation { duration: 150 } }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "0 mm (위험)"; color: Theme.colorDanger; font.pixelSize: 10 }
                        Item { Layout.fillWidth: true }
                        Text { text: "5000 mm (주의)"; color: Theme.colorCaution; font.pixelSize: 10 }
                        Item { Layout.fillWidth: true }
                        Text { text: "10000 mm (안전)"; color: Theme.colorSafe; font.pixelSize: 10 }
                    }
                }
            }

            // 2) AI 객체 감지 실시간 상태
            Rectangle {
                Layout.fillWidth: true
                height: 72
                radius: Theme.radiusSm
                color: Theme.colorSurface
                border.width: 1
                border.color: Theme.colorBorder

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    // 보행자 상태
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: Theme.radiusXs
                        color: Qt.rgba(1, 1, 1, 0.03)

                        Column {
                            anchors.centerIn: parent
                            spacing: 3
                            Text { text: "[작업자]"; color: Theme.colorTextSecondary; font.pixelSize: 11; anchors.horizontalCenter: parent.horizontalCenter }
                            Text { text: "실시간 감지 중"; color: Theme.colorPersonBox; font.pixelSize: 12; font.weight: Font.Bold; anchors.horizontalCenter: parent.horizontalCenter }
                        }
                    }

                    // 지게차 상태
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: Theme.radiusXs
                        color: Qt.rgba(1, 1, 1, 0.03)

                        Column {
                            anchors.centerIn: parent
                            spacing: 3
                            Text { text: "[지게차]"; color: Theme.colorTextSecondary; font.pixelSize: 11; anchors.horizontalCenter: parent.horizontalCenter }
                            Text { text: "동선 추적 중"; color: Theme.colorForkliftBox; font.pixelSize: 12; font.weight: Font.Bold; anchors.horizontalCenter: parent.horizontalCenter }
                        }
                    }
                }
            }

            // 3) 최근 발생 이벤트 타임라인
            Text {
                text: "최근 이벤트 타임라인"
                color: Theme.colorTextPrimary
                font.pixelSize: 12
                font.weight: Font.Bold
            }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 6
                clip: true
                model: eventLogModel

                delegate: Rectangle {
                    width: ListView.view.width
                    height: 38
                    radius: Theme.radiusXs
                    color: Theme.colorSurfaceElevated
                    border.width: 1
                    border.color: Theme.colorBorder

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 8

                        Text {
                            text: Qt.formatDateTime(model.utcTime, "HH:mm:ss")
                            color: Theme.colorTextMuted
                            font.pixelSize: 11
                        }

                        Text {
                            readonly property bool isEx: model.exceptionState > 0
                            text: isEx ? Theme.exceptionLabel(model.exceptionState) : Theme.riskLabel(model.riskLevel)
                            color: isEx ? Theme.colorUnknown : Theme.riskColor(model.riskLevel)
                            font.pixelSize: 11
                            font.weight: Font.Bold
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: model.distanceValid ? Math.round(model.distanceM * 1000) + " mm" : "-"
                            color: Theme.colorTextSecondary
                            font.pixelSize: 11
                        }
                    }
                }
            }
        }
    }
}
