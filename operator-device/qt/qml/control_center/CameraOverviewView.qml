import QtQuick
import QtQuick.Layouts
import Safety.Common

// 2단계: 선택된 구역(Zone) 내의 물리 카메라(Camera) 목록 뷰
Item {
    id: root
    property string zoneId: "ZONE_A"
    signal cameraSelected(string cameraId)
    signal backRequested()

    readonly property var camerasInZone: [
        {
            cameraId: "CAM_01",
            cameraName: "한화 4채널 멀티센서 360° 카메라",
            modelName: "PNM-9084QZ (192.168.0.3:554)",
            channelCount: 4,
            channels: ["CH 1: 창고 입구", "CH 2: 적재 구역", "CH 3: 출하 게이트", "CH 4: 통로 교차로"]
        }
    ]

    function isCameraAlert(camId) {
        for (var i = 0; i < cameraListModel.count; ++i) {
            var id = cameraListModel.data(cameraListModel.index(i, 0), CameraListModel.CameraIdRole);
            if (id.startsWith(camId)) {
                var r = cameraListModel.data(cameraListModel.index(i, 0), CameraListModel.RiskLevelRole);
                var ex = cameraListModel.data(cameraListModel.index(i, 0), CameraListModel.ExceptionStateRole);
                if (r > 0 || ex > 0) return true;
            }
        }
        return false;
    }

    function moveLeft() { gridView.currentIndex = Math.max(0, gridView.currentIndex - 1) }
    function moveRight() { gridView.currentIndex = Math.min(gridView.count - 1, gridView.currentIndex + 1) }
    function moveUp() { gridView.currentIndex = Math.max(0, gridView.currentIndex - 1) }
    function moveDown() { gridView.currentIndex = Math.min(gridView.count - 1, gridView.currentIndex + 1) }
    function activateCurrent() {
        if (root.camerasInZone.length > 0)
            root.cameraSelected(root.camerasInZone[Math.max(0, gridView.currentIndex)].cameraId)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingMd

        // 상단 내비게이션 바
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            Rectangle {
                width: 32
                height: 32
                radius: Theme.radiusSm
                color: Theme.colorSurfaceElevated
                border.width: 1
                border.color: Theme.colorBorder

                Text {
                    anchors.centerIn: parent
                    text: "←"
                    color: Theme.colorTextPrimary
                    font.pixelSize: Theme.typeHeading.size - 2
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.backRequested()
                }
            }

            Text {
                text: root.zoneId + " › 설치 카메라 목록"
                color: "#ffffff"
                font.pixelSize: Theme.typeHeading.size
                font.weight: Font.Bold
                Layout.fillWidth: true
            }
        }

        // 카메라 카드 그리드
        GridView {
            id: gridView
            Layout.fillWidth: true
            Layout.fillHeight: true
            cellWidth: 380
            cellHeight: 230
            clip: true
            model: root.camerasInZone

            delegate: Rectangle {
                property bool isCurrent: GridView.isCurrentItem
                width: 360
                height: 210
                radius: Theme.radiusMd
                color: (isCurrent || camMouse.containsMouse) ? Theme.colorSurfaceElevated : Theme.colorSurface
                border.width: isCameraAlert(modelData.cameraId) ? 2 : (isCurrent ? 2 : Theme.borderWidthHairline)
                border.color: isCameraAlert(modelData.cameraId) ? Theme.colorDanger : (isCurrent || camMouse.containsMouse ? Theme.colorAccent : Theme.colorBorder)

                Behavior on border.color { ColorAnimation { duration: 150 } }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMd
                    spacing: Theme.spacingSm

                    // 상단 헤더
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm

                        Rectangle {
                            width: 36
                            height: 36
                            radius: Theme.radiusSm
                            color: isCameraAlert(modelData.cameraId) ? Theme.colorDangerBg : Qt.rgba(1, 1, 1, 0.05)
                            border.width: 1
                            border.color: isCameraAlert(modelData.cameraId) ? Theme.colorDanger : Theme.colorBorder

                            Text {
                                anchors.centerIn: parent
                                text: "CAM"
                                color: Theme.colorAccent
                                font.pixelSize: 10
                                font.weight: Font.Bold
                            }
                        }

                        Column {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: modelData.cameraId
                                color: "#ffffff"
                                font.pixelSize: 16
                                font.weight: Font.Bold
                            }
                            Text {
                                text: modelData.cameraName
                                color: "#e2e8f0"
                                font.pixelSize: 13
                            }
                        }

                        Rectangle {
                            visible: isCameraAlert(modelData.cameraId)
                            implicitWidth: camAlertTxt.implicitWidth + 12
                            height: 22
                            radius: Theme.radiusSm
                            color: Theme.colorDangerBg
                            border.width: 1
                            border.color: Theme.colorDanger

                            Text {
                                id: camAlertTxt
                                anchors.centerIn: parent
                                text: "위험 감지"
                                color: Theme.colorDanger
                                font.pixelSize: 11
                                font.weight: Font.Bold
                            }
                        }
                    }

                    // 하위 채널 목록 요약
                    Column {
                        Layout.fillWidth: true
                        spacing: 3

                        Repeater {
                            model: modelData.channels
                            Text {
                                text: "  ├─ " + modelData
                                color: Theme.colorTextMuted
                                font.pixelSize: Theme.typeLabel.size + 1
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }

                    // 하단 진입 버튼
                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            text: "4개 렌즈 동시 모니터링"
                            color: Theme.colorTextMuted
                            font.pixelSize: Theme.typeCaption.size - 1
                            Layout.fillWidth: true
                        }

                        Rectangle {
                            width: 110
                            height: 28
                            radius: Theme.radiusSm
                            color: Theme.colorAccent
                            opacity: camMouse.containsMouse ? 1.0 : 0.85

                            Text {
                                anchors.centerIn: parent
                                text: "4채널 영상 보기 →"
                                color: "#ffffff"
                                font.pixelSize: Theme.typeCaption.size - 1
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                }

                MouseArea {
                    id: camMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.cameraSelected(modelData.cameraId)
                }
            }
        }
    }
}
