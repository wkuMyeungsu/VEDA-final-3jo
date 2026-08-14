import QtQuick
import Safety.Common

// Server / camera / sensor connection status: an icon-centric compact row
// anchored to a corner over the video. Output-only display, no touch
// target sizing needed -- width follows content instead of a fixed card.
Rectangle {
    id: root
    width: contentRow.implicitWidth + Theme.spacingMd * 2
    height: contentRow.implicitHeight + Theme.spacingSm * 2
    radius: Theme.radiusMd
    color: Qt.rgba(Theme.colorSurface.r, Theme.colorSurface.g, Theme.colorSurface.b, 0.82)
    border.color: Theme.colorBorder
    border.width: Theme.borderWidthHairline

    Row {
        id: contentRow
        anchors.centerIn: parent
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
}
