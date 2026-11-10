import QtQuick
import QtQuick.Layouts

// One writeup at a time, studied in steps: read, remember, check yourself,
// practice. The day's writeups show as segments on top.
Rectangle {
    id: study

    property var items: []
    property int index: 0
    property var progress: ({})
    property int step: 0
    property int revealed: 0
    readonly property var item: index >= 0 && index < items.length ? items[index] : null
    readonly property var takeaways: item ? item.takeaways || [] : []
    readonly property var questions: item ? item.questions || [] : []
    readonly property var practice: item ? item.practice || [] : []
    readonly property var done: progress.done || []
    // Steps without content are skipped, so old digests are just "Read".
    readonly property var steps: [0].concat(takeaways.length ? [1] : [], questions.length ? [2] : [], practice.length ? [3] : [])
    readonly property bool lastStep: steps.indexOf(step) === steps.length - 1
    readonly property var stepNames: [qsTr("Read it"), qsTr("Remember"), qsTr("Check yourself"), qsTr("Practice")]

    signal closed()
    signal openArticle()
    signal toggleStar()
    signal practiceToggled(int index, bool done)
    signal questionJudged(string question, string result)
    signal finished()

    function nextStep() {
        const at = steps.indexOf(step)
        if (at < steps.length - 1) { step = steps[at + 1]; revealed = 0 }
        else finished()
    }
    function previousStep() {
        const at = steps.indexOf(step)
        if (at > 0) { step = steps[at - 1]; revealed = 0 }
    }
    function reveal() {
        if (step === 2 && revealed < questions.length) ++revealed
        else nextStep()
    }

    onIndexChanged: { step = 0; revealed = 0 }
    color: Style.bg

    RowLayout {
        id: top
        x: 40; y: 24
        width: parent.width - 80
        spacing: 20

        Icon {
            name: "close"
            size: 20
            color: closeHover.hovered ? Style.ink : Style.muted
            HoverHandler { id: closeHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: study.closed() }
        }
        Text { text: "WriteupPulse"; color: Style.ink; font.family: Style.sans; font.pixelSize: 16; font.weight: Font.DemiBold }
        Item { Layout.fillWidth: true }
        Row {
            spacing: 6
            Repeater {
                model: study.items
                Rectangle {
                    required property var modelData
                    required property int index
                    width: Math.min(54, 300 / Math.max(1, study.items.length))
                    height: 6
                    radius: 3
                    color: index === study.index ? Style.accent
                         : (backend.progressRevision, backend.state(modelData.url).read !== undefined) ? Style.done : Style.raised
                }
            }
        }
        Text {
            text: qsTr("%1 / %2").arg((study.index + 1).toLocaleString(Qt.locale(), 'f', 0))
                                 .arg(study.items.length.toLocaleString(Qt.locale(), 'f', 0))
            color: Style.muted
            font.family: Style.sans
            font.pixelSize: 13
        }
        Item { Layout.fillWidth: true }
        Rectangle {
            visible: backend.streak > 0
            width: streakText.implicitWidth + 24
            height: 30
            radius: 15
            color: Style.accentSoft
            Text {
                id: streakText
                anchors.centerIn: parent
                text: "🔥 " + qsTr("%n-day streak", "", backend.streak)
                color: Style.accent
                font.family: Style.sans
                font.pixelSize: 13
            }
        }
    }

    Rectangle {
        id: card
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: top.bottom
        anchors.topMargin: 28
        anchors.bottom: hints.top
        anchors.bottomMargin: 20
        width: Math.min(parent.width - 80, 860)
        radius: 18
        color: Style.pane

        Flickable {
            id: body
            anchors { left: parent.left; right: parent.right; top: parent.top; bottom: footer.top; margins: 48; bottomMargin: 16 }
            contentHeight: column.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: column
                width: body.width
                spacing: 0

                Text {
                    text: study.item ? [study.item.category || "Other", study.item.level].filter(s => s).join("  ·  ").toUpperCase() : ""
                    color: Style.topicColor(backend.topicOf(study.item ? study.item.category || "Other" : "Other"))
                    font.family: Style.sans
                    font.pixelSize: 11
                    font.letterSpacing: 1.6
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: 14
                    text: study.item ? study.item.title : ""
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    lineHeight: 1.15
                    color: Style.ink
                    font.family: Style.serif
                    font.pixelSize: 30
                }

                Flow {
                    Layout.fillWidth: true
                    Layout.topMargin: 24
                    spacing: 10
                    Repeater {
                        model: study.steps
                        Rectangle {
                            required property int modelData
                            readonly property bool current: modelData === study.step
                            readonly property bool passed: study.steps.indexOf(modelData) < study.steps.indexOf(study.step)
                            width: stepLabel.implicitWidth + 40
                            height: 34
                            radius: 17
                            color: current ? Style.accent : passed ? Qt.rgba(0.5, 0.82, 0.63, 0.14) : Style.raised
                            Text {
                                id: stepLabel
                                anchors.centerIn: parent
                                text: (parent.passed ? "✓  " : "") + study.stepNames[modelData]
                                color: parent.current ? Style.accentInk : parent.passed ? Style.done : Style.muted
                                font.family: Style.sans
                                font.pixelSize: 13
                                font.weight: parent.current ? Font.DemiBold : Font.Normal
                            }
                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: { study.step = modelData; study.revealed = 0 } }
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: 26
                    visible: study.step === 0
                    text: study.item ? study.item.summary : ""
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    lineHeight: 1.6
                    color: Style.ink
                    font.family: Style.serif
                    font.pixelSize: 17
                }

                Repeater {
                    model: study.step === 1 ? study.takeaways : []
                    RowLayout {
                        required property string modelData
                        required property int index
                        Layout.fillWidth: true
                        Layout.topMargin: index === 0 ? 26 : 14
                        spacing: 16
                        Text {
                            Layout.alignment: Qt.AlignTop
                            text: (index + 1).toLocaleString(Qt.locale(), 'f', 0)
                            color: Style.accent
                            font.family: Style.serif
                            font.pixelSize: 17
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            lineHeight: 1.4
                            color: Style.ink
                            font.family: Style.serif
                            font.pixelSize: 17
                        }
                    }
                }

                Text {
                    Layout.topMargin: 26
                    visible: study.step === 2
                    text: qsTr("Answer in your head, reveal, then say if you knew it.")
                    color: Style.muted
                    font.family: Style.sans
                    font.pixelSize: 13
                }
                Repeater {
                    model: study.step === 2 ? study.questions : []
                    QuestionCard {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        Layout.topMargin: 12
                        question: modelData.question
                        answer: modelData.answer
                        shown: index < study.revealed
                        result: (study.progress.checks || {})[modelData.question] || ""
                        onJudged: result => study.questionJudged(modelData.question, result)
                    }
                }

                Text {
                    Layout.topMargin: 26
                    visible: study.step === 3
                    text: qsTr("Only in a lab or on systems you own.")
                    color: Style.muted
                    font.family: Style.sans
                    font.pixelSize: 13
                }
                Repeater {
                    model: study.step === 3 ? study.practice : []
                    PracticeStep {
                        required property string modelData
                        required property int index
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        text: modelData
                        checked: study.done.indexOf(index) >= 0
                        onToggled: study.practiceToggled(index, !checked)
                    }
                }
            }
        }

        Rectangle { anchors.bottom: footer.top; anchors.bottomMargin: 18; x: 48; width: parent.width - 96; height: 1; color: Style.raised }
        RowLayout {
            id: footer
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 24
            x: 48
            width: parent.width - 96
            spacing: 22
            Text {
                text: qsTr("Open article") + " ↗"
                color: Style.accent
                font.family: Style.sans
                font.pixelSize: 13
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: study.openArticle() }
            }
            Text {
                text: study.progress.favorite ? "★ " + qsTr("Saved") : "☆ " + qsTr("Save")
                color: study.progress.favorite ? Style.accent : Style.muted
                font.family: Style.sans
                font.pixelSize: 13
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: study.toggleStar() }
            }
            Item { Layout.fillWidth: true }
            ActionButton {
                text: study.lastStep ? qsTr("Mark read, next writeup") + "  →" : qsTr("Next step") + "  →"
                primary: true
                onClicked: study.nextStep()
            }
        }
    }

    Text {
        id: hints
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 22
        text: qsTr("←  previous step     Space  reveal or next     →  next step     M  mark read     Esc  back to the list")
        color: Style.faint
        font.family: Style.sans
        font.pixelSize: 12
    }
}
