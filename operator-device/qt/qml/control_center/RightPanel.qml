import QtQuick
import QtQuick.Layouts
import Safety.Common

Item {
    id: root
    signal cameraFocusRequested(string cameraId)

    property int activeTab: 0 // 0: 구역 계층도, 1: 경보 & 통신 상태

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingSm

        // 상단 KPI 실시간 요약 카드
        Rectangle {
            Layout.fillWidth: true
            height: 64
            radius: Theme.radiusMd
            color: Theme.colorSurface
            border.width: Theme.borderWidthHairline
            border.color: Theme.colorBorder

            Row {
                anchors.fill: parent
                anchors.margins: Theme.spacingSm
                spacing: Theme.spacingXs

                // KPI 1: 활성 카메라 채널
                Rectangle {
                    width: (parent.width - Theme.spacingXs * 2) / 3
                    height: parent.height
                    radius: Theme.radiusSm
                    color: Qt.rgba(1, 1, 1, 0.03)

                    Column {
                        anchors.centerIn: parent
                        spacing: 2
                        Text {
                            text: "카메라 채널"
                            color: Theme.colorTextMuted
                            font.pixelSize: Theme.typeLabel.size
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                        Text {
                            text: cameraListModel.count + " CH"
                            color: Theme.colorTextPrimary
                            font.pixelSize: Theme.typeHeading.size
                            font.weight: Font.DemiBold
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                    }
                }

                // KPI 2: 현재 위험 경보 수
                Rectangle {
                    width: (parent.width - Theme.spacingXs * 2) / 3
                    height: parent.height
                    radius: Theme.radiusSm
                    color: alertListModel.count > 0 ? Theme.colorDangerBg : Qt.rgba(1, 1, 1, 0.03)
                    border.width: alertListModel.count > 0 ? 1 : 0
                    border.color: Theme.colorDanger

                    Column {
                        anchors.centerIn: parent
                        spacing: 2
                        Text {
                            text: "실시간 경보"
                            color: alertListModel.count > 0 ? Theme.colorDanger : Theme.colorTextMuted
                            font.pixelSize: Theme.typeLabel.size
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                        Text {
                            text: alertListModel.count + " 건"
                            color: alertListModel.count > 0 ? Theme.colorDanger : Theme.colorSafe
                            font.pixelSize: Theme.typeHeading.size
                            font.weight: Font.DemiBold
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                    }
                }

                // KPI 3: 시스템 상태
                Rectangle {
                    width: (parent.width - Theme.spacingXs * 2) / 3
                    height: parent.height
                    radius: Theme.radiusSm
                    color: Qt.rgba(1, 1, 1, 0.03)

                    Column {
                        anchors.centerIn: parent
                        spacing: 2
                        Text {
                            text: "시스템 상태"
                            color: Theme.colorTextMuted
                            font.pixelSize: Theme.typeLabel.size
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                        Text {
                            text: serverConnection.connectionState === 2 ? "정상 작동" : "점검 필요"
                            color: serverConnection.connectionState === 2 ? Theme.colorSafe : Theme.colorCaution
                            font.pixelSize: Theme.typeHeading.size
                            font.weight: Font.DemiBold
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                    }
                }
            }
        }

        // 탭 선택 바
        Rectangle {
            Layout.fillWidth: true
            height: 34
            radius: Theme.radiusSm
            color: Theme.colorSurfaceSunken
            border.width: Theme.borderWidthHairline
            border.color: Theme.colorBorder

            Row {
                anchors.fill: parent
                anchors.margins: 2
                spacing: 2

                Rectangle {
                    width: (parent.width - 2) / 2
                    height: parent.height
                    radius: Theme.radiusSm - 1
                    color: root.activeTab === 0 ? Theme.colorSurfaceElevated : "transparent"
                    border.width: root.activeTab === 0 ? 1 : 0
                    border.color: Theme.colorBorderStrong

                    Text {
                        anchors.centerIn: parent
                        text: "🗂️ 구역 · 카메라 계층도"
                        color: root.activeTab === 0 ? Theme.colorTextPrimary : Theme.colorTextMuted
                        font.pixelSize: Theme.typeCaption.size
                        font.weight: root.activeTab === 0 ? Font.Medium : Font.Normal
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.activeTab = 0
                    }
                }

                Rectangle {
                    width: (parent.width - 2) / 2
                    height: parent.height
                    radius: Theme.radiusSm - 1
                    color: root.activeTab === 1 ? Theme.colorSurfaceElevated : "transparent"
                    border.width: root.activeTab === 1 ? 1 : 0
                    border.color: Theme.colorBorderStrong

                    Text {
                        anchors.centerIn: parent
                        text: "🚨 실시간 경보 · 상태"
                        color: root.activeTab === 1 ? Theme.colorTextPrimary : Theme.colorTextMuted
                        font.pixelSize: Theme.typeCaption.size
                        font.weight: root.activeTab === 1 ? Font.Medium : Font.Normal
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.activeTab = 1
                    }
                }
            }
        }

        // 탭 콘텐츠 영역
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // 탭 0: 구역 ➔ 카메라 ➔ 채널 3단계 계층 트리
            ZoneHierarchyView {
                anchors.fill: parent
                visible: root.activeTab === 0
                onCameraSelected: (cameraId) => root.cameraFocusRequested(cameraId)
            }

            // 탭 1: 경보 목록 및 카메라 상태
            Column {
                anchors.fill: parent
                spacing: Theme.spacingSm
                visible: root.activeTab === 1

                SectionPanel {
                    title: "현재 위험 경보"
                    width: parent.width
                    height: parent.height * 0.55

                    AlertListView {
                        anchors.fill: parent
                        onCameraFocusRequested: (cameraId) => root.cameraFocusRequested(cameraId)
                    }
                }

                SectionPanel {
                    title: "카메라 통신 상태"
                    width: parent.width
                    height: parent.height * 0.45 - Theme.spacingSm

                    CameraStatusList {
                        anchors.fill: parent
                        onCameraFocusRequested: (cameraId) => root.cameraFocusRequested(cameraId)
                    }
                }
            }
        }
    }
}
