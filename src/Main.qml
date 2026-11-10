import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: win
    width: 1280
    height: 860
    minimumWidth: 900
    minimumHeight: 560
    visible: true
    title: "WriteupPulse"
    color: Style.bg

    // Material only draws the few stock controls (fields, menus, popups),
    // in the app's own colours.
    Material.theme: Material.Dark
    Material.accent: Style.accent
    Material.background: Style.raised
    Material.foreground: Style.ink
    Material.roundedScale: Material.SmallScale

    property bool helpVisible: false
    property bool settingsVisible: false
    property bool focusVisible: false
    property int current: 0
    // A node the Concepts view opens on, then clears.
    property string graphFocus: ""
    // Single-key shortcuts stay quiet while the user types in a field.
    // (and stay out of the way while Get started is open).
    readonly property bool typing: (activeFocusItem !== null && activeFocusItem.cursorPosition !== undefined) || startVisible
    property bool startAgain: false
    readonly property bool startVisible: !backend.onboarded || startAgain
    readonly property bool searching: backend.query.trim().length > 0
    readonly property var item: valid(current) ? backend.items[current] : null

    Connections {
        target: backend
        function onDigestChanged() { win.current = Math.min(win.current, Math.max(0, backend.items.length - 1)) }
        function onDateChanged() { win.current = 0 }
        function onFilterChanged() { win.current = 0 }
    }

    function valid(i) { return i >= 0 && i < backend.items.length }
    function progressOf(i) {
        backend.progressRevision
        return valid(i) ? backend.state(backend.items[i].url) : ({})
    }
    function openArticle(i) { if (valid(i)) Qt.openUrlExternally(backend.items[i].url) }
    function toggleRead(i) { if (valid(i)) backend.setRead(backend.items[i], progressOf(i).read === undefined) }
    function toggleLab(i) { if (valid(i)) backend.setLab(backend.items[i], progressOf(i).lab === undefined) }
    function toggleStar(i) { if (valid(i)) backend.setFavorite(backend.items[i], progressOf(i).favorite !== true) }
    function move(delta) {
        if (!backend.items.length) return
        current = Math.max(0, Math.min(backend.items.length - 1, current + delta))
        list.positionViewAtIndex(current, ListView.Contain)
    }
    // Topics are translated; the directions inside them stay as the field
    // calls them (Web, Pwn, OSINT…).
    function topicName(topic) {
        translator.language
        return topic === "Security" ? qsTr("Security") : topic === "AI / ML" ? qsTr("AI / ML")
             : topic === "Engineering" ? qsTr("Engineering") : qsTr("Other")
    }
    function show(view) {
        search.text = ""
        backend.view = view
        focusVisible = false
    }
    // From the concept map to the writeup in its day, out from under any
    // filter that would hide it.
    function openWriteup(item) {
        show("day")
        backend.date = item.date
        let at = backend.items.findIndex(it => it.url === item.url)
        if (at < 0) {
            backend.categoryFilter = []
            backend.levelFilter = ""
            backend.hideRead = false
            at = backend.items.findIndex(it => it.url === item.url)
        }
        current = Math.max(0, at)
        list.positionViewAtIndex(current, ListView.Contain)
    }
    // Topic pills narrow the list to that topic's directions.
    function topicOn(topic) {
        const own = backend.categoriesOf(topic)
        return backend.categoryFilter.length > 0 && backend.categoryFilter.every(c => own.indexOf(c) >= 0)
    }
    function toggleTopic(topic) { backend.categoryFilter = topicOn(topic) ? [] : backend.categoriesOf(topic) }
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
    // Marks the writeup read and moves on, closing focus after the last one.
    function finishInFocus() {
        if (progressOf(current).read === undefined) toggleRead(current)
        if (current < backend.items.length - 1) { current += 1; list.positionViewAtIndex(current, ListView.Contain) }
        else focusVisible = false
    }

    // Today, Yesterday, or the date in the user's language.
    function dayLabel(text) {
        translator.language
        if (!text) return ""
        const parts = text.split("-")
        const day = new Date(parts[0], parts[1] - 1, parts[2])
        const today = new Date(); today.setHours(0, 0, 0, 0)
        const days = Math.round((today - day) / 86400000)
        if (days === 0) return qsTr("Today")
        if (days === 1) return qsTr("Yesterday")
        if (days === -1) return qsTr("Tomorrow")
        return day.toLocaleDateString(Qt.locale(), "ddd d MMM")
    }
    function longDay(text) {
        translator.language
        if (!text) return ""
        const parts = text.split("-")
        return new Date(parts[0], parts[1] - 1, parts[2]).toLocaleDateString(Qt.locale(), "dddd, d MMMM")
    }

    // Only running the agent here needs Claude Code; reading the site doesn't.
    readonly property bool agentNeeded: backend.agentMissing && backend.source === "claude"

    function statusText() {
        translator.language
        if (backend.running) return backend.source === "feed" ? qsTr("Downloading new writeups…")
                                                              : qsTr("Searching and summarizing, this takes a few minutes…")
        if (isNaN(backend.lastRun.getTime())) return qsTr("Never synced")
        const when = backend.lastRun.toLocaleString(Qt.locale(), Locale.ShortFormat)
        return backend.lastOk ? qsTr("Last sync %1").arg(when)
                              : qsTr("Last sync failed %1: %2").arg(when).arg(backend.lastMessage)
    }

    function heading() {
        translator.language
        if (win.searching) return qsTr("%n results", "", backend.items.length)
        if (backend.view === "favorites") return qsTr("Favorites")
        return backend.date ? win.dayLabel(backend.date) : "WriteupPulse"
    }

    function subheading() {
        translator.language
        backend.progressRevision
        if (win.searching || backend.view !== "day") return ""
        let read = 0
        for (const it of backend.items) if (backend.state(it.url).read !== undefined) ++read
        return [win.longDay(backend.date), qsTr("%1 of %2 read").arg(read.toLocaleString(Qt.locale(), 'f', 0))
                .arg(backend.items.length.toLocaleString(Qt.locale(), 'f', 0))].filter(s => s).join("  ·  ")
    }

    function emptyText() {
        if (backend.running && !backend.dates.length) return backend.source === "feed" ? qsTr("Downloading writeups from the site…")
                                                                                      : qsTr("The agent is reading today's writeups…")
        if (!backend.dates.length) return backend.source === "feed" ? qsTr("No writeups yet. Press R to download them.")
                                                                    : qsTr("No writeups yet. Press R to fetch today's.")
        if (backend.hiddenCount > 0) return qsTr("Every writeup here is hidden by the filters.")
        if (win.searching) return qsTr("Nothing found.")
        if (backend.view === "favorites") return qsTr("No favorites yet. Press F on a writeup to keep it here.")
        return ""
    }

    Shortcut {
        sequence: "Esc"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (win.helpVisible) win.helpVisible = false
            else if (win.settingsVisible) win.settingsVisible = false
            else if (win.focusVisible) win.focusVisible = false
            else if (win.typing) { search.text = ""; list.forceActiveFocus() }
        }
    }
    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequences: [StandardKey.Find]; context: Qt.ApplicationShortcut; onActivated: { win.focusVisible = false; search.forceActiveFocus() } }
    Shortcut { sequence: "/"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: { win.focusVisible = false; search.forceActiveFocus() } }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: Qt.quit() }
    Shortcut { sequence: "R"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: backend.refresh() }
    Shortcut { sequence: "O"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.openArticle(win.current) }
    Shortcut { sequence: "M"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.toggleRead(win.current) }
    Shortcut { sequence: "B"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.toggleLab(win.current) }
    Shortcut { sequence: "F"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.toggleStar(win.current) }
    Shortcut { sequence: "U"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: backend.hideRead = !backend.hideRead }
    Shortcut { sequence: "S"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.show(backend.view === "stats" ? "day" : "stats") }
    Shortcut { sequence: "V"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.show(backend.view === "review" ? "day" : "review") }
    Shortcut { sequence: "G"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.show(backend.view === "graph" ? "day" : "graph") }
    Shortcut { sequence: "P"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.settingsVisible = !win.settingsVisible }
    Shortcut { sequence: "E"; context: Qt.ApplicationShortcut; enabled: !win.typing && backend.dates.length > 0; onActivated: moreMenu.popup(moreButton, 0, moreButton.height) }
    Shortcut { sequence: "Return"; context: Qt.ApplicationShortcut; enabled: !win.typing && !win.focusVisible && win.item !== null; onActivated: win.focusVisible = true }
    Shortcut { sequence: "1"; context: Qt.ApplicationShortcut; enabled: !win.typing && !win.focusVisible; onActivated: reader.tab = 0 }
    Shortcut { sequence: "2"; context: Qt.ApplicationShortcut; enabled: !win.typing && !win.focusVisible; onActivated: reader.tab = 1 }
    Shortcut { sequence: "3"; context: Qt.ApplicationShortcut; enabled: !win.typing && !win.focusVisible; onActivated: reader.tab = 2 }
    Shortcut { sequence: "4"; context: Qt.ApplicationShortcut; enabled: !win.typing && !win.focusVisible; onActivated: reader.tab = 3 }
    Shortcut { sequence: "5"; context: Qt.ApplicationShortcut; enabled: !win.typing && !win.focusVisible; onActivated: reader.tab = 4 }
    Shortcut { sequence: "6"; context: Qt.ApplicationShortcut; enabled: !win.typing && !win.focusVisible; onActivated: reader.tab = 5 }
    Shortcut { sequences: ["Down", "J"]; context: Qt.ApplicationShortcut; enabled: !win.typing && !win.focusVisible; onActivated: win.move(1) }
    Shortcut { sequences: ["Up", "K"]; context: Qt.ApplicationShortcut; enabled: !win.typing && !win.focusVisible; onActivated: win.move(-1) }
    Shortcut { sequence: "Left"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.focusVisible ? focusView.previousStep() : backend.olderDay() }
    Shortcut { sequence: "Right"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.focusVisible ? focusView.nextStep() : backend.newerDay() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: !win.typing && win.focusVisible; onActivated: focusView.reveal() }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        RowLayout {
            anchors.fill: parent
            spacing: 0

            // Icon rail
            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: 64
                color: Style.rail

                Column {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 22
                    spacing: 14

                    Rectangle {
                        width: 32; height: 32; radius: 9
                        color: Style.accent
                        Text { anchors.centerIn: parent; text: "W"; color: Style.accentInk; font.family: Style.sans; font.pixelSize: 16; font.bold: true }
                        SequentialAnimation on opacity {
                            running: backend.running
                            loops: Animation.Infinite
                            onRunningChanged: if (!running) parent.opacity = 1
                            NumberAnimation { to: 0.35; duration: 700; easing.type: Easing.InOutQuad }
                            NumberAnimation { to: 1; duration: 700; easing.type: Easing.InOutQuad }
                        }
                    }
                    Item { width: 1; height: 8 }
                    Repeater {
                        model: [
                            ["☰", "day", qsTr("Days")],
                            ["★", "favorites", qsTr("Favorites")],
                            ["↻", "review", qsTr("Review")],
                            ["∑", "stats", qsTr("Statistics")],
                            ["◈", "graph", qsTr("Concepts")]
                        ]
                        Rectangle {
                            required property var modelData
                            readonly property bool on: backend.view === modelData[1] && !win.searching
                            width: 40; height: 40; radius: 10
                            color: on ? Style.raised : railHover.hovered ? Style.pane : "transparent"
                            Text { anchors.centerIn: parent; text: modelData[0]; color: parent.on ? Style.ink : Style.faint; font.pixelSize: 18 }
                            // How many questions wait for today's review.
                            Rectangle {
                                readonly property int count: modelData[1] === "review" ? (backend.review.cards || []).length : 0
                                visible: count > 0
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: -2
                                width: Math.max(16, badge.implicitWidth + 8); height: 16; radius: 8
                                color: Style.accent
                                Text { id: badge; anchors.centerIn: parent; text: parent.count; color: Style.accentInk; font.family: Style.sans; font.pixelSize: 10; font.bold: true }
                            }
                            HoverHandler { id: railHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: win.show(modelData[1]) }
                            ToolTip.visible: railHover.hovered
                            ToolTip.text: modelData[2]
                        }
                    }
                }
                Column {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 18
                    spacing: 10
                    Repeater {
                        model: [["⚙", qsTr("Settings and directions")], ["?", qsTr("Help and shortcuts")]]
                        Rectangle {
                            required property var modelData
                            required property int index
                            width: 40; height: 40; radius: 10
                            color: bottomHover.hovered ? Style.pane : "transparent"
                            // A symbol font keeps the gear monochrome instead of an emoji.
                            Text { anchors.centerIn: parent; text: modelData[0]; color: Style.faint; font.family: "Noto Sans Symbols"; font.pixelSize: 17 }
                            HoverHandler { id: bottomHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: index === 0 ? win.settingsVisible = true : win.helpVisible = true }
                            ToolTip.visible: bottomHover.hovered
                            ToolTip.text: modelData[1]
                        }
                    }
                }
            }

            // The list
            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: Math.min(400, win.width * 0.34)
                color: Style.pane
                visible: ["stats", "graph", "review"].indexOf(backend.view) < 0 || win.searching
                Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Style.line }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.margins: 22
                        Layout.bottomMargin: 14
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text {
                                id: dayTitle
                                Layout.fillWidth: true
                                text: win.heading() + (backend.view === "day" && !win.searching && backend.dates.length > 1 ? "  ▾" : "")
                                elide: Text.ElideRight
                                color: Style.ink
                                font.family: Style.sans
                                font.pixelSize: 22
                                font.weight: Font.DemiBold
                                HoverHandler { cursorShape: backend.dates.length > 1 ? Qt.PointingHandCursor : Qt.ArrowCursor }
                                TapHandler { onTapped: if (backend.dates.length > 1) dayMenu.popup(dayTitle, 0, dayTitle.height) }
                                Menu {
                                    id: dayMenu
                                    width: 220
                                    Repeater {
                                        model: backend.dates
                                        MenuItem {
                                            required property string modelData
                                            text: win.dayLabel(modelData)
                                            onTriggered: { search.text = ""; backend.date = modelData }
                                        }
                                    }
                                }
                            }
                            Repeater {
                                model: [["‹", qsTr("Older day")], ["›", qsTr("Newer day")]]
                                Rectangle {
                                    required property var modelData
                                    required property int index
                                    visible: backend.view === "day" && !win.searching
                                    width: 28; height: 28; radius: 8
                                    color: navHover.hovered ? Style.hover : "transparent"
                                    Text { anchors.centerIn: parent; text: modelData[0]; color: Style.muted; font.pixelSize: 20 }
                                    HoverHandler { id: navHover; cursorShape: Qt.PointingHandCursor }
                                    TapHandler { onTapped: index === 0 ? backend.olderDay() : backend.newerDay() }
                                    ToolTip.visible: navHover.hovered
                                    ToolTip.text: modelData[1]
                                }
                            }
                            Rectangle {
                                id: moreButton
                                width: 28; height: 28; radius: 8
                                color: moreHover.hovered ? Style.hover : "transparent"
                                Text { anchors.centerIn: parent; text: "⋯"; color: Style.muted; font.pixelSize: 18 }
                                HoverHandler { id: moreHover; cursorShape: Qt.PointingHandCursor }
                                TapHandler { onTapped: moreMenu.popup(moreButton, 0, moreButton.height) }
                                Menu {
                                    id: moreMenu
                                    width: 260
                                    MenuItem { text: qsTr("Refresh now"); enabled: !backend.running && !win.agentNeeded; onTriggered: backend.refresh() }
                                    MenuSeparator {}
                                    MenuItem { text: qsTr("This day as Markdown"); enabled: backend.dates.length > 0; onTriggered: backend.exportMarkdown(false) }
                                    MenuItem { text: qsTr("This week as Markdown"); enabled: backend.dates.length > 0; onTriggered: backend.exportMarkdown(true) }
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: win.subheading() + (backend.streak > 0 ? "  ·  🔥 " + qsTr("%n-day streak", "", backend.streak) : "")
                            elide: Text.ElideRight
                            color: Style.muted
                            font.family: Style.sans
                            font.pixelSize: 12
                        }

                        // Sync status, with refresh right there.
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Text {
                                Layout.fillWidth: true
                                text: win.statusText()
                                elide: Text.ElideRight
                                color: backend.running || backend.lastOk ? Style.faint : Style.danger
                                font.family: Style.sans
                                font.pixelSize: 11
                                HoverHandler { id: statusHover }
                                ToolTip.visible: statusHover.hovered && text.length > 50
                                ToolTip.text: text
                            }
                            Text {
                                visible: !backend.running && !win.agentNeeded
                                text: "↻ " + qsTr("Refresh")
                                color: Style.accent
                                font.family: Style.sans
                                font.pixelSize: 11
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                                TapHandler { onTapped: backend.refresh() }
                            }
                        }

                        TextField {
                            id: search
                            Layout.fillWidth: true
                            Layout.topMargin: 10
                            implicitHeight: 38
                            topPadding: 0
                            bottomPadding: 0
                            leftPadding: 14
                            placeholderText: qsTr("Search all writeups") + "   Ctrl F"
                            placeholderTextColor: Style.faint
                            color: Style.ink
                            font.family: Style.sans
                            font.pixelSize: 13
                            background: Rectangle { radius: Style.radius; color: Style.raised; border.color: search.activeFocus ? Style.accent : "transparent" }
                            text: backend.query
                            onTextEdited: backend.query = text
                        }

                        Flow {
                            Layout.fillWidth: true
                            Layout.topMargin: 6
                            spacing: 6
                            Pill { text: qsTr("All"); on: backend.categoryFilter.length === 0 && !backend.hideRead && !backend.levelFilter; onClicked: { backend.categoryFilter = []; backend.hideRead = false; backend.levelFilter = "" } }
                            Pill { text: qsTr("Unread"); on: backend.hideRead; onClicked: backend.hideRead = !backend.hideRead }
                            Repeater {
                                model: backend.topics.filter(t => t !== "Other" && backend.shownCategories.some(c => backend.topicOf(c) === t))
                                Pill {
                                    required property string modelData
                                    text: win.topicName(modelData)
                                    tint: Style.topicColor(modelData)
                                    on: win.topicOn(modelData)
                                    onClicked: win.toggleTopic(modelData)
                                }
                            }
                            Pill {
                                id: levelPill
                                text: (backend.levelFilter || qsTr("Level")) + " ▾"
                                on: backend.levelFilter.length > 0
                                onClicked: levelMenu.popup(levelPill, 0, levelPill.height)
                                Menu {
                                    id: levelMenu
                                    MenuItem { text: qsTr("All levels"); onTriggered: backend.levelFilter = "" }
                                    Repeater {
                                        model: backend.levels
                                        MenuItem { required property string modelData; text: modelData; onTriggered: backend.levelFilter = modelData }
                                    }
                                }
                            }
                        }

                        // Favorites can be narrowed by the user's tags.
                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            visible: backend.view === "favorites" && !win.searching && backend.tags.length > 0
                            Repeater {
                                model: backend.tags
                                Pill {
                                    required property string modelData
                                    text: "#" + modelData
                                    on: backend.tagFilter === modelData
                                    onClicked: backend.tagFilter = backend.tagFilter === modelData ? "" : modelData
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.leftMargin: 22
                        Layout.rightMargin: 22
                        visible: problemText.text.length > 0
                        implicitHeight: problemText.implicitHeight + 20
                        radius: Style.radius
                        color: Qt.rgba(0.94, 0.54, 0.54, 0.12)
                        Text {
                            id: problemText
                            anchors { fill: parent; margins: 10 }
                            wrapMode: Text.Wrap
                            color: Style.danger
                            font.family: Style.sans
                            font.pixelSize: 12
                            text: win.agentNeeded
                                  ? qsTr("Claude Code isn't installed, so there is no agent to fetch writeups. Install it and sign in with `claude`.")
                                  : backend.problem
                        }
                    }

                    Text {
                        Layout.leftMargin: 22
                        Layout.topMargin: 4
                        visible: backend.hiddenCount > 0
                        text: qsTr("%n hidden by filters", "", backend.hiddenCount)
                        color: Style.faint
                        font.family: Style.sans
                        font.pixelSize: 11
                    }

                    Rectangle { Layout.fillWidth: true; Layout.topMargin: 8; height: 1; color: Style.line }

                    ListView {
                        id: list
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: backend.items
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        delegate: WriteupRow {
                            required property var modelData
                            required property int index
                            width: ListView.view.width
                            item: modelData
                            progress: win.progressOf(index)
                            selected: index === win.current
                            showDate: win.searching || backend.view === "favorites"
                            dayText: win.dayLabel(modelData.date)
                            onClicked: win.current = index
                        }

                        Text {
                            anchors.centerIn: parent
                            width: parent.width - 60
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            visible: backend.items.length === 0
                            text: win.emptyText()
                            color: Style.muted
                            font.family: Style.sans
                            font.pixelSize: 14
                        }
                    }
                }
            }

            // The open writeup, or statistics
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ReaderPane {
                    id: reader
                    anchors.fill: parent
                    visible: ["stats", "graph", "review"].indexOf(backend.view) < 0 || win.searching
                    item: win.item
                    progress: win.progressOf(win.current)
                    onOpenArticle: win.openArticle(win.current)
                    onToggleRead: win.toggleRead(win.current)
                    onToggleLab: win.toggleLab(win.current)
                    onToggleStar: win.toggleStar(win.current)
                    onFocusRequested: win.focusVisible = true
                    onPracticeToggled: (step, done) => backend.setPracticeDone(win.item.url, step, done)
                    onQuestionJudged: (question, result) => backend.setCheck(win.item, question, result)
                    onTagsEdited: tags => backend.setTags(win.item.url, tags)
                    onReferenceShown: id => { backend.graphSource = "references"; win.graphFocus = id; win.show("graph") }
                }
                Text {
                    anchors.centerIn: parent
                    visible: reader.visible && win.item === null
                    text: "WriteupPulse"
                    color: Style.line
                    font.family: Style.serif
                    font.pixelSize: 40
                }
                StatsView {
                    anchors.fill: parent
                    visible: backend.view === "stats" && !win.searching
                }
                Loader {
                    anchors.fill: parent
                    active: backend.view === "review" && !win.searching
                    sourceComponent: ReviewView { onWriteupChosen: item => win.openWriteup(item) }
                }
                // Only while shown: reading the graph lays it out.
                Loader {
                    anchors.fill: parent
                    active: backend.view === "graph" && !win.searching
                    sourceComponent: GraphView { onWriteupChosen: item => win.openWriteup(item) }
                }
            }
        }

        GetStarted {
            anchors.fill: parent
            z: 10
            visible: win.startVisible
            onVisibleChanged: if (visible) { step = 0; forceActiveFocus() }
            onFinished: { backend.onboarded = true; win.startAgain = false }
        }

        FocusView {
            id: focusView
            anchors.fill: parent
            visible: win.focusVisible
            items: backend.items
            index: win.current
            progress: win.progressOf(win.current)
            onClosed: win.focusVisible = false
            onOpenArticle: win.openArticle(win.current)
            onToggleStar: win.toggleStar(win.current)
            onPracticeToggled: (step, done) => backend.setPracticeDone(win.item.url, step, done)
            onQuestionJudged: (question, result) => backend.setCheck(win.item, question, result)
            onFinished: win.finishInFocus()
        }

        // A short confirmation after an export, at the bottom of the window.
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 28
            visible: backend.notice.length > 0
            implicitWidth: noticeText.implicitWidth + 36
            implicitHeight: 42
            radius: 21
            color: Style.raised
            border.color: Style.accent
            Text { id: noticeText; anchors.centerIn: parent; text: backend.notice; color: Style.ink; font.family: Style.sans; font.pixelSize: 13 }
        }

        // Settings: language, the daily timer, and which directions to search.
        Popup {
            anchors.centerIn: parent
            width: Math.min(parent.width - 80, 760)
            height: Math.min(parent.height - 80, settingsColumn.implicitHeight + 56)
            modal: true
            visible: win.settingsVisible
            onClosed: win.settingsVisible = false
            padding: 28
            background: Rectangle { radius: 16; color: Style.pane; border.color: Style.line }
            Overlay.modal: Rectangle { color: "#b0000000" }

            Flickable {
                anchors.fill: parent
                contentHeight: settingsColumn.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                ColumnLayout {
                    id: settingsColumn
                    width: parent.width
                    spacing: 14

                    Text { text: qsTr("Settings"); color: Style.ink; font.family: Style.sans; font.pixelSize: 22; font.weight: Font.DemiBold }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 14
                        Text { text: qsTr("Writeups from"); color: Style.muted; font.family: Style.sans; font.pixelSize: 14 }
                        Repeater {
                            model: [["feed", qsTr("The WriteupPulse site")], ["claude", qsTr("My Claude Code")]]
                            Pill {
                                required property var modelData
                                text: modelData[1]
                                on: backend.source === modelData[0]
                                onClicked: backend.source = modelData[0]
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: backend.source === "feed"
                              ? qsTr("New days come from the WriteupPulse site as soon as they are published. No Claude Code needed.")
                              : qsTr("Your own Claude Code finds and summarizes the writeups every morning, using your Claude usage.")
                        color: Style.faint
                        font.family: Style.sans
                        font.pixelSize: 12
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 14
                        Text { text: qsTr("My level"); color: Style.muted; font.family: Style.sans; font.pixelSize: 14 }
                        Repeater {
                            model: [""].concat(backend.levels)
                            Pill {
                                required property string modelData
                                text: modelData ? modelData : qsTr("Any")
                                on: backend.myLevel === modelData
                                onClicked: backend.myLevel = modelData
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: qsTr("Each day shows the writeups at your level first, and review starts new questions from them. Every day has at least one piece for beginners and one deep dive.")
                        color: Style.faint
                        font.family: Style.sans
                        font.pixelSize: 12
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 14
                        Text { text: qsTr("Language"); color: Style.muted; font.family: Style.sans; font.pixelSize: 14 }
                        Repeater {
                            model: [["en", "English"], ["ru", "Русский"], ["uk", "Українська"]]
                            Pill {
                                required property var modelData
                                text: modelData[1]
                                on: backend.language === modelData[0]
                                onClicked: backend.language = modelData[0]
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Switch {
                            visible: backend.timerAvailable
                            text: qsTr("Daily at 08:00")
                            checked: backend.timerEnabled
                            onToggled: { backend.setTimerEnabled(checked); checked = Qt.binding(() => backend.timerEnabled) }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: backend.source === "feed"
                              ? qsTr("The language is used for the window. Summaries from the site stay in the language they were published in.")
                              : qsTr("The language is used for the window and for new summaries, from the next fetch on.")
                        color: Style.faint
                        font.family: Style.sans
                        font.pixelSize: 12
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 14
                        Text { text: qsTr("Tutor model"); color: Style.muted; font.family: Style.sans; font.pixelSize: 14 }
                        TextField {
                            Layout.preferredWidth: 200
                            text: tutor.model
                            color: Style.ink
                            font.family: Style.sans
                            font.pixelSize: 13
                            onEditingFinished: tutor.model = text
                        }
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: tutor.status === "ready" ? qsTr("Ready")
                                : tutor.status === "noModel" ? qsTr("Not downloaded: ollama pull %1").arg(tutor.model)
                                : tutor.status === "battery" ? qsTr("Off on battery")
                                : qsTr("Ollama is not running")
                            color: tutor.status === "ready" ? Style.done : Style.faint
                            font.family: Style.sans
                            font.pixelSize: 12
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: qsTr("The Ask tab and practice reviews use this local Ollama model; nothing leaves your machine.")
                        color: Style.faint
                        font.family: Style.sans
                        font.pixelSize: 12
                    }

                    Switch {
                        text: qsTr("Save article texts for search")
                        checked: backend.saveTexts
                        onToggled: backend.saveTexts = checked
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: qsTr("The window downloads each writeup's page once, from the site that published it, and keeps the text on this machine, so search covers whole articles. Code blocks are left out.")
                        color: Style.faint
                        font.family: Style.sans
                        font.pixelSize: 12
                    }

                    Rectangle { Layout.fillWidth: true; Layout.topMargin: 6; height: 1; color: Style.line }
                    Text { text: qsTr("Notes"); color: Style.ink; font.family: Style.sans; font.pixelSize: 17; font.weight: Font.DemiBold }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 14
                        Text { text: qsTr("Notes folder"); color: Style.muted; font.family: Style.sans; font.pixelSize: 14 }
                        TextField {
                            id: folderField
                            Layout.fillWidth: true
                            text: backend.notesFolder
                            placeholderText: qsTr("An Obsidian vault or any folder")
                            color: Style.ink
                            font.family: Style.sans
                            font.pixelSize: 13
                            onEditingFinished: backend.notesFolder = text
                        }
                        ActionButton { text: qsTr("Choose…"); onClicked: folderDialog.open() }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 14
                        Text { text: qsTr("Notion"); color: Style.muted; font.family: Style.sans; font.pixelSize: 14 }
                        TextField {
                            Layout.fillWidth: true
                            text: backend.notionToken
                            placeholderText: qsTr("Integration token")
                            echoMode: TextInput.Password
                            color: Style.ink
                            font.family: Style.sans
                            font.pixelSize: 13
                            onEditingFinished: backend.notionToken = text
                        }
                        TextField {
                            Layout.fillWidth: true
                            text: backend.notionParent
                            placeholderText: qsTr("Link to the page for notes")
                            color: Style.ink
                            font.family: Style.sans
                            font.pixelSize: 13
                            onEditingFinished: backend.notionParent = text
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: qsTr("Notes (tab 6 on a writeup) are kept in the app. With a folder, each note is also saved there as Markdown with front matter and [[links]] to its terms, ready for Obsidian, Logseq, or any Markdown app. For Notion, create an integration at notion.so/my-integrations, share a page with it, and paste its token and the page's link: \"Send to Notion\" then adds a page under it. The token is kept in ~/.config on this computer.")
                        color: Style.faint
                        font.family: Style.sans
                        font.pixelSize: 12
                    }

                    Rectangle { Layout.fillWidth: true; Layout.topMargin: 6; height: 1; color: Style.line }
                    Text { text: qsTr("Directions"); color: Style.ink; font.family: Style.sans; font.pixelSize: 17; font.weight: Font.DemiBold }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Style.muted
                        font.family: Style.sans
                        font.pixelSize: 13
                        text: backend.source === "feed"
                              ? qsTr("Star the directions you want first in each day. The highlighted ones only matter with your own Claude Code.")
                              : qsTr("The agent searches the highlighted directions, from the next fetch on. Star the ones it should pick more often; they also come first in each day.")
                    }
                    Repeater {
                        model: backend.topics
                        ColumnLayout {
                            id: topic
                            required property string modelData
                            Layout.fillWidth: true
                            spacing: 8
                            Text {
                                Layout.topMargin: 4
                                text: win.topicName(topic.modelData)
                                color: Style.topicColor(topic.modelData)
                                font.family: Style.sans
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 8
                                Repeater {
                                    model: backend.categoriesOf(topic.modelData)
                                    Row {
                                        required property string modelData
                                        spacing: 2
                                        Pill {
                                            text: modelData
                                            tint: Style.topicColor(topic.modelData)
                                            on: backend.searchCategories.length === 0 || backend.searchCategories.indexOf(modelData) >= 0
                                            onClicked: win.toggleSearched(modelData)
                                        }
                                        Text {
                                            width: 26; height: 28
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                            text: backend.preferredCategories.indexOf(modelData) >= 0 ? "★" : "☆"
                                            color: backend.preferredCategories.indexOf(modelData) >= 0 ? Style.accent : Style.faint
                                            font.pixelSize: 16
                                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                                            TapHandler { onTapped: win.togglePreferred(modelData) }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    RowLayout {
                        Layout.topMargin: 8
                        Layout.fillWidth: true
                        ActionButton { text: qsTr("Search everything"); onClicked: { backend.searchCategories = []; backend.preferredCategories = [] } }
                        Item { Layout.fillWidth: true }
                        ActionButton { text: qsTr("Done"); primary: true; onClicked: win.settingsVisible = false }
                    }
                }
            }
        }

        FolderDialog {
            id: folderDialog
            title: qsTr("Notes folder")
            onAccepted: backend.notesFolder = selectedFolder.toString()
        }

        Popup {
            anchors.centerIn: parent
            modal: true
            visible: win.helpVisible
            onClosed: win.helpVisible = false
            padding: 28
            background: Rectangle { radius: 16; color: Style.pane; border.color: Style.line }
            Overlay.modal: Rectangle { color: "#b0000000" }

            HelpView {
                implicitWidth: Math.min(win.width - 140, 760)
                implicitHeight: Math.min(win.height - 120, 760)
                onGetStarted: { win.helpVisible = false; win.startAgain = true }
            }
        }
    }
}
