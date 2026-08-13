import QtQuick
import Safety.Common

// Composes the C++ VideoStream (video surface) with a DetectionOverlay
// on top. UI code just sets cameraId/personBBox/forkliftBBox/distanceM --
// it never touches the video source directly, so it never knows whether
// that camera is Mock, a local file, or RTSP.
Item {
    id: root
    property string cameraId: ""
    property bbox personBBox
    property bbox forkliftBBox
    property real distanceM: 0
    property bool distanceValid: true
    property bool showOverlay: true

    readonly property int connectionState: videoItem.connectionState
    readonly property real fps: videoItem.fps
    readonly property size videoNativeSize: videoItem.videoSize

    VideoStream {
        id: videoItem
        anchors.fill: parent
        cameraId: root.cameraId
    }

    DetectionOverlay {
        anchors.fill: parent
        visible: root.showOverlay && !videoItem.switching
        videoSize: videoItem.videoSize
        personBBox: root.personBBox
        forkliftBBox: root.forkliftBBox
        distanceM: root.distanceM
        distanceValid: root.distanceValid
        personColor: Theme.colorPersonBox
        forkliftColor: Theme.colorForkliftBox
        lineColor: Theme.colorTextPrimary
    }
}
