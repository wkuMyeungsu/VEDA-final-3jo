import QtQuick
import Safety.Common

// One tile in the control-center grid: video + overlay, a risk banner that
// only appears above CAUTION, a highlighted border while at risk, and a
// footer with name/zone/connection state. Click anywhere on the video to
// expand it.
Rectangle {
    id: root
    property string cameraId: ""
    property string cameraName: ""
    property string zone: ""
    property int riskLevel: 0
    property int exceptionState: 0
    property real distanceM: 0
    property int videoConnectionState: 0

    signal clicked()

    readonly property bool isAlert: riskLevel !== 0 || exceptionState !== 0

    color: Theme.colorSurface
    radius: Theme.radiusMd
    border.width: isAlert ? 2 : 1
    border.color: isAlert ? Theme.riskColor(riskLevel) : Theme.colorBorder

    Behavior on border.color { ColorAnimation { duration: Theme.animationNormal } }

    RiskBanner {
        id: banner
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingSm
        visible: root.isAlert
        riskLevel: root.riskLevel
        exceptionState: root.exceptionState
    }

    Item {
        id: videoArea
        anchors.top: root.isAlert ? banner.bottom : parent.top
        anchors.topMargin: Theme.spacingSm
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: headerRow.top
        anchors.bottomMargin: Theme.spacingXs
        anchors.leftMargin: Theme.spacingSm
        anchors.rightMargin: Theme.spacingSm
        clip: true

        CameraVideoView {
            anchors.fill: parent
            cameraId: root.cameraId
            distanceM: root.distanceM
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.clicked()
        }
    }

    Row {
        id: headerRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacingSm
        height: 18
        spacing: Theme.spacingSm

        Text {
            text: root.cameraName + " (" + root.cameraId + ")"
            color: Theme.colorTextPrimary
            font.pixelSize: Theme.fontSizeSm
            font.bold: true
            elide: Text.ElideRight
            width: 150
        }
        Text {
            text: root.zone
            color: Theme.colorTextSecondary
            font.pixelSize: Theme.fontSizeSm
        }
        Text {
            text: Theme.connectionLabel(root.videoConnectionState)
            color: Theme.connectionColor(root.videoConnectionState)
            font.pixelSize: Theme.fontSizeSm
        }
    }
}
