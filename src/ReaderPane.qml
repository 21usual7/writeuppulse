import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The open writeup: its header and actions, then the summary, takeaways,
// self-check questions, practice, and the local tutor as tabs. Agent text is
// plain text.
Flickable {
    id: pane

    property var item: null
    property var progress: ({})
    property int tab: 0
    readonly property var takeaways: item ? item.takeaways || [] : []
    readonly property var questions: item ? item.questions || [] : []
    readonly property var practice: item ? item.practice || [] : []
    readonly property var done: progress.done || []
    readonly property var userTags: progress.tags || []
    readonly property bool learning: questions.length > 0 || practice.length > 0

    signal openArticle()
    signal toggleRead()
    signal toggleLab()
    signal toggleStar()
    signal focusRequested()
    signal practiceToggled(int index, bool done)
    signal questionJudged(string question, string result)
    signal tagsEdited(var tags)
    signal referenceShown(string id)

    // The ATT&CK, CWE, CAPEC, and OWASP entries this writeup names, and the one opened.
    readonly property var references: item ? backend.references(item.url) : []
    property string term: ""

    // The note being edited, saved a moment after the last keystroke and
    // before another writeup opens.
    property string noteText: ""
    property var noteItem: null
    function saveNote() {
        if (noteTimer.running) {
            noteTimer.stop()
            if (noteItem) backend.setNote(noteItem, noteText)
        }
    }
    Timer { id: noteTimer; interval: 800; onTriggered: if (pane.noteItem) backend.setNote(pane.noteItem, pane.noteText) }

    onItemChanged: {
        saveNote()
        noteItem = item
        noteText = item ? backend.note(item.url) : ""
        tab = 0
        term = ""
        contentY = 0
        if (item) tutor.open(item, -1)
    }

    // Keeps the newest answer in view while it streams in.
    Connections {
        target: tutor
        function onMessagesChanged() {
            if (pane.tab === 4) pane.contentY = Math.max(0, pane.contentHeight - pane.height)
        }
    }

    contentHeight: page.implicitHeight + 80
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {}

    function count(n) { return n.toLocaleString(Qt.locale(), 'f', 0) }

    ColumnLayout {
        id: page
        visible: pane.item !== null
        x: 56; y: 40
        width: Math.min(pane.width - 112, 720)
        spacing: 0

        Flow {
            Layout.fillWidth: true
            spacing: 8
            Pill {
                text: pane.item ? pane.item.category || "Other" : ""
                on: true
                tint: Style.topicColor(backend.topicOf(pane.item ? pane.item.category || "Other" : "Other"))
            }
            Pill { visible: !!(pane.item && pane.item.level); text: pane.item ? pane.item.level || "" : "" }
            Text {
                height: 28
                verticalAlignment: Text.AlignVCenter
                text: pane.item ? "  " + [pane.item.source, pane.item.popularity].filter(s => s).join("  ·  ") : ""
                textFormat: Text.PlainText
                color: Style.faint
                font.family: Style.sans
                font.pixelSize: 12
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: 14
            text: pane.item ? pane.item.title : ""
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            lineHeight: 1.15
            color: Style.ink
            font.family: Style.sans
            font.pixelSize: 28
            font.weight: Font.DemiBold
        }
        Text {
            Layout.fillWidth: true
            Layout.topMargin: 10
            visible: text.length > 0
            text: pane.item ? pane.item.why || "" : ""
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: Style.muted
            font.family: Style.sans
            font.pixelSize: 15
        }

        Flow {
            Layout.fillWidth: true
            Layout.topMargin: 20
            spacing: 8
            ActionButton { text: qsTr("Open article") + " ↗"; primary: true; hint: "O"; onClicked: pane.openArticle() }
            ActionButton {
                text: pane.learning ? qsTr("Study step by step") : qsTr("Focus mode")
                hint: "Enter"
                onClicked: pane.focusRequested()
            }
            ActionButton {
                text: pane.progress.read !== undefined ? "✓ " + qsTr("Read") : qsTr("Mark read")
                on: pane.progress.read !== undefined
                hint: "M"
                onClicked: pane.toggleRead()
            }
            ActionButton {
                text: pane.progress.lab !== undefined ? "✓ " + qsTr("Done in lab") : qsTr("Done in lab")
                on: pane.progress.lab !== undefined
                hint: "B"
                onClicked: pane.toggleLab()
            }
            ActionButton {
                text: pane.progress.favorite ? "★" : "☆"
                on: pane.progress.favorite === true
                hint: pane.progress.favorite ? qsTr("Remove from favorites") : qsTr("Add to favorites")
                onClicked: pane.toggleStar()
            }
        }

        Flow {
            Layout.fillWidth: true
            Layout.topMargin: 12
            spacing: 6
            Repeater {
                model: pane.userTags
                Pill {
                    required property string modelData
                    text: "#" + modelData
                    removable: true
                    onRemoved: pane.tagsEdited(pane.userTags.filter(t => t !== modelData))
                }
            }
            TextField {
                width: 150
                height: 30
                topPadding: 0
                bottomPadding: 0
                placeholderText: "+ " + qsTr("Add a tag")
                placeholderTextColor: Style.faint
                color: Style.ink
                font.family: Style.sans
                font.pixelSize: 12
                background: Rectangle { radius: 15; color: "transparent"; border.color: parent.activeFocus ? Style.accent : Style.line }
                leftPadding: 12
                onAccepted: {
                    const tag = text.trim().replace(/^#/, "")
                    if (tag.length) pane.tagsEdited(pane.userTags.concat([tag]))
                    text = ""
                }
            }
        }

        // Tabs
        RowLayout {
            Layout.topMargin: 28
            spacing: 26
            Repeater {
                model: [
                    qsTr("Summary"),
                    qsTr("Takeaways") + "  " + pane.count(pane.takeaways.length),
                    qsTr("Check yourself") + "  " + pane.count(pane.questions.length),
                    qsTr("Practice") + "  " + qsTr("%1/%2").arg(pane.count(pane.done.length)).arg(pane.count(pane.practice.length)),
                    qsTr("Ask"),
                    qsTr("Notes") + (pane.noteText.trim().length > 0 ? "  •" : "")
                ]
                Column {
                    required property string modelData
                    required property int index
                    readonly property bool empty: index === 1 ? pane.takeaways.length === 0
                                                : index === 2 ? pane.questions.length === 0
                                                : index === 3 ? pane.practice.length === 0 : false
                    spacing: 8
                    opacity: empty ? 0.4 : 1
                    Text {
                        text: modelData
                        color: pane.tab === index ? Style.ink : Style.muted
                        font.family: Style.sans
                        font.pixelSize: 14
                        font.weight: pane.tab === index ? Font.DemiBold : Font.Normal
                    }
                    Rectangle { width: parent.width; height: 2; color: pane.tab === index ? Style.accent : "transparent" }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: pane.tab = index }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Style.line }

        // Summary
        Text {
            Layout.fillWidth: true
            Layout.topMargin: 22
            visible: pane.tab === 0
            text: pane.item ? pane.item.summary : ""
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            lineHeight: 1.6
            color: Style.ink
            font.family: Style.sans
            font.pixelSize: 16
        }

        // Glossary: the catalog terms the writeup uses, each with its definition.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 28
            visible: pane.tab === 0 && pane.references.length > 0
            spacing: 10
            Text {
                text: qsTr("Terms in this writeup")
                color: Style.muted
                font.family: Style.sans
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: pane.references.slice(0, 12)
                    Pill {
                        required property var modelData
                        text: modelData.name.length > 42 ? modelData.id : modelData.name
                        tint: Style.kindColor(modelData.kind)
                        on: pane.term === modelData.id
                        onClicked: pane.term = on ? "" : modelData.id
                    }
                }
            }
            Rectangle {
                id: definition
                readonly property var entry: pane.references.find(r => r.id === pane.term) || null
                Layout.fillWidth: true
                visible: entry !== null
                implicitHeight: definitionColumn.implicitHeight + 32
                radius: 12
                color: Style.raised
                ColumnLayout {
                    id: definitionColumn
                    x: 16; y: 16
                    width: parent.width - 32
                    spacing: 8
                    Text {
                        Layout.fillWidth: true
                        text: definition.entry ? definition.entry.name : ""
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        color: Style.ink
                        font.family: Style.sans
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: definition.entry ? definition.entry.id : ""
                        color: definition.entry ? Style.kindColor(definition.entry.kind) : Style.faint
                        font.family: Style.sans
                        font.pixelSize: 12
                    }
                    Text {
                        Layout.fillWidth: true
                        text: definition.entry ? definition.entry.text : ""
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        lineHeight: 1.45
                        color: Style.ink
                        font.family: Style.serif
                        font.pixelSize: 14
                    }
                    Row {
                        spacing: 18
                        Text {
                            text: (definition.entry && definition.entry.kind === "owasp" ? qsTr("Read at OWASP") : qsTr("Read at MITRE")) + " ↗"
                            color: readHover.hovered ? Style.ink : Style.accent
                            font.family: Style.sans
                            font.pixelSize: 13
                            HoverHandler { id: readHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: Qt.openUrlExternally(definition.entry.link) }
                        }
                        Text {
                            text: qsTr("Show on the map")
                            color: mapHover.hovered ? Style.ink : Style.accent
                            font.family: Style.sans
                            font.pixelSize: 13
                            HoverHandler { id: mapHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: pane.referenceShown(definition.entry.id) }
                        }
                    }
                }
            }
        }

        // Takeaways
        GridLayout {
            Layout.fillWidth: true
            Layout.topMargin: 22
            visible: pane.tab === 1
            columns: page.width > 560 ? 2 : 1
            columnSpacing: 10
            rowSpacing: 10
            Repeater {
                model: pane.tab === 1 ? pane.takeaways : []
                Rectangle {
                    required property string modelData
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    implicitHeight: takeaway.implicitHeight + 28
                    radius: 10
                    color: Style.raised
                    Text {
                        id: takeaway
                        x: 16; y: 14
                        width: parent.width - 32
                        text: modelData
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        lineHeight: 1.3
                        color: Style.ink
                        font.family: Style.sans
                        font.pixelSize: 14
                    }
                }
            }
        }

        // Check yourself
        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 18
            visible: pane.tab === 2
            spacing: 10
            Repeater {
                model: pane.tab === 2 ? pane.questions : []
                QuestionCard {
                    required property var modelData
                    Layout.fillWidth: true
                    question: modelData.question
                    answer: modelData.answer
                    result: (pane.progress.checks || {})[modelData.question] || ""
                    onJudged: result => pane.questionJudged(modelData.question, result)
                }
            }
        }

        // Practice
        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 18
            visible: pane.tab === 3
            spacing: 4
            Repeater {
                model: pane.tab === 3 ? pane.practice : []
                PracticeStep {
                    required property string modelData
                    required property int index
                    Layout.fillWidth: true
                    text: modelData
                    checked: pane.done.indexOf(index) >= 0
                    discussable: true
                    onToggled: pane.practiceToggled(index, !checked)
                    onDiscuss: { tutor.open(pane.item, index); pane.tab = 4 }
                }
            }
        }

        // Where to try it: legal labs for the writeup's direction and level.
        ColumnLayout {
            readonly property var labs: pane.item ? backend.labs(pane.item.category || "", pane.item.level || "") : []
            Layout.fillWidth: true
            Layout.topMargin: 26
            visible: pane.tab === 3 && labs.length > 0
            spacing: 10
            Text {
                text: qsTr("Where to practice legally")
                color: Style.muted
                font.family: Style.sans
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Repeater {
                model: parent.labs
                Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: labColumn.implicitHeight + 24
                    radius: 12
                    color: labHover.hovered ? Style.hover : Style.raised
                    Column {
                        id: labColumn
                        x: 16; y: 12
                        width: parent.width - 32
                        spacing: 4
                        Row {
                            spacing: 10
                            Text {
                                text: modelData.name + " ↗"
                                color: Style.ink
                                font.family: Style.sans
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                            }
                            Text {
                                anchors.baseline: parent.children[0].baseline
                                text: (modelData.open ? qsTr("open source, on your machine") : qsTr("free online"))
                                      + "  ·  " + modelData.levels.join(", ")
                                color: Style.faint
                                font.family: Style.sans
                                font.pixelSize: 11
                            }
                        }
                        Text {
                            width: parent.width
                            text: {
                                translator.language
                                return modelData.about[translator.language] || modelData.about.en
                            }
                            wrapMode: Text.Wrap
                            color: Style.muted
                            font.family: Style.sans
                            font.pixelSize: 13
                        }
                    }
                    HoverHandler { id: labHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: Qt.openUrlExternally(modelData.url) }
                }
            }
        }

        // Ask
        TutorChat {
            Layout.fillWidth: true
            Layout.topMargin: 18
            visible: pane.tab === 4
            item: pane.item
        }

        // Notes: the reader's own words, kept here and, when set up, in a
        // notes folder (Obsidian) or Notion.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 18
            visible: pane.tab === 5
            spacing: 12
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: Math.max(220, noteArea.implicitHeight + 20)
                radius: 12
                color: Style.raised
                TextArea {
                    id: noteArea
                    anchors.fill: parent
                    anchors.margins: 4
                    text: pane.noteText
                    wrapMode: TextEdit.Wrap
                    color: Style.ink
                    font.family: Style.sans
                    font.pixelSize: 14
                    background: null
                    onTextChanged: if (text !== pane.noteText) { pane.noteText = text; noteTimer.restart() }
                    onActiveFocusChanged: if (!activeFocus) pane.saveNote()
                    Text {
                        x: noteArea.leftPadding; y: noteArea.topPadding
                        width: noteArea.width - noteArea.leftPadding - noteArea.rightPadding
                        visible: noteArea.text.length === 0
                        wrapMode: Text.Wrap
                        text: qsTr("What did you learn, what to try next, what to look up. Markdown works.")
                        color: Style.faint
                        font: noteArea.font
                    }
                }
            }
            Row {
                spacing: 10
                ActionButton {
                    visible: backend.notesFolder !== ""
                    text: qsTr("Save to notes folder")
                    hint: backend.notesFolder
                    onClicked: { pane.saveNote(); backend.saveToFolder(pane.item) }
                }
                ActionButton {
                    visible: backend.notionToken !== ""
                    text: backend.sending ? qsTr("Sending…") : qsTr("Send to Notion")
                    enabled: !backend.sending
                    onClicked: { pane.saveNote(); backend.sendToNotion(pane.item) }
                }
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: backend.notesFolder !== ""
                      ? qsTr("Each saved note is also written to %1/WriteupPulse with the summary, takeaways, and terms as links. A file you edit there is yours: the app won't overwrite it.").arg(backend.notesFolder)
                      : qsTr("Notes stay on this computer. To keep them in Obsidian or another Markdown app, or in Notion, set it up in Settings (P).")
                color: Style.faint
                font.family: Style.sans
                font.pixelSize: 12
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: 22
            visible: pane.tab > 0 && pane.tab < 4 && !pane.learning
            wrapMode: Text.Wrap
            text: qsTr("This day was fetched before learning mode, so it has no questions or practice.")
            color: Style.muted
            font.family: Style.sans
            font.pixelSize: 14
        }
    }
}
