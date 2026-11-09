import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1080
    height: 760
    minimumWidth: 720
    minimumHeight: 480
    visible: true
    title: "WriteupPulse"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    readonly property color dim: "#9a9aa2"
    readonly property color panel: "#17171a"
    property bool helpVisible: false
    property int current: 0
    property var expanded: ({})

    Connections {
        target: backend
        function onDigestChanged() { win.current = 0; win.expanded = ({}) }
    }

    function toggle(i) {
        if (i < 0 || i >= backend.items.length) return
        const next = Object.assign({}, expanded)
        next[i] = !next[i]
        expanded = next
    }
    function openArticle(i) {
        if (i >= 0 && i < backend.items.length) Qt.openUrlExternally(backend.items[i].url)
    }
    function move(delta) {
        if (!backend.items.length) return
        current = Math.max(0, Math.min(backend.items.length - 1, current + delta))
        cards.positionViewAtIndex(current, ListView.Contain)
    }

    // Today, Yesterday, or the date in the user's language.
    function dayLabel(text) {
        translator.language
        const parts = text.split("-")
        const day = new Date(parts[0], parts[1] - 1, parts[2])
        const today = new Date(); today.setHours(0, 0, 0, 0)
        const days = Math.round((today - day) / 86400000)
        if (days === 0) return qsTr("Today")
        if (days === 1) return qsTr("Yesterday")
        return day.toLocaleDateString(Qt.locale(), "ddd d MMM")
    }

    function statusText() {
        translator.language
        if (backend.running) return qsTr("Searching and summarizing, this takes a few minutes…")
        if (isNaN(backend.lastRun.getTime())) return qsTr("Never synced")
        const when = backend.lastRun.toLocaleString(Qt.locale(), Locale.ShortFormat)
        return backend.lastOk ? qsTr("Last sync %1").arg(when)
                              : qsTr("Last sync failed %1: %2").arg(when).arg(backend.lastMessage)
    }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Esc"; context: Qt.ApplicationShortcut; enabled: win.helpVisible; onActivated: win.helpVisible = false }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequences: ["Space", "Return"]; context: Qt.ApplicationShortcut; onActivated: win.toggle(win.current) }
    Shortcut { sequence: "R"; context: Qt.ApplicationShortcut; onActivated: backend.refresh() }
    Shortcut { sequence: "O"; context: Qt.ApplicationShortcut; onActivated: win.openArticle(win.current) }
    Shortcut { sequences: ["Down", "J"]; context: Qt.ApplicationShortcut; onActivated: win.move(1) }
    Shortcut { sequences: ["Up", "K"]; context: Qt.ApplicationShortcut; onActivated: win.move(-1) }
    Shortcut { sequences: ["Left", "H"]; context: Qt.ApplicationShortcut; onActivated: backend.olderDay() }
    Shortcut { sequences: ["Right", "L"]; context: Qt.ApplicationShortcut; onActivated: backend.newerDay() }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 16

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Rectangle {
                    implicitWidth: 12; implicitHeight: 12; radius: 6
                    color: theme.accent
                    SequentialAnimation on opacity {
                        running: backend.running
                        loops: Animation.Infinite
                        onRunningChanged: if (!running) parent.opacity = 1
                        NumberAnimation { to: 0.2; duration: 700; easing.type: Easing.InOutQuad }
                        NumberAnimation { to: 1; duration: 700; easing.type: Easing.InOutQuad }
                    }
                }
                Label {
                    text: "WriteupPulse"
                    font.pixelSize: 22
                    font.bold: true
                }
                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    text: win.statusText()
                    color: backend.running || backend.lastOk ? win.dim : "#ff6b6b"
                    elide: Text.ElideRight
                    ToolTip.visible: !backend.lastOk && statusHover.hovered
                    ToolTip.text: backend.lastMessage
                    HoverHandler { id: statusHover }
                }
                Switch {
                    text: qsTr("Daily at 08:00")
                    visible: backend.timerAvailable
                    checked: backend.timerEnabled
                    onToggled: {
                        backend.setTimerEnabled(checked)
                        checked = Qt.binding(() => backend.timerEnabled)
                    }
                }
                Button {
                    text: backend.running ? qsTr("Fetching…") : qsTr("Refresh now")
                    highlighted: true
                    enabled: !backend.running && !backend.agentMissing
                    // Material keeps highlighted text white, which a light
                    // accent like the default yellow washes out.
                    contentItem: Label {
                        text: parent.text
                        font: parent.font
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: parent.enabled ? theme.accentForeground : win.dim
                    }
                    onClicked: backend.refresh()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                visible: problemText.text.length > 0
                implicitHeight: problemText.implicitHeight + 20
                radius: theme.radius
                color: "#2a1414"
                border.color: "#5c2626"
                Label {
                    id: problemText
                    anchors { fill: parent; margins: 10 }
                    wrapMode: Text.Wrap
                    color: "#ffb4b4"
                    text: backend.agentMissing
                          ? qsTr("Claude Code isn't installed, so there is no agent to fetch writeups. Install it and sign in with `claude`.")
                          : backend.problem
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 16

                Rectangle {
                    Layout.preferredWidth: 180
                    Layout.fillHeight: true
                    radius: theme.radius
                    color: win.panel

                    ListView {
                        id: days
                        anchors.fill: parent
                        anchors.margins: 6
                        clip: true
                        spacing: 2
                        model: backend.dates
                        delegate: ItemDelegate {
                            required property string modelData
                            width: ListView.view.width
                            text: win.dayLabel(modelData)
                            highlighted: modelData === backend.date
                            Material.roundedScale: Math.min(4, theme.radius)
                            onClicked: backend.date = modelData
                        }
                    }
                    Label {
                        anchors.centerIn: parent
                        width: parent.width - 24
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        visible: backend.dates.length === 0
                        text: qsTr("No history yet")
                        color: win.dim
                    }
                }

                ListView {
                    id: cards
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 12
                    model: backend.items
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    delegate: WriteupCard {
                        required property var modelData
                        required property int index
                        width: ListView.view.width - 12
                        item: modelData
                        number: index + 1
                        selected: index === win.current
                        expanded: !!win.expanded[index]
                        onClicked: win.current = index
                        onToggled: win.toggle(index)
                        onOpened: win.openArticle(index)
                    }

                    Label {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 40, 420)
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        visible: backend.items.length === 0
                        text: backend.running ? qsTr("The agent is reading today's writeups…")
                                              : qsTr("No writeups yet. Press R to fetch today's.")
                        color: win.dim
                        font.pixelSize: 16
                    }
                }
            }
        }

        Popup {
            id: help
            anchors.centerIn: parent
            modal: true
            visible: win.helpVisible
            onClosed: win.helpVisible = false
            padding: 24
            Material.roundedScale: theme.radius
            Overlay.modal: Rectangle { color: "#a0000000" }

            GridLayout {
                columns: 2
                columnSpacing: 24
                rowSpacing: 10
                Repeater {
                    model: [
                        ["Space", qsTr("Expand or collapse the writeup")],
                        ["↑ ↓", qsTr("Previous or next writeup")],
                        ["← →", qsTr("Older or newer day")],
                        ["O", qsTr("Open the article in the browser")],
                        ["R", qsTr("Fetch today's writeups now")],
                        ["?", qsTr("Show or hide shortcuts")],
                        ["Q", qsTr("Quit")]
                    ]
                    delegate: Repeater {
                        required property var modelData
                        model: modelData
                        Label {
                            required property string modelData
                            required property int index
                            text: modelData
                            color: index === 0 ? theme.accent : "white"
                            font.bold: index === 0
                        }
                    }
                }
            }
        }
    }
}
