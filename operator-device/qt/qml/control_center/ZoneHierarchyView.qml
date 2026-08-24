import QtQuick
import QtQuick.Layouts
import Safety.Common

// 구역(Zone) ➔ 물리 카메라(Camera) ➔ 채널(Channel) 계층 트리 뷰
Rectangle {
    id: root
    signal cameraSelected(string cameraId)

    color: "transparent"

    // 실시간 연결 상태 및 위험 상태 로컬 맵
    property var connStates: ({})
    property var riskLevels: ({})
    property var distValues: ({})

    function refreshAll() {
        var cs = {};
        var rs = {};
        var ds = {};
        for (var i = 0; i < cameraListModel.count; ++i) {
            var id = cameraListModel.cameraIdAt(i);
            if (id && id.length > 0) {
                cs[id] = cameraListModel.videoConnectionStateFor(id);
                rs[id] = cameraListModel.riskLevelFor(id);
                ds[id] = cameraListModel.distanceMFor(id);
            }
        }
        root.connStates = cs;
        root.riskLevels = rs;
        root.distValues = ds;
    }

    Component.onCompleted: refreshAll()

    Connections {
        target: cameraListModel
        function onVideoConnectionChanged(cameraId, state) {
            var cs = Object.assign({}, root.connStates);
            cs[cameraId] = state;
            root.connStates = cs;
        }
        function onRiskUpdated(cameraId, rLevel, exState, dist) {
            var rs = Object.assign({}, root.riskLevels);
            var ds = Object.assign({}, root.distValues);
            rs[cameraId] = rLevel;
            ds[cameraId] = dist;
            root.riskLevels = rs;
            root.distValues = ds;
        }
        function onDataChanged() {
            root.refreshAll();
        }
    }

    readonly property var zones: [
        {
            zoneId: "ZONE_A",
            zoneName: "창고 메인 구역",
            cameras: [
                {
                    cameraId: "CAM_01",
                    cameraModel: "4채널 멀티센서 카메라",
                    channels: [
                        { streamId: "CAM_01_CH_01", name: "CH 1: 창고 입구 (전방)" },
                        { streamId: "CAM_01_CH_02", name: "CH 2: 적재 구역 (우측)" },
                        { streamId: "CAM_01_CH_03", name: "CH 3: 출하 게이트 (후방)" },
                        { streamId: "CAM_01_CH_04", name: "CH 4: 통로 교차로 (좌측)" }
                    ]
                }
            ]
        }
    ]

    function isZoneAlert(zone) {
        for (var i = 0; i < zone.cameras.length; ++i) {
            var cam = zone.cameras[i];
            for (var j = 0; j < cam.channels.length; ++j) {
                var rLevel = root.riskLevels[cam.channels[j].streamId] || 0;
                if (rLevel > 0) return true;
            }
        }
        return false;
    }

    ListView {
        anchors.fill: parent
        spacing: Theme.spacingSm
        clip: true
        model: root.zones

        delegate: Rectangle {
            id: zoneCard
            width: ListView.view.width
            height: zoneColumn.implicitHeight + Theme.spacingMd * 2
            radius: Theme.radiusMd
            color: Theme.colorSurface
            border.width: Theme.borderWidthHairline
            border.color: isZoneAlert(modelData) ? Theme.colorDanger : Theme.colorBorder

            Column {
                id: zoneColumn
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: Theme.spacingMd
                spacing: Theme.spacingSm

                // 1단계: 구역(Zone) 헤더
                RowLayout {
                    width: parent.width
                    spacing: Theme.spacingSm

                    Text {
                        text: "📁 " + modelData.zoneId + " (" + modelData.zoneName + ")"
                        color: isZoneAlert(modelData) ? Theme.colorDanger : "#ffffff"
                        font.pixelSize: 14
                        font.weight: Font.Bold
                        Layout.fillWidth: true
                    }

                    Rectangle {
                        visible: isZoneAlert(modelData)
                        implicitWidth: alertText.implicitWidth + 12
                        height: 22
                        radius: Theme.radiusSm
                        color: Theme.colorDangerBg
                        border.width: 1
                        border.color: Theme.colorDanger

                        Text {
                            id: alertText
                            anchors.centerIn: parent
                            text: "🚨 위험 감지"
                            color: Theme.colorDanger
                            font.pixelSize: 11
                            font.weight: Font.Bold
                        }
                    }
                }

                // 2단계: 물리 카메라 (Camera)
                Repeater {
                    model: modelData.cameras

                    delegate: Column {
                        width: zoneColumn.width
                        spacing: 6

                        // 카메라 명칭
                        Text {
                            text: "📹 " + modelData.cameraId + " · " + modelData.cameraModel
                            color: "#cbd5e1"
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }

                        // 3단계: 채널 (Channel)
                        Repeater {
                            model: modelData.channels

                            delegate: Rectangle {
                                id: channelItem
                                width: zoneColumn.width
                                height: 42
                                radius: Theme.radiusSm
                                color: chMouseArea.containsMouse ? "#27354a" : "#141a29"
                                border.width: 1
                                border.color: riskLevel > 0 ? Theme.riskColor(riskLevel) : (chMouseArea.containsMouse ? Theme.colorAccent : "#2d3748")

                                readonly property int connState: root.connStates[modelData.streamId] !== undefined 
                                                                 ? root.connStates[modelData.streamId] 
                                                                 : cameraListModel.videoConnectionStateFor(modelData.streamId)
                                readonly property int riskLevel: root.riskLevels[modelData.streamId] !== undefined 
                                                                 ? root.riskLevels[modelData.streamId] 
                                                                 : cameraListModel.riskLevelFor(modelData.streamId)
                                readonly property real distM: root.distValues[modelData.streamId] !== undefined 
                                                              ? root.distValues[modelData.streamId] 
                                                              : cameraListModel.distanceMFor(modelData.streamId)

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: Theme.spacingMd
                                    anchors.rightMargin: Theme.spacingSm
                                    spacing: Theme.spacingSm

                                    // 연결선 기호
                                    Text {
                                        text: index === 3 ? "└─" : "├─"
                                        color: "#64748b"
                                        font.pixelSize: 12
                                    }

                                    // 실시간 연결 상태 점 (초록/주황/빨강)
                                    Rectangle {
                                        width: 8
                                        height: 8
                                        radius: 4
                                        color: Theme.connectionColor(connState)
                                    }

                                    // 채널 이름 (순백색)
                                    Text {
                                        text: modelData.name
                                        color: "#ffffff"
                                        font.pixelSize: 12
                                        font.weight: Font.Medium
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                    }

                                    // 위험 발생 시 뱃지
                                    Rectangle {
                                        visible: riskLevel > 0
                                        implicitWidth: badgeText.implicitWidth + 10
                                        height: 22
                                        radius: Theme.radiusSm
                                        color: Theme.riskBgColor(riskLevel)
                                        border.width: 1
                                        border.color: Theme.riskColor(riskLevel)

                                        Text {
                                            id: badgeText
                                            anchors.centerIn: parent
                                            text: Theme.riskLabel(riskLevel) + (distM > 0 ? " " + Math.round(distM * 1000) + " mm" : "")
                                            color: Theme.riskColor(riskLevel)
                                            font.pixelSize: 11
                                            font.weight: Font.Bold
                                        }
                                    }
                                }

                                MouseArea {
                                    id: chMouseArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.cameraSelected(modelData.streamId)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
