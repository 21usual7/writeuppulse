pragma Singleton
import QtQuick

// WriteupPulse's own look: calm slate for long reading, one teal accent, and
// a colour per topic so a day's mix reads at a glance.
QtObject {
    readonly property color bg: "#0f1216"
    readonly property color rail: "#0b0d10"
    readonly property color pane: "#14181d"
    readonly property color raised: "#1a1f26"
    readonly property color hover: "#1f252d"
    readonly property color line: "#232a33"
    readonly property color ink: "#e4e8ee"
    readonly property color muted: "#8a94a3"
    readonly property color faint: "#5b6573"
    readonly property color accent: "#4fc1b0"
    readonly property color accentInk: "#0b0d10"
    readonly property color accentSoft: Qt.rgba(0.31, 0.76, 0.69, 0.15)
    readonly property color done: "#7fd1a0"
    readonly property color danger: "#f08a8a"
    readonly property string sans: "Noto Sans"
    readonly property string serif: "Noto Serif"
    readonly property int radius: 8

    // Concept kinds on the map, in the topics' family of colours.
    function kindColor(kind) {
        return kind === "technique" ? "#e2a46b"
             : kind === "tool" ? "#9d8cf2"
             : kind === "vulnerability" ? "#e57b8c"
             : kind === "technology" ? "#6fa8dc"
             : "#4fc1b0"
    }

    function topicColor(topic) {
        return topic === "Security" ? "#e57b8c"
             : topic === "AI / ML" ? "#9d8cf2"
             : topic === "Engineering" ? "#e2a46b"
             : "#8a94a3"
    }
}
