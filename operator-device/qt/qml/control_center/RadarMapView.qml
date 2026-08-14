import QtQuick
import Safety.Common

// 2D 실시간 공장 레이더 평면도 (N-카메라 동적 지원)
// cameraListModel의 카메라/구역 목록을 기반으로 동적 노드를 생성하고 실시간 위치 및 위험도를 시각화합니다.
Rectangle {
    id: root
    signal cameraSelected(string cameraId)

    color: Theme.colorSurface
    radius: Theme.radiusMd
    border.width: Theme.borderWidthHairline
    border.color: Theme.colorBorder
    clip: true

    // 배경 그리드 라인
    Canvas {
        id: gridCanvas
        anchors.fill: parent
        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.strokeStyle = "rgba(255, 255, 255, 0.04)"
            ctx.lineWidth = 1

            var step = 30
            for (var x = step; x < width; x += step) {
                ctx.beginPath()
                ctx.moveTo(x, 0)
                ctx.lineTo(x, height)
                ctx.stroke()
            }
            for (var y = step; y < height; y += step) {
                ctx.beginPath()
                ctx.moveTo(0, y)
                ctx.lineTo(width, y)
                ctx.stroke()
            }

            // 동심원 레이더 그리드
            ctx.strokeStyle = "rgba(79, 140, 247, 0.08)"
            var cx = width / 2
            var cy = height / 2
            for (var r = 40; r < Math.max(width, height) / 1.5; r += 50) {
                ctx.beginPath()
                ctx.arc(cx, cy, r, 0, Math.PI * 2)
                ctx.stroke()
            }
        }
    }

    // 상단 타이틀 바
    Row {
        id: titleRow
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingSm

        Rectangle {
            width: 8
            height: 8
            radius: 4
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.colorAccent

            SequentialAnimation on opacity {
                loops: Animation.Infinite
                NumberAnimation { from: 0.3; to: 1.0; duration: 1000; easing.type: Easing.InOutQuad }
                NumberAnimation { from: 1.0; to: 0.3; duration: 1000; easing.type: Easing.InOutQuad }
            }
        }

        Text {
            text: "실시간 현장 레이더 (2D 맵)"
            color: Theme.colorTextPrimary
            font.pixelSize: Theme.typeHeading.size
            font.weight: Font.Medium
        }
    }

    // N-카메라/구역 동적 노드 배치 영역
    Item {
        id: mapArea
        anchors.top: titleRow.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingMd

        // 4분할 또는 N분할 구역 렌더링
        Repeater {
            model: cameraListModel

            delegate: Rectangle {
                id: zoneNode
                // N대 카메라를 2열 기준으로 자동 그리드 좌표 계산
                property int cols: 2
                property int rowIdx: Math.floor(index / cols)
                property int colIdx: index % cols
                property int totalRows: Math.max(1, Math.ceil(cameraListModel.rowCount() / cols))

                width: (mapArea.width - Theme.spacingMd) / cols
                height: (mapArea.height - (totalRows - 1) * Theme.spacingMd) / totalRows
                x: colIdx * (width + Theme.spacingMd)
                y: rowIdx * (height + Theme.spacingMd)

                radius: Theme.radiusSm
                color: model.riskLevel !== 0
                       ? Theme.riskBgColor(model.riskLevel)
                       : Qt.rgba(0.08, 0.12, 0.18, 0.6)
                border.width: Theme.borderWidthHairline
                border.color: model.riskLevel !== 0
                              ? Theme.riskColor(model.riskLevel)
                              : Theme.colorBorder

                Behavior on color { ColorAnimation { duration: Theme.animationNormal } }
                Behavior on border.color { ColorAnimation { duration: Theme.animationNormal } }

                // 구역명 및 카메라 ID
                Row {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.margins: Theme.spacingSm
                    spacing: Theme.spacingXs

                    Text {
                        text: model.zone.length > 0 ? model.zone : ("Zone " + (index + 1))
                        color: model.riskLevel !== 0 ? Theme.riskColor(model.riskLevel) : Theme.colorTextSecondary
                        font.pixelSize: Theme.typeLabel.size
                        font.weight: Font.Medium
                    }

                    Text {
                        text: "[" + model.cameraId + "]"
                        color: Theme.colorTextMuted
                        font.pixelSize: Theme.typeCaption.size
                    }
                }

                // 지게차 심볼
                Rectangle {
                    anchors.centerIn: parent
                    width: 24
                    height: 24
                    radius: 4
                    color: Theme.colorForkliftBox

                    Text {
                        anchors.centerIn: parent
                        text: "🚜"
                        font.pixelSize: 12
                    }
                }

                // 작업자 닷 (위험 발생 시 실시간 애니메이션)
                Rectangle {
                    visible: model.riskLevel !== 0
                    width: 10
                    height: 10
                    radius: 5
                    x: parent.width * 0.7
                    y: parent.height * 0.4
                    color: Theme.colorPersonBox

                    SequentialAnimation on scale {
                        loops: Animation.Infinite
                        running: zoneNode.visible && model.riskLevel !== 0
                        NumberAnimation { from: 0.8; to: 1.4; duration: 600; easing.type: Easing.InOutQuad }
                        NumberAnimation { from: 1.4; to: 0.8; duration: 600; easing.type: Easing.InOutQuad }
                    }
                }

                // 구역 클릭 시 카메라 선택 신호 방출
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    hoverEnabled: true
                    onClicked: root.cameraSelected(model.cameraId)
                }
            }
        }
    }
}
