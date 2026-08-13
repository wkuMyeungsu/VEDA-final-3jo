import QtQuick
import Safety.Common

// Zone + camera name, shown over the video in a translucent strip.
Rectangle {
    id: root
    color: Qt.rgba(Theme.colorSurface.r, Theme.colorSurface.g, Theme.colorSurface.b, 0.82)

    Column {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Theme.spacingLg
        spacing: Theme.spacingXxs

        Text {
            text: activeCamera.zone
            color: Theme.colorTextSecondary
            font.pixelSize: Theme.typeCaption.size
        }
        Text {
            text: activeCamera.cameraName + " (" + activeCamera.activeCameraId + ")"
            color: Theme.colorTextPrimary
            font.pixelSize: Theme.typeHeading.size
            font.bold: true
        }
    }
}
