import QtQuick
import QtQuick.Layouts
import Safety.Common

// Aerodynamic Floating Glass Pill HUD for the driver:
// Displays exact risk level, live distance, and crystal-clear driver action guidance.
// Hidden during SAFE (Clean View) and fades in smoothly on Caution/Danger/Emergency/Exceptions.
Rectangle {
    id: root
    property int riskLevel: 0
    property int exceptionState: 0
    property real distanceM: 0
    property bool distanceValid: true

    readonly property bool isAlert: riskLevel > 0
    readonly property bool hasException: exceptionState > 0
    readonly property color accent: hasException && riskLevel === 0 ? Theme.colorCaution : Theme.riskColor(riskLevel)

    implicitWidth: hudRow.implicitWidth + 32
    height: 42
    radius: Theme.radiusPill
    color: Qt.rgba(0.06, 0.08, 0.12, 0.90)
    border.width: 1.5
    border.color: root.accent

    visible: opacity > 0
    opacity: (isAlert || hasException) ? 1.0 : 0.0

    Behavior on opacity { NumberAnimation { duration: 200 } }
    Behavior on border.color { ColorAnimation { duration: 150 } }

    RowLayout {
        id: hudRow
        anchors.centerIn: parent
        spacing: 12

        // 1) 상태 태그 뱃지
        Rectangle {
            implicitWidth: tagText.implicitWidth + 14
            height: 24
            radius: 4
            color: root.accent

            Text {
                id: tagText
                anchors.centerIn: parent
                color: "#ffffff"
                font.pixelSize: 11
                font.bold: true
                text: {
                    if (root.hasException && root.riskLevel === 0) {
                        switch (root.exceptionState) {
                        case 1: return "센서 점검"
                        case 2: return "시야 차폐"
                        case 3: return "통신 단절"
                        case 4: return "자율 항법"
                        case 5: return "거리 미확인"
                        default: return "상태 알림"
                        }
                    }
                    switch (root.riskLevel) {
                    case 1: return "주의 [CAUTION]"
                    case 2: return "경보 [DANGER]"
                    case 3: return "비상 [EMERGENCY]"
                    default: return "정상 [SAFE]"
                    }
                }
            }
        }

        // 2) 실시간 접근 거리 (위험 발생 시)
        Text {
            visible: root.isAlert
            text: root.distanceValid ? (root.distanceM.toFixed(2) + " m") : "접근 감지"
            color: root.accent
            font.pixelSize: 16
            font.bold: true
        }

        // 구분선 (위험 시)
        Rectangle {
            visible: root.isAlert
            width: 1
            height: 14
            color: Qt.rgba(1, 1, 1, 0.2)
        }

        // 3) 운전자 행동 지침 가이드
        Text {
            color: Theme.colorTextPrimary
            font.pixelSize: 13
            font.bold: true
            text: {
                if (root.hasException && root.riskLevel === 0) {
                    switch (root.exceptionState) {
                    case 1: return "센서 이상 감지 · 육안 안전 확인 필요"
                    case 2: return "카메라 시야 차폐됨 · 서행 운전"
                    case 3: return "관제 통신 일시 끊김 · 로컬 감시 중"
                    case 4: return "카메라 음영 · IMU 추측항법 가동 중"
                    case 5: return "접근 거리 측정 불가 · 주의 운전"
                    default: return "안전 상태 확인"
                    }
                }
                switch (root.riskLevel) {
                case 1: return "보행자 접근 · 서행 감속"
                case 2: return "충돌 위험 · 즉시 정지"
                case 3: return "비상 제동 구역 · 전진 차단"
                default: return "안전 거리 유지 중"
                }
            }
        }
    }
}
