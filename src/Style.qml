pragma Singleton
import QtQuick

// WriteupPulse's own look: a soft dark slate for long reading (not black, so
// the text doesn't glare), secondary text kept readable (4.5:1 and up on the
// panes), one teal accent, and a colour per topic so a day's mix reads at a
// glance.
QtObject {
    readonly property color bg: "#171b21"
    readonly property color rail: "#13161b"
    readonly property color pane: "#1c2027"
    readonly property color raised: "#242a33"
    readonly property color hover: "#2b323c"
    readonly property color line: "#2e353f"
    readonly property color ink: "#dce1e8"
    readonly property color muted: "#a7b0bd"
    readonly property color faint: "#808a97"
    readonly property color accent: "#6ccab9"
    readonly property color accentInk: "#0f2420"
    readonly property color accentSoft: Qt.rgba(0.42, 0.79, 0.73, 0.16)
    readonly property color done: "#8dd6aa"
    readonly property color danger: "#f19c9c"
    readonly property string sans: "Noto Sans"
    readonly property string serif: "Noto Serif"
    readonly property int radius: 8

    // Concept kinds on the map (the model's, and the catalogs': an ATT&CK
    // technique, a CWE weakness, a CAPEC pattern, an OWASP category), in the
    // topics' family of colours.
    function kindColor(kind) {
        return kind === "technique" ? "#e6b07c"
             : kind === "tool" || kind === "pattern" ? "#a99cf2"
             : kind === "vulnerability" || kind === "weakness" ? "#ec8e9b"
             : kind === "technology" ? "#7fb3e0"
             : kind === "owasp" ? "#e4d27e"
             : "#6ccab9"
    }

    function topicColor(topic) {
        return topic === "Security" ? "#ec8e9b"
             : topic === "AI / ML" ? "#a99cf2"
             : topic === "Engineering" ? "#e6b07c"
             : "#a7b0bd"
    }
}
