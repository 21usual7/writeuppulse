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

    onItemChanged: {
        tab = 0
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
                    qsTr("Ask")
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

        // Ask
        TutorChat {
            Layout.fillWidth: true
            Layout.topMargin: 18
            visible: pane.tab === 4
            item: pane.item
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
