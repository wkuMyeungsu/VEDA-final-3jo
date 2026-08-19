pragma Singleton
import QtQuick

// Single source of truth for colors, spacing, typography and animation
// timings across both apps. Nothing here talks to C++; it is pure design
// tokens plus a handful of small mapping helpers (risk level -> color,
// connection state -> label) so QML never repeats those switch statements.
QtObject {
    // --- Surfaces (Deep Slate & Glass) ---
    readonly property color colorBackground: "#080c14"
    readonly property color colorSurface: "#101622"
    readonly property color colorSurfaceElevated: "#161e2e"
    readonly property color colorSurfaceGlass: Qt.rgba(0.06, 0.09, 0.14, 0.85)
    readonly property color colorBorder: Qt.rgba(1, 1, 1, 0.07)
    readonly property color colorBorderStrong: Qt.rgba(1, 1, 1, 0.14)
    readonly property color colorBorderAccent: Qt.rgba(0.31, 0.55, 0.97, 0.35)

    // --- Text ---
    readonly property color colorTextPrimary: "#f1f5f9"
    readonly property color colorTextSecondary: "#94a3b8"
    readonly property color colorTextMuted: "#64748b"

    readonly property color colorAccent: "#4f8cf7"
    readonly property color colorAccentAlpha20: Qt.rgba(0.31, 0.55, 0.97, 0.18)
    readonly property color colorFocusRing: colorAccent          // - 포커스 링 전용 별칭
    readonly property color colorSurfaceSunken: "#060910"        // - 카드보다 어두운 침하 표면 (영상 웰 등)
    readonly property color colorScrim: Qt.rgba(0, 0, 0, 0.65)   // - 모달/오버레이 배경 딤
    readonly property color colorHairlineTop: Qt.rgba(1, 1, 1, 0.05)   // - 융기 표면 상단 하이라이트 선
    readonly property color colorHoverOverlay: Qt.rgba(1, 1, 1, 0.04) // - hover 시 덧칠 오버레이
    readonly property color colorPressOverlay: Qt.rgba(0, 0, 0, 0.15) // - press 시 덧칠 오버레이

    // --- Risk levels (Modern vibrant tone; Bg 불투명도는 위험도에 비례 -- 안전 배너 시인성 확보) ---
    readonly property color colorSafe: "#22c55e"
    readonly property color colorSafeBg: Qt.rgba(0.13, 0.77, 0.37, 0.20)
    readonly property color colorCaution: "#f59e0b"
    readonly property color colorCautionBg: Qt.rgba(0.96, 0.62, 0.04, 0.35)
    readonly property color colorDanger: "#ef4444"
    readonly property color colorDangerBg: Qt.rgba(0.94, 0.27, 0.27, 0.55)
    readonly property color colorEmergency: "#f43f5e"
    readonly property color colorEmergencyBg: Qt.rgba(0.96, 0.25, 0.37, 0.70)

    // 통신/카메라 끊김 등으로 riskLevel을 신뢰할 수 없을 때 쓰는 중립색 (안전으로 오인 방지)
    readonly property color colorUnknown: "#94a3b8"
    readonly property color colorUnknownBg: Qt.rgba(0.58, 0.64, 0.72, 0.10)

    readonly property color colorPersonBox: "#38bdf8"
    readonly property color colorForkliftBox: "#fb923c"

    // --- Spacing scale ---
    readonly property int spacingXxs: 2
    readonly property int spacingXs: 4
    readonly property int spacingSm: 8
    readonly property int spacingMd: 12
    readonly property int spacingLg: 20
    readonly property int spacingXl: 32

    // --- Corner radius ---
    readonly property int radiusSm: 8
    readonly property int radiusMd: 12
    readonly property int radiusLg: 14
    readonly property int radiusPill: 999             // - 높이에 무관하게 완전한 알약형 보장

    // --- Border width ---
    readonly property int borderWidthHairline: 1
    readonly property int borderWidthAlert: 2

    // --- Typography (크기 · 굵기 · 자간 · 수치정렬을 함께 이동) ---
    readonly property QtObject typeDisplay: QtObject {   // - 단말 거리 표시 등 최상위 수치
        readonly property int size: 42
        readonly property int weight: Font.Bold
        readonly property real spacing: -0.5
        readonly property bool tabular: true             // - 수치 갱신 시 가로 흔들림 방지
    }
    readonly property QtObject typeTitle: QtObject {     // - 앱바 시스템명, 확대뷰 카메라ID
        readonly property int size: 26
        readonly property int weight: Font.Bold
        readonly property real spacing: 0
        readonly property bool tabular: false
    }
    readonly property QtObject typeHeading: QtObject {   // - 패널/데모패널 제목
        readonly property int size: 18
        readonly property int weight: Font.Bold
        readonly property real spacing: 0
        readonly property bool tabular: false
    }
    readonly property QtObject typeBody: QtObject {      // - 일반 본문 텍스트
        readonly property int size: 14
        readonly property int weight: Font.Normal
        readonly property real spacing: 0
        readonly property bool tabular: false
    }
    readonly property QtObject typeLabel: QtObject {     // - 섹션 제목 · 컬럼 헤더 (대문자성 + 자간)
        readonly property int size: 12
        readonly property int weight: Font.Bold
        readonly property real spacing: 1
        readonly property bool tabular: false
    }
    readonly property QtObject typeCaption: QtObject {   // - 보조/부가 텍스트
        readonly property int size: 12
        readonly property int weight: Font.Normal
        readonly property real spacing: 0
        readonly property bool tabular: false
    }

    // --- Layout (기존 화면 상수를 동일 값으로 흡수) ---
    readonly property int appBarHeight: 56
    readonly property int topBarHeight: 40             // - 단말 TopBar, 800x480 기준 축소(기존 64)
    readonly property int rightPanelWidth: 340
    readonly property int eventLogHeight: 200
    readonly property int cameraCardFooterHeight: 18
    readonly property int cameraCardChromeHeight: 40
    readonly property int tableRowHeight: 22
    readonly property int alertRowHeight: 52
    readonly property int statusRowHeight: 44
    readonly property int iconButtonSize: 36
    readonly property int connectionDotSize: 10

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

    // FPGA 누적 오류 배지용 문구 조립 (StatusStrip). "누적"은 IWarningDevice의 실제 동작을
    // 그대로 옮긴 표현 -- 지금 순간의 상태가 아니라 마지막 CLEAR_ERROR 이후 한 번이라도
    // 있었던 오류를 계속 들고 있는다는 뜻이라, 실시간 플래그와 헷갈리지 않게 이 단어를 그대로 씀.
    function fpgaLatchLabel(checksumLatched, protocolLatched, timeoutLatched) {
        const names = []
        if (checksumLatched) names.push("체크섬")
        if (protocolLatched) names.push("프로토콜")
        if (timeoutLatched) names.push("타임아웃")
        return names.join(" · ")
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
