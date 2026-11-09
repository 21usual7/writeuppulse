import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1180
    height: 820
    minimumWidth: 760
    minimumHeight: 520
    visible: true
    title: "WriteupPulse"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    readonly property color dim: "#9a9aa2"
    readonly property color panel: "#17171a"
    property bool helpVisible: false
    property bool directionsVisible: false
    property int current: 0
    property var expanded: ({})
    // Single-key shortcuts stay quiet while the user types in a field.
    readonly property bool typing: activeFocusItem !== null && activeFocusItem.cursorPosition !== undefined
    readonly property bool searching: backend.query.trim().length > 0

    Connections {
        target: backend
        function onDigestChanged() { win.current = Math.min(win.current, Math.max(0, backend.items.length - 1)); if (!backend.items.length) win.expanded = ({}) }
        function onDateChanged() { win.current = 0; win.expanded = ({}) }
    }

    function valid(i) { return i >= 0 && i < backend.items.length }
    function toggle(i) {
        if (!valid(i) || backend.deepMode) return
        const next = Object.assign({}, expanded)
        next[backend.items[i].url] = !next[backend.items[i].url]
        expanded = next
    }
    function openArticle(i) { if (valid(i)) Qt.openUrlExternally(backend.items[i].url) }
    function progressOf(i) {
        backend.progressRevision
        return valid(i) ? backend.state(backend.items[i].url) : ({})
    }
    function toggleRead(i) { if (valid(i)) backend.setRead(backend.items[i], progressOf(i).read === undefined) }
    function toggleLab(i) { if (valid(i)) backend.setLab(backend.items[i], progressOf(i).lab === undefined) }
    function toggleStar(i) { if (valid(i)) backend.setFavorite(backend.items[i], progressOf(i).favorite !== true) }
    function move(delta) {
        if (!backend.items.length) return
        current = Math.max(0, Math.min(backend.items.length - 1, current + delta))
        cards.positionViewAtIndex(current, ListView.Contain)
    }
    function toggleCategory(category) {
        const list = backend.categoryFilter.slice()
        const at = list.indexOf(category)
        if (at >= 0) list.splice(at, 1); else list.push(category)
        backend.categoryFilter = list
    }
    function toggleSearched(category) {
        const list = backend.searchCategories.length ? backend.searchCategories.slice() : backend.categoriesOf("")
        const at = list.indexOf(category)
        if (at >= 0) list.splice(at, 1); else list.push(category)
        backend.searchCategories = list
    }
    function togglePreferred(category) {
        const list = backend.preferredCategories.slice()
        const at = list.indexOf(category)
        if (at >= 0) list.splice(at, 1); else list.push(category)
        backend.preferredCategories = list
    }
    function clearFilters() {
        backend.categoryFilter = []
        backend.levelFilter = ""
        backend.hideRead = false
        backend.tagFilter = ""
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

    function heading() {
        translator.language
        if (win.searching) return qsTr("%n results", "", backend.items.length)
        if (backend.view === "favorites") return qsTr("Favorites")
        return backend.date ? win.dayLabel(backend.date) : ""
    }

    function emptyText() {
        if (backend.running && !backend.dates.length) return qsTr("The agent is reading today's writeups…")
        if (!backend.dates.length) return qsTr("No writeups yet. Press R to fetch today's.")
        if (backend.hiddenCount > 0) return qsTr("Every writeup here is hidden by the filters.")
        if (win.searching) return qsTr("Nothing found.")
        if (backend.view === "favorites") return qsTr("No favorites yet. Press F on a writeup to keep it here.")
        return ""
    }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut {
        sequence: "Esc"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (win.helpVisible) win.helpVisible = false
            else if (win.directionsVisible) win.directionsVisible = false
            else if (win.typing) { search.text = ""; cards.forceActiveFocus() }
        }
    }
    Shortcut { sequences: [StandardKey.Find]; context: Qt.ApplicationShortcut; onActivated: search.forceActiveFocus() }
    Shortcut { sequence: "/"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: search.forceActiveFocus() }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: Qt.quit() }
    Shortcut { sequences: ["Space", "Return"]; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.toggle(win.current) }
    Shortcut { sequence: "D"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: backend.deepMode = !backend.deepMode }
    Shortcut { sequence: "R"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: backend.refresh() }
    Shortcut { sequence: "O"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.openArticle(win.current) }
    Shortcut { sequence: "M"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.toggleRead(win.current) }
    Shortcut { sequence: "B"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.toggleLab(win.current) }
    Shortcut { sequence: "F"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.toggleStar(win.current) }
    Shortcut { sequence: "H"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: backend.hideRead = !backend.hideRead }
    Shortcut { sequence: "E"; context: Qt.ApplicationShortcut; enabled: !win.typing && backend.dates.length > 0; onActivated: exportMenu.popup(exportButton, 0, exportButton.height) }
    Shortcut { sequence: "P"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.directionsVisible = !win.directionsVisible }
    Shortcut { sequence: "S"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: backend.view = backend.view === "stats" ? "day" : "stats" }
    Shortcut { sequences: ["Down", "J"]; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.move(1) }
    Shortcut { sequences: ["Up", "K"]; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.move(-1) }
    Shortcut { sequence: "Left"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: backend.olderDay() }
    Shortcut { sequence: "Right"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: backend.newerDay() }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12

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
                ColumnLayout {
                    spacing: 0
                    Layout.fillWidth: true
                    RowLayout {
                        spacing: 12
                        Label { text: "WriteupPulse"; font.pixelSize: 22; font.bold: true }
                        Label {
                            visible: backend.streak > 0
                            text: qsTr("%n-day streak", "", backend.streak)
                            color: theme.accent
                            font.bold: true
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: win.statusText()
                        color: backend.running || backend.lastOk ? win.dim : "#ff6b6b"
                        elide: Text.ElideRight
                        font.pixelSize: 13
                        ToolTip.visible: statusHover.hovered && text.length > 40
                        ToolTip.text: text
                        HoverHandler { id: statusHover }
                    }
                }
                TextField {
                    id: search
                    Layout.preferredWidth: 240
                    Layout.minimumWidth: 120
                    Layout.fillWidth: true
                    Layout.maximumWidth: 280
                    placeholderText: qsTr("Search all writeups")
                    Material.containerStyle: Material.Outlined
                    text: backend.query
                    onTextEdited: backend.query = text
                }
                ComboBox {
                    readonly property var codes: ["en", "ru", "uk"]
                    // Each language in its own name, so it can be found from any of them.
                    model: ["English", "Русский", "Українська"]
                    currentIndex: Math.max(0, codes.indexOf(backend.language))
                    implicitWidth: 130
                    Material.roundedScale: theme.radius
                    onActivated: index => backend.language = codes[index]
                    ToolTip.visible: hovered && !popup.visible
                    ToolTip.delay: 600
                    ToolTip.text: qsTr("Language of the window and of new summaries, from the next fetch on")
                }
                Button {
                    text: backend.running ? qsTr("Fetching…") : qsTr("Refresh now")
                    highlighted: true
                    enabled: !backend.running && !backend.agentMissing
                    onClicked: backend.refresh()
                    // Material keeps highlighted text white, which a light
                    // accent like the default yellow washes out.
                    contentItem: Label {
                        text: parent.text
                        font: parent.font
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: parent.enabled ? theme.accentForeground : win.dim
                    }
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

                // Favorites, statistics, and the days.
                Rectangle {
                    Layout.preferredWidth: 190
                    Layout.fillHeight: true
                    radius: theme.radius
                    color: win.panel

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 2

                        ItemDelegate {
                            Layout.fillWidth: true
                            text: "★  " + qsTr("Favorites") + (backend.favoriteCount ? "  " + backend.favoriteCount.toLocaleString(Qt.locale(), 'f', 0) : "")
                            highlighted: backend.view === "favorites" && !win.searching
                            Material.roundedScale: Math.min(4, theme.radius)
                            onClicked: { search.text = ""; backend.view = "favorites" }
                        }
                        ItemDelegate {
                            Layout.fillWidth: true
                            text: "⚙  " + qsTr("Directions")
                            Material.roundedScale: Math.min(4, theme.radius)
                            onClicked: win.directionsVisible = true
                        }
                        ItemDelegate {
                            Layout.fillWidth: true
                            text: "∑  " + qsTr("Statistics")
                            highlighted: backend.view === "stats" && !win.searching
                            Material.roundedScale: Math.min(4, theme.radius)
                            onClicked: { search.text = ""; backend.view = "stats" }
                        }
                        Label {
                            Layout.topMargin: 10
                            Layout.leftMargin: 10
                            Layout.bottomMargin: 2
                            text: qsTr("Days")
                            color: win.dim
                            font.pixelSize: 12
                        }
                        ListView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 2
                            model: backend.dates
                            delegate: ItemDelegate {
                                required property string modelData
                                width: ListView.view.width
                                text: win.dayLabel(modelData)
                                highlighted: modelData === backend.date && backend.view === "day" && !win.searching
                                Material.roundedScale: Math.min(4, theme.radius)
                                onClicked: { search.text = ""; backend.date = modelData }
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
                        Switch {
                            Layout.fillWidth: true
                            visible: backend.timerAvailable
                            text: qsTr("Daily at 08:00")
                            font.pixelSize: 13
                            checked: backend.timerEnabled
                            onToggled: {
                                backend.setTimerEnabled(checked)
                                checked = Qt.binding(() => backend.timerEnabled)
                            }
                        }
                    }
                }

                StatsView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: backend.view === "stats" && !win.searching
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: backend.view !== "stats" || win.searching
                    spacing: 10

                    // Heading, view mode, and filters; the controls wrap under the
                    // heading when the window is narrow.
                    Flow {
                        Layout.fillWidth: true
                        spacing: 12
                        Label {
                            height: 40
                            verticalAlignment: Text.AlignVCenter
                            text: win.heading()
                            font.pixelSize: 20
                            font.bold: true
                        }
                        Row {
                            spacing: 4
                            topPadding: 6
                            FilterChip { text: qsTr("Quick"); on: !backend.deepMode; onClicked: backend.deepMode = false }
                            FilterChip { text: qsTr("Deep"); on: backend.deepMode; onClicked: backend.deepMode = true }
                        }
                        ComboBox {
                            width: 150
                            height: 40
                            model: [qsTr("All levels")].concat(backend.levels)
                            currentIndex: backend.levelFilter ? backend.levels.indexOf(backend.levelFilter) + 1 : 0
                            onActivated: index => backend.levelFilter = index > 0 ? backend.levels[index - 1] : ""
                            Material.roundedScale: theme.radius
                        }
                        Switch {
                            height: 40
                            text: qsTr("Hide read")
                            checked: backend.hideRead
                            onToggled: { backend.hideRead = checked; checked = Qt.binding(() => backend.hideRead) }
                        }
                        Button {
                            id: exportButton
                            height: 40
                            flat: true
                            visible: backend.dates.length > 0
                            text: qsTr("Export") + " ▾"
                            Material.roundedScale: theme.radius
                            onClicked: exportMenu.popup(exportButton, 0, exportButton.height)
                            Menu {
                                id: exportMenu
                                Material.roundedScale: theme.radius
                                width: 280
                                MenuItem { text: qsTr("This day as Markdown"); onTriggered: backend.exportMarkdown(false) }
                                MenuItem { text: qsTr("This week as Markdown"); onTriggered: backend.exportMarkdown(true) }
                            }
                        }
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        visible: backend.shownCategories.length > 1 || backend.categoryFilter.length > 0
                        FilterChip {
                            text: qsTr("All directions")
                            on: backend.categoryFilter.length === 0
                            onClicked: backend.categoryFilter = []
                        }
                        Repeater {
                            model: backend.shownCategories
                            FilterChip {
                                required property string modelData
                                text: modelData
                                on: backend.categoryFilter.indexOf(modelData) >= 0
                                onClicked: win.toggleCategory(modelData)
                            }
                        }
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        visible: backend.view === "favorites" && !win.searching && backend.tags.length > 0
                        FilterChip { text: qsTr("All tags"); on: !backend.tagFilter; onClicked: backend.tagFilter = "" }
                        Repeater {
                            model: backend.tags
                            FilterChip {
                                required property string modelData
                                text: "#" + modelData
                                on: backend.tagFilter === modelData
                                onClicked: backend.tagFilter = modelData
                            }
                        }
                    }

                    RowLayout {
                        visible: backend.hiddenCount > 0
                        spacing: 8
                        Label {
                            text: qsTr("%n hidden by filters", "", backend.hiddenCount)
                            color: win.dim
                            font.pixelSize: 13
                        }
                        Button {
                            flat: true
                            text: qsTr("Show all")
                            font.pixelSize: 13
                            Material.foreground: theme.accent
                            onClicked: win.clearFilters()
                        }
                    }

                    ListView {
                        id: cards
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 14
                        model: backend.items
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        // Cards keep a readable width and line up with the
                        // heading and filters above them.
                        delegate: Item {
                            id: row
                            required property var modelData
                            required property int index
                            width: ListView.view.width - 12
                            height: card.implicitHeight
                            WriteupCard {
                                id: card
                                anchors.left: parent.left
                                width: Math.min(parent.width, 940)
                                height: implicitHeight
                                item: row.modelData
                                number: row.index + 1
                                selected: row.index === win.current
                                expanded: backend.deepMode || !!win.expanded[row.modelData.url]
                                showDate: win.searching || backend.view === "favorites"
                                dayText: win.dayLabel(row.modelData.date)
                                progress: win.progressOf(row.index)
                                onClicked: win.current = row.index
                                onToggled: win.toggle(row.index)
                                onOpened: win.openArticle(row.index)
                                onReadToggled: win.toggleRead(row.index)
                                onLabToggled: win.toggleLab(row.index)
                                onStarToggled: win.toggleStar(row.index)
                                onPracticeToggled: (step, done) => backend.setPracticeDone(row.modelData.url, step, done)
                                onTagsEdited: tags => backend.setTags(row.modelData.url, tags)
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            width: Math.min(parent.width - 40, 420)
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            visible: backend.items.length === 0
                            text: win.emptyText()
                            color: win.dim
                            font.pixelSize: 16
                        }
                    }
                }
            }
        }

        // A short confirmation after an export, at the bottom of the window.
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 28
            visible: backend.notice.length > 0
            implicitWidth: noticeLabel.implicitWidth + 32
            implicitHeight: 40
            radius: theme.radius
            color: "#26262c"
            border.color: theme.accent
            Label {
                id: noticeLabel
                anchors.centerIn: parent
                text: backend.notice
            }
        }

        // Which directions the agent searches, and which it picks more often.
        Popup {
            anchors.centerIn: parent
            width: Math.min(parent.width - 80, 720)
            height: Math.min(parent.height - 80, directionsColumn.implicitHeight + 48)
            modal: true
            visible: win.directionsVisible
            onClosed: win.directionsVisible = false
            padding: 24
            Material.roundedScale: theme.radius
            Overlay.modal: Rectangle { color: "#a0000000" }

            Flickable {
                anchors.fill: parent
                contentHeight: directionsColumn.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                ColumnLayout {
                    id: directionsColumn
                    width: parent.width
                    spacing: 12

                    Label {
                        text: qsTr("Directions")
                        font.pixelSize: 20
                        font.bold: true
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: win.dim
                        text: qsTr("The agent searches the highlighted directions, from the next fetch on. Star the ones it should pick more often; they also come first in each day.")
                    }
                    Repeater {
                        model: backend.topics
                        ColumnLayout {
                            id: topic
                            required property string modelData
                            Layout.fillWidth: true
                            spacing: 6
                            Label {
                                Layout.topMargin: 6
                                text: topic.modelData === "Other" ? qsTr("Other") : topic.modelData
                                color: theme.accent
                                font.bold: true
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 8
                                Repeater {
                                    model: backend.categoriesOf(topic.modelData)
                                    Row {
                                        required property string modelData
                                        spacing: 2
                                        FilterChip {
                                            text: modelData
                                            on: backend.searchCategories.length === 0 || backend.searchCategories.indexOf(modelData) >= 0
                                            onClicked: win.toggleSearched(modelData)
                                        }
                                        Label {
                                            width: 26
                                            height: 28
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                            text: backend.preferredCategories.indexOf(modelData) >= 0 ? "★" : "☆"
                                            color: backend.preferredCategories.indexOf(modelData) >= 0 ? theme.accent : win.dim
                                            font.pixelSize: 16
                                            TapHandler { onTapped: win.togglePreferred(modelData) }
                                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    RowLayout {
                        Layout.topMargin: 8
                        Button {
                            flat: true
                            text: qsTr("Search everything")
                            Material.foreground: theme.accent
                            onClicked: { backend.searchCategories = []; backend.preferredCategories = [] }
                        }
                        Item { Layout.fillWidth: true }
                        Button {
                            flat: true
                            text: qsTr("Done")
                            onClicked: win.directionsVisible = false
                        }
                    }
                }
            }
        }

        Popup {
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
                        ["D", qsTr("Switch between quick and deep view")],
                        ["↑ ↓", qsTr("Previous or next writeup")],
                        ["← →", qsTr("Older or newer day")],
                        ["O", qsTr("Open the article in the browser")],
                        ["M", qsTr("Mark as read")],
                        ["B", qsTr("Mark as done in a lab")],
                        ["F", qsTr("Add to or remove from favorites")],
                        ["H", qsTr("Hide or show read writeups")],
                        ["Ctrl+F  /", qsTr("Search all writeups")],
                        ["S", qsTr("Statistics")],
                        ["P", qsTr("Directions the agent searches")],
                        ["E", qsTr("Export the day or week as Markdown")],
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
