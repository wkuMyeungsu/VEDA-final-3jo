pragma Singleton
import QtQuick

// Single source of truth for colors, spacing, typography and animation
// timings across both apps. Nothing here talks to C++; it is pure design
// tokens plus a handful of small mapping helpers (risk level -> color,
// connection state -> label) so QML never repeats those switch statements.
QtObject {
    id: themeRoot
    // --- Surfaces (Hanwha Vision Matte Charcoal & High-Tech Obsidian Slate) ---
    readonly property color colorBackground: "#0c0e12"
    readonly property color colorSurface: "#16191f"
    readonly property color colorSurfaceElevated: "#20252e"
    readonly property color colorSurfaceGlass: Qt.rgba(0.09, 0.10, 0.13, 0.94)
    readonly property color colorBorder: Qt.rgba(1, 1, 1, 0.14)
    readonly property color colorBorderStrong: Qt.rgba(1, 1, 1, 0.25)
    readonly property color colorBorderAccent: Qt.rgba(0.95, 0.45, 0.13, 0.65)

    // --- Text (고대비 명도 개선: 어두운 곳에서도 또렷한 가독성) ---
    readonly property color colorTextPrimary: "#ffffff"
    readonly property color colorTextSecondary: "#d1d5db"
    readonly property color colorTextMuted: "#9ca3af"

    // --- Brand Accent (한화비전 시그니처 오렌지 & 스마트 안전 관제 톤) ---
    readonly property color colorAccent: "#F37321"
    readonly property color colorAccentAlpha20: Qt.rgba(0.95, 0.45, 0.13, 0.20)
    readonly property color colorFocusRing: colorAccent          // - 포커스 링 전용 별칭
    readonly property color colorSurfaceSunken: "#08090c"        // - 카드보다 어두운 침하 표면 (영상 웰 등)
    readonly property color colorScrim: Qt.rgba(0, 0, 0, 0.75)   // - 모달/오버레이 배경 딤
    readonly property color colorHairlineTop: Qt.rgba(1, 1, 1, 0.10)   // - 융기 표면 상단 하이라이트 선
    readonly property color colorHoverOverlay: Qt.rgba(1, 1, 1, 0.08) // - hover 시 덧칠 오버레이
    readonly property color colorPressOverlay: Qt.rgba(0, 0, 0, 0.20) // - press 시 덧칠 오버레이

    // --- Risk levels (생생하고 또렷한 비비드 톤) ---
    readonly property color colorSafe: "#10b981"
    readonly property color colorSafeBg: Qt.rgba(0.06, 0.72, 0.50, 0.25)
    readonly property color colorCaution: "#f59e0b"
    readonly property color colorCautionBg: Qt.rgba(0.96, 0.62, 0.04, 0.35)
    readonly property color colorDanger: "#ef4444"
    readonly property color colorDangerBg: Qt.rgba(0.94, 0.27, 0.27, 0.55)
    readonly property color colorEmergency: "#f43f5e"
    readonly property color colorEmergencyBg: Qt.rgba(0.96, 0.25, 0.37, 0.70)

    // 통신/카메라 끊김 등으로 riskLevel을 신뢰할 수 없을 때 쓰는 중립색 (안전으로 오인 방지)
    readonly property color colorUnknown: "#d1d5db"
    readonly property color colorUnknownBg: Qt.rgba(0.58, 0.64, 0.72, 0.22)

    readonly property color colorPersonBox: "#38bdf8"
    readonly property color colorForkliftBox: "#F37321"

    // --- Spacing scale ---
    readonly property int spacingXxs: 2
    readonly property int spacingXs: 4
    readonly property int spacingSm: 8
    readonly property int spacingMd: 12
    readonly property int spacingLg: 18
    readonly property int spacingXl: 28

    // --- Corner radius ---
    readonly property int radiusXs: 4
    readonly property int radiusSm: 6
    readonly property int radiusMd: 10
    readonly property int radiusLg: 14
    readonly property int radiusPill: 999             // - 높이에 무관하게 완전한 알약형 보장

    // --- Border width ---
    readonly property int borderWidthHairline: 1
    readonly property int borderWidthAlert: 1

    // --- Typography (시원하고 또렷한 프리텐다드 스케일) ---
    readonly property string fontFamily: "Pretendard, 'Segoe UI Variable Text', 'Segoe UI', 'Malgun Gothic', sans-serif"
    readonly property string fontMono: "'Consolas', 'Cascadia Code', monospace"

    readonly property QtObject typeDisplay: QtObject {   // - 최상위 대형 수치/거리
        readonly property string family: themeRoot.fontFamily
        readonly property int size: 38
        readonly property int weight: Font.Bold
        readonly property real spacing: -0.5
        readonly property bool tabular: true
    }
    readonly property QtObject typeTitle: QtObject {     // - 앱바 시스템명, 메인 타이틀
        readonly property string family: themeRoot.fontFamily
        readonly property int size: 20
        readonly property int weight: Font.Bold
        readonly property real spacing: -0.3
        readonly property bool tabular: false
    }
    readonly property QtObject typeHeading: QtObject {   // - 패널 제목, 카드 타이틀
        readonly property string family: themeRoot.fontFamily
        readonly property int size: 16
        readonly property int weight: Font.DemiBold
        readonly property real spacing: -0.2
        readonly property bool tabular: false
    }
    readonly property QtObject typeBody: QtObject {      // - 일반 본문 텍스트
        readonly property string family: themeRoot.fontFamily
        readonly property int size: 14
        readonly property int weight: Font.Normal
        readonly property real spacing: -0.1
        readonly property bool tabular: false
    }
    readonly property QtObject typeLabel: QtObject {     // - 버튼/태그/컬럼 헤더
        readonly property string family: themeRoot.fontFamily
        readonly property int size: 13
        readonly property int weight: Font.DemiBold
        readonly property real spacing: 0
        readonly property bool tabular: false
    }
    readonly property QtObject typeCaption: QtObject {   // - 보조 설명/타임스탬프
        readonly property string family: themeRoot.fontFamily
        readonly property int size: 12
        readonly property int weight: Font.Medium
        readonly property real spacing: 0
        readonly property bool tabular: false
    }

    // --- Layout (쾌적한 여백과 든든한 헤더 크기) ---
    readonly property int appBarHeight: 56
    readonly property int topBarHeight: 44
    readonly property int rightPanelWidth: 360
    readonly property int eventLogHeight: 220
    readonly property int cameraCardFooterHeight: 28
    readonly property int cameraCardChromeHeight: 42
    readonly property int tableRowHeight: 24
    readonly property int alertRowHeight: 48
    readonly property int statusRowHeight: 40
    readonly property int iconButtonSize: 32
    readonly property int connectionDotSize: 8

    // --- Animation ---
    readonly property int animationFast: 120
    readonly property int animationNormal: 220
    readonly property int animationSlow: 420
    readonly property int easingStandard: Easing.InOutQuad

    // RiskTypes.RiskLevel: 0 Safe, 1 Caution, 2 Danger, 3 Emergency
    function riskColor(level) {
        switch (level) {
        case 1: return colorCaution
        case 2: return colorDanger
        case 3: return colorEmergency
        default: return colorSafe
        }
    }

    function riskBgColor(level) {
        switch (level) {
        case 1: return colorCautionBg
        case 2: return colorDangerBg
        case 3: return colorEmergencyBg
        default: return colorSafeBg
        }
    }

    function riskLabel(level) {
        switch (level) {
        case 1: return "CAUTION"
        case 2: return "DANGER"
        case 3: return "EMERGENCY"
        default: return "SAFE"
        }
    }

    function riskIcon(level) {
        switch (level) {
        case 1: return "⚠"
        case 2: return "✖"
        case 3: return "✖"
        default: return "✓"
        }
    }

    // RiskTypes.ExceptionState: 0 None .. 6 UnconfirmedProximity
    function exceptionLabel(state) {
        switch (state) {
        case 1: return "SENSOR_FAULT"
        case 2: return "DEAD_RECKONING"
        case 3: return "EMERGENCY_IMPACT"
        case 4: return "NETWORK_DISCONNECTED"
        case 5: return "CAMERA_DISCONNECTED"
        case 6: return "NOT CONFIRMED"
        default: return "NONE"
        }
    }

    // RiskTypes.ConnectionState: 0 Disconnected, 1 Connecting, 2 Connected
    function connectionLabel(state) {
        switch (state) {
        case 2: return "CONNECTED"
        case 1: return "CONNECTING"
        default: return "DISCONNECTED"
        }
    }

    function connectionColor(state) {
        switch (state) {
        case 2: return colorSafe
        case 1: return colorCaution
        default: return colorTextMuted
        }
    }

    // 카드/확대뷰가 공유하는 "경보 중" 테두리 규칙 -- 한쪽만 고치고 잊어버리는
    // 드리프트 방지용 (CameraCard·ExpandedCameraView가 각자 복제해서 쓰던 로직)
    function alertBorderColor(riskLevel, exceptionState) {
        if (exceptionState !== 0)
            return colorUnknown
        return (riskLevel !== 0) ? riskColor(riskLevel) : colorBorder
    }

    function alertBorderWidth(riskLevel, exceptionState) {
        return (riskLevel !== 0 || exceptionState !== 0) ? borderWidthAlert : borderWidthHairline
    }
}
