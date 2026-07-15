import QtQuick
import Safety.Common

// Single-camera detail view shown when a grid card is clicked. Listens to
// MetadataService::metadataUpdated for this camera_id to keep the overlay
// (bbox/distance) live without polling.
Item {
    id: root
    property string cameraId: ""
    signal backRequested()

    property real distanceM: 0
    property bbox personBBox
    property bbox forkliftBBox
    property int riskLevel: 0
    property int exceptionState: 0

    Connections {
        target: metadataService
        function onMetadataUpdated(metadata) {
            if (metadata.cameraId !== root.cameraId)
                return
            root.distanceM = metadata.distanceM
            root.personBBox = metadata.personBBox
            root.forkliftBBox = metadata.forkliftBBox
            root.riskLevel = metadata.riskLevel
            root.exceptionState = metadata.exceptionState
        }
    }

    Component.onCompleted: {
        const latest = metadataService.latestFor(root.cameraId)
        root.distanceM = latest.distanceM
        root.personBBox = latest.personBBox
        root.forkliftBBox = latest.forkliftBBox
        root.riskLevel = latest.riskLevel
        root.exceptionState = latest.exceptionState
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.colorSurface
        radius: Theme.radiusMd
        border.color: root.riskLevel !== 0 ? Theme.riskColor(root.riskLevel) : Theme.colorBorder
        border.width: root.riskLevel !== 0 ? 2 : 1

        Item {
            id: header
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: Theme.spacingMd
            height: 36

            Rectangle {
                id: backButton
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 36
                height: 36
                radius: Theme.radiusSm
                color: Theme.colorSurfaceElevated
                border.color: Theme.colorBorder
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "←"
                    color: Theme.colorTextPrimary
                    font.pixelSize: Theme.fontSizeLg
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.backRequested()
                }
            }

            Text {
                id: idText
                anchors.left: backButton.right
                anchors.leftMargin: Theme.spacingMd
                anchors.verticalCenter: parent.verticalCenter
                text: root.cameraId
                color: Theme.colorTextPrimary
                font.pixelSize: Theme.fontSizeLg
                font.bold: true
            }

            RiskBanner {
                anchors.left: idText.right
                anchors.leftMargin: Theme.spacingMd
                anchors.verticalCenter: parent.verticalCenter
                visible: root.riskLevel !== 0
                riskLevel: root.riskLevel
                exceptionState: root.exceptionState
            }
        }

        Item {
            anchors.top: header.bottom
            anchors.topMargin: Theme.spacingMd
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.spacingMd
            clip: true

            CameraVideoView {
                anchors.fill: parent
                cameraId: root.cameraId
                personBBox: root.personBBox
                forkliftBBox: root.forkliftBBox
                distanceM: root.distanceM
            }
        }
    }
}
