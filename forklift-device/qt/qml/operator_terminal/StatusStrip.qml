import QtQuick
import Safety.Common

// Server / camera / sensor connection status: an icon-centric compact row
// anchored to a corner over the video. Output-only display, no touch
// target sizing needed -- width follows content instead of a fixed card.
Rectangle {
    id: root
    width: contentColumn.implicitWidth + Theme.spacingMd * 2
    height: contentColumn.implicitHeight + Theme.spacingSm * 2
    radius: Theme.radiusMd
    color: Qt.rgba(Theme.colorSurface.r, Theme.colorSurface.g, Theme.colorSurface.b, 0.82)
    border.color: Theme.colorBorder
    border.width: Theme.borderWidthHairline

    // Anchored bottom-left over the video, so growth (badge row appearing)
    // has to happen upward/rightward only -- the parent anchors left+bottom,
    // never letting this cross into the video center or the RiskHud above.
    Column {
        id: contentColumn
        anchors.centerIn: parent
        spacing: Theme.spacingXs

        Row {
            id: contentRow
            spacing: Theme.spacingMd

            ConnectionIndicator {
                compact: true
                label: "서버"
                state: serverConnection.connectionState
            }
            ConnectionIndicator {
                compact: true
                label: "카메라"
                state: activeCamera.videoConnectionState
            }
            ConnectionIndicator {
                compact: true
                label: "센서"
                // No dedicated sensor channel yet: sensor health rides on the
                // metadata pipeline, degraded explicitly by a SENSOR_FAULT
                // exception.
                state: activeCamera.exceptionState === 1 ? 0 : metadataDistributor.connectionState
            }
            ConnectionIndicator {
                compact: true
                label: "FPGA"
                state: activeCamera.fpgaConnectionState
            }
        }

        // FPGA 패킷 파싱 누적 오류(체크섬/프로토콜/타임아웃) 표시줄. 평소엔 visible이
        // false라 자리를 안 차지하다가, 하나라도 누적되면 나타남 -- ESTOP/전진차단처럼
        // 즉시 위험한 상태는 아니라서 전체화면 오버레이(EstopOverlay) 대신 여기 붙임.
        // "누적"이라는 단어와 아래 안내 문구로 지금 순간의 오류가 아니라 정비 전까지
        // 계속 유지되는 상태임을 알림 -- 색만으로 표시하지 않음.
        StatusBadge {
            visible: activeCamera.fpgaErrorLatched
            icon: "⚠"
            text: "FPGA 오류 누적: " + Theme.fpgaLatchLabel(activeCamera.checksumErrorLatched,
                                                            activeCamera.protocolErrorLatched,
                                                            activeCamera.timeoutErrorLatched)
            accentColor: Theme.colorCaution
        }

        Text {
            visible: activeCamera.fpgaErrorLatched
            text: "정비자가 초기화할 때까지 유지됩니다"
            color: Theme.colorTextMuted
            font.pixelSize: Theme.typeCaption.size
        }
    }
}
