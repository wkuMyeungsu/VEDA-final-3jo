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
    property int riskLevel: 0

    // TTC 예측 실험 표시용 (기본값=무효 -- 이 값을 안 채우는 화면(예: 그리드 카드)은
    // 아무것도 안 그려짐, TtcExperiment와 무관하게 항상 안전한 기본 상태)
    property bool ttcValid: false
    property real ttcSeconds: 0
    property real ttcConfidence: 0
    property string ttcReason: ""

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
        visible: root.showOverlay
        videoSize: videoItem.videoSize
        personBBox: root.personBBox
        forkliftBBox: root.forkliftBBox
        distanceM: root.distanceM
        distanceValid: root.distanceValid
        personColor: root.riskLevel !== 0 ? Theme.riskColor(root.riskLevel) : Theme.colorPersonBox
        forkliftColor: Theme.colorForkliftBox
        lineColor: Theme.colorTextPrimary
        ttcValid: root.ttcValid
        ttcSeconds: root.ttcSeconds
        ttcConfidence: root.ttcConfidence
        ttcReason: root.ttcReason
        ttcColor: Theme.colorTextMuted
    }
}
