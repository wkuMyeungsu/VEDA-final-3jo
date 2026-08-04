pragma Singleton
import QtQuick

// Single source of truth for colors, spacing, typography and animation
// timings across both apps. Nothing here talks to C++; it is pure design
// tokens plus a handful of small mapping helpers (risk level -> color,
// connection state -> label) so QML never repeats those switch statements.
QtObject {
    // --- Surfaces ---
    readonly property color colorBackground: "#0b0f1a"
    readonly property color colorSurface: "#141a29"
    readonly property color colorSurfaceElevated: "#1b2233"
    readonly property color colorBorder: "#2a3245"
    readonly property color colorBorderStrong: "#3a445c"

    // --- Text ---
    readonly property color colorTextPrimary: "#eef1f7"
    readonly property color colorTextSecondary: "#9aa4bb"
    readonly property color colorTextMuted: "#6b7690"

    readonly property color colorAccent: "#4f8cf7"

    // --- Risk levels (color + text + icon, never color alone) ---
    readonly property color colorSafe: "#3cb371"
    readonly property color colorSafeBg: "#15271d"
    readonly property color colorCaution: "#f5a623"
    readonly property color colorCautionBg: "#332810"
    readonly property color colorDanger: "#e0473f"
    readonly property color colorDangerBg: "#331414"
    readonly property color colorEmergency: "#ff3b3b"
    readonly property color colorEmergencyBg: "#3d0a0a"

    readonly property color colorPersonBox: "#4fc3f7"
    readonly property color colorForkliftBox: "#ffb74d"

    // --- Spacing scale ---
    readonly property int spacingXs: 4
    readonly property int spacingSm: 8
    readonly property int spacingMd: 12
    readonly property int spacingLg: 20
    readonly property int spacingXl: 32

    // --- Corner radius ---
    readonly property int radiusSm: 8
    readonly property int radiusMd: 12
    readonly property int radiusLg: 14

    // --- Typography ---
    readonly property int fontSizeSm: 12
    readonly property int fontSizeMd: 14
    readonly property int fontSizeLg: 18
    readonly property int fontSizeXl: 26
    readonly property int fontSizeHuge: 42

    // --- Animation ---
    readonly property int animationFast: 120
    readonly property int animationNormal: 220
    readonly property int animationSlow: 420

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
        case 6: return "UNCONFIRMED_PROXIMITY"
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
}
