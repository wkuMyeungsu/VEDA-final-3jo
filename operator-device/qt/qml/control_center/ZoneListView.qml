import QtQuick
import QtQuick.Layouts
import Safety.Common

// 1단계: 공장 내 구역(Zone) 목록 대시보드 뷰
Item {
    id: root
    signal zoneSelected(string zoneId)

    readonly property var zones: [
        {
            zoneId: "ZONE_A",
            zoneName: "창고 메인 구역 (1층)",
            description: "입출하 게이트 및 지게차 메인 주행 통로",
            cameraCount: 1,
            channelCount: 4
        }
    ]

    function isZoneAlert(zoneId) {
        for (var i = 0; i < cameraListModel.count; ++i) {
            var z = cameraListModel.zoneFor(cameraListModel.data(cameraListModel.index(i, 0), CameraListModel.CameraIdRole));
            if (z === zoneId) {
                var rLevel = cameraListModel.data(cameraListModel.index(i, 0), CameraListModel.RiskLevelRole);
                var ex = cameraListModel.data(cameraListModel.index(i, 0), CameraListModel.ExceptionStateRole);
                if (rLevel > 0 || ex > 0) return true;
            }
        }
        return false;
    }

    function moveLeft() { gridView.currentIndex = Math.max(0, gridView.currentIndex - 1) }
    function moveRight() { gridView.currentIndex = Math.min(gridView.count - 1, gridView.currentIndex + 1) }
    function moveUp() { gridView.currentIndex = Math.max(0, gridView.currentIndex - 1) }
    function moveDown() { gridView.currentIndex = Math.min(gridView.count - 1, gridView.currentIndex + 1) }
    function activateCurrent() {
        if (root.zones.length > 0)
            root.zoneSelected(root.zones[Math.max(0, gridView.currentIndex)].zoneId)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingMd

        // 상단 안내 라벨
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            Text {
                text: "🏢 관제 구역 선택 (Zone Overview)"
                color: "#ffffff"
                font.pixelSize: Theme.typeHeading.size
                font.weight: Font.Bold
            }

            Text {
                text: "모니터링할 구역을 선택하면 해당 구역의 카메라로 이동합니다."
                color: "#94a3b8"
                font.pixelSize: Theme.typeCaption.size
            }
        }

        // 구역 카드 그리드/목록
        GridView {
            id: gridView
            Layout.fillWidth: true
            Layout.fillHeight: true
            cellWidth: 360
            cellHeight: 220
            clip: true
            model: root.zones

            delegate: Rectangle {
                property bool isCurrent: GridView.isCurrentItem
                width: 340
                height: 200
                radius: Theme.radiusMd
                color: (isCurrent || zoneMouse.containsMouse) ? Theme.colorSurfaceElevated : Theme.colorSurface
                border.width: isZoneAlert(modelData.zoneId) ? 2 : (isCurrent ? 2 : 1)
                border.color: isZoneAlert(modelData.zoneId) ? Theme.colorDanger : (isCurrent || zoneMouse.containsMouse ? Theme.colorAccent : Theme.colorBorder)

                Behavior on border.color { ColorAnimation { duration: 150 } }
                Behavior on color { ColorAnimation { duration: 150 } }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMd
                    spacing: Theme.spacingSm

                    // 상단 헤더
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm

                        Rectangle {
                            width: 38
                            height: 38
                            radius: Theme.radiusSm
                            color: isZoneAlert(modelData.zoneId) ? Theme.colorDangerBg : Qt.rgba(1, 1, 1, 0.08)
                            border.width: 1
                            border.color: isZoneAlert(modelData.zoneId) ? Theme.colorDanger : Theme.colorBorder

                            Text {
                                anchors.centerIn: parent
                                text: "ZONE"
                                color: Theme.colorAccent
                                font.pixelSize: 10
                                font.weight: Font.Bold
                            }
                        }

                        Column {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: modelData.zoneId
                                color: "#ffffff"
                                font.pixelSize: 16
                                font.weight: Font.Bold
                            }
                            Text {
                                text: modelData.zoneName
                                color: "#e2e8f0"
                                font.pixelSize: 13
                                font.weight: Font.Medium
                            }
                        }

                        Rectangle {
                            visible: isZoneAlert(modelData.zoneId)
                            implicitWidth: alertTxt.implicitWidth + 12
                            height: 24
                            radius: Theme.radiusSm
                            color: Theme.colorDangerBg
                            border.width: 1
                            border.color: Theme.colorDanger

                            Text {
                                id: alertTxt
                                anchors.centerIn: parent
                                text: "위험 감지"
                                color: Theme.colorDanger
                                font.pixelSize: 11
                                font.weight: Font.Bold
                            }
                        }
                    }

                    // 설명
                    Text {
                        text: modelData.description
                        color: "#94a3b8"
                        font.pixelSize: 12
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }

                    Item { Layout.fillHeight: true }

                    // 하단 통계 및 진입 버튼
                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            text: "물리 카메라 " + modelData.cameraCount + "대 · " + modelData.channelCount + "개 채널"
                            color: "#cbd5e1"
                            font.pixelSize: 12
                            Layout.fillWidth: true
                        }

                        Rectangle {
                            width: 85
                            height: 30
                            radius: Theme.radiusSm
                            color: Theme.colorAccent
                            opacity: zoneMouse.containsMouse ? 1.0 : 0.88

                            Text {
                                anchors.centerIn: parent
                                text: "구역 진입 →"
                                color: "#ffffff"
                                font.pixelSize: 12
                                font.weight: Font.Bold
                            }
                        }
                    }
                }

                MouseArea {
                    id: zoneMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.zoneSelected(modelData.zoneId)
                }
            }
        }
    }
}
