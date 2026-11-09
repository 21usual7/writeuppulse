import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// One writeup. The quick view shows the gist; the deep view adds the full
// summary, takeaways as cards, self-check questions, the practice checklist,
// and the user's tags. Agent text is shown as plain text, never rich text.
Rectangle {
    id: card

    required property var item
    required property int number
    property bool selected: false
    property bool expanded: false
    property bool showDate: false
    property string dayText
    property var progress: ({})
    readonly property bool isRead: progress.read !== undefined
    readonly property bool inLab: progress.lab !== undefined
    readonly property bool starred: progress.favorite === true
    readonly property var done: progress.done || []
    readonly property var userTags: progress.tags || []
    readonly property var questions: item.questions || []
    readonly property var practice: item.practice || []
    readonly property var takeaways: item.takeaways || []

    signal clicked()
    signal toggled()
    signal opened()
    signal readToggled()
    signal labToggled()
    signal starToggled()
    signal practiceToggled(int index, bool done)
    signal tagsEdited(var tags)

    readonly property color dim: "#9a9aa2"
    readonly property color text: "#e6e6e6"

    implicitHeight: content.implicitHeight + 36
    radius: theme.radius
    color: "#17171a"
    border.width: selected ? 2 : 1
    border.color: selected ? theme.accent : "#26262b"

    TapHandler { onTapped: card.clicked() }

    ColumnLayout {
        id: content
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 18 }
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Label {
                Layout.alignment: Qt.AlignTop
                text: card.number.toLocaleString(Qt.locale(), 'f', 0)
                color: theme.accent
                font.pixelSize: 19
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                text: card.item.title
                opacity: card.isRead ? 0.6 : 1
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                font.pixelSize: 18
                font.bold: true
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: 8
            Rectangle {
                visible: card.item.category !== undefined
                implicitWidth: categoryLabel.implicitWidth + 14
                implicitHeight: 22
                radius: Math.min(4, theme.radius)
                color: "transparent"
                border.color: theme.accent
                Label {
                    id: categoryLabel
                    anchors.centerIn: parent
                    text: card.item.category || ""
                    color: theme.accent
                    font.pixelSize: 12
                }
            }
            Rectangle {
                visible: !!card.item.level
                implicitWidth: levelLabel.implicitWidth + 14
                implicitHeight: 22
                radius: Math.min(4, theme.radius)
                color: "#24242a"
                Label {
                    id: levelLabel
                    anchors.centerIn: parent
                    text: card.item.level || ""
                    color: "#d0d0d6"
                    font.pixelSize: 12
                }
            }
            Label {
                height: 22
                verticalAlignment: Text.AlignVCenter
                text: [card.showDate ? card.dayText : "", card.item.source, card.item.popularity]
                      .concat(card.item.tags || []).filter(s => s).join("  ·  ")
                textFormat: Text.PlainText
                color: card.dim
                font.pixelSize: 13
            }
        }

        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: card.item.why || ""
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: card.dim
            font.italic: true
            font.pixelSize: 14
        }

        Label {
            Layout.fillWidth: true
            text: card.expanded ? card.item.summary : card.item.summary.split(/\n\s*\n/)[0]
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            maximumLineCount: card.expanded ? 1000 : 2
            elide: Text.ElideRight
            lineHeight: 1.3
            font.pixelSize: 15
            color: card.text
        }

        // Takeaways as small cards, two to a row when there is room.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6
            visible: card.expanded && card.takeaways.length > 0
            spacing: 8
            Label { text: qsTr("Takeaways"); color: theme.accent; font.bold: true; font.pixelSize: 14 }
            GridLayout {
                Layout.fillWidth: true
                columns: card.width > 760 ? 2 : 1
                columnSpacing: 8
                rowSpacing: 8
                Repeater {
                    model: card.expanded ? card.takeaways : []
                    Rectangle {
                        required property string modelData
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        implicitHeight: takeaway.implicitHeight + 20
                        radius: Math.min(4, theme.radius)
                        color: "#1f1f24"
                        Rectangle { width: 3; height: parent.height; color: theme.accent; radius: Math.min(2, theme.radius) }
                        Label {
                            id: takeaway
                            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: 14; rightMargin: 10 }
                            text: modelData
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            lineHeight: 1.2
                            color: card.text
                        }
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6
            visible: card.expanded && card.questions.length > 0
            spacing: 6
            Label { text: qsTr("Check yourself"); color: theme.accent; font.bold: true; font.pixelSize: 14 }
            Repeater {
                model: card.expanded ? card.questions : []
                ColumnLayout {
                    id: question
                    required property var modelData
                    required property int index
                    property bool shown: false
                    Layout.fillWidth: true
                    spacing: 2
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: (question.index + 1).toLocaleString(Qt.locale(), 'f', 0) + ". " + question.modelData.question
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            color: card.text
                        }
                        Button {
                            flat: true
                            text: question.shown ? qsTr("Hide answer") : qsTr("Show answer")
                            font.pixelSize: 13
                            Material.roundedScale: theme.radius
                            Material.foreground: theme.accent
                            onClicked: question.shown = !question.shown
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        Layout.leftMargin: 18
                        Layout.bottomMargin: 4
                        visible: question.shown
                        text: question.modelData.answer
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        color: card.dim
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6
            visible: card.expanded && card.practice.length > 0
            spacing: 0
            Label {
                text: qsTr("Try it yourself") + "  ·  " + qsTr("%1 of %2 done")
                      .arg(card.done.length.toLocaleString(Qt.locale(), 'f', 0))
                      .arg(card.practice.length.toLocaleString(Qt.locale(), 'f', 0))
                color: theme.accent
                font.bold: true
                font.pixelSize: 14
            }
            Repeater {
                model: card.expanded ? card.practice : []
                RowLayout {
                    id: step
                    required property string modelData
                    required property int index
                    readonly property bool checked: card.done.indexOf(index) >= 0
                    Layout.fillWidth: true
                    spacing: 0
                    CheckBox {
                        Layout.alignment: Qt.AlignTop
                        checked: step.checked
                        // Progress owns the state; returning the current one keeps
                        // the binding instead of letting the click overwrite it.
                        nextCheckState: function() { card.practiceToggled(step.index, !step.checked); return checkState }
                    }
                    Label {
                        Layout.fillWidth: true
                        Layout.topMargin: 12
                        text: step.modelData
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        color: step.checked ? card.dim : card.text
                        font.strikeout: step.checked
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: card.expanded && card.questions.length === 0 && card.practice.length === 0
            wrapMode: Text.Wrap
            text: qsTr("This day was fetched before learning mode, so it has no questions or practice.")
            color: card.dim
            font.pixelSize: 13
        }

        Flow {
            Layout.fillWidth: true
            Layout.topMargin: 4
            visible: card.expanded || card.userTags.length > 0
            spacing: 6
            Repeater {
                model: card.userTags
                FilterChip {
                    required property string modelData
                    text: "#" + modelData
                    removable: card.expanded
                    onRemoved: card.tagsEdited(card.userTags.filter(t => t !== modelData))
                }
            }
            TextField {
                visible: card.expanded
                width: 160
                height: 36
                placeholderText: qsTr("Add a tag")
                font.pixelSize: 13
                Material.containerStyle: Material.Outlined
                Material.roundedScale: theme.radius
                onAccepted: {
                    const tag = text.trim().replace(/^#/, "")
                    if (tag.length) card.tagsEdited(card.userTags.concat([tag]))
                    text = ""
                }
            }
        }

        // Wraps instead of pushing past the card in longer languages.
        Flow {
            Layout.fillWidth: true
            spacing: 4
            Button {
                visible: !backend.deepMode
                flat: true
                text: card.expanded ? qsTr("Collapse") : qsTr("Read more")
                Material.roundedScale: theme.radius
                onClicked: { card.clicked(); card.toggled() }
            }
            Button {
                flat: true
                text: qsTr("Open article")
                Material.roundedScale: theme.radius
                Material.foreground: theme.accent
                onClicked: { card.clicked(); card.opened() }
            }
            Button {
                flat: true
                text: card.isRead ? "✓ " + qsTr("Read") : qsTr("Mark read")
                Material.roundedScale: theme.radius
                Material.foreground: card.isRead ? theme.accent : card.text
                onClicked: { card.clicked(); card.readToggled() }
            }
            Button {
                flat: true
                text: card.inLab ? "✓ " + qsTr("Done in lab") : qsTr("Done in lab")
                Material.roundedScale: theme.radius
                Material.foreground: card.inLab ? theme.accent : card.text
                onClicked: { card.clicked(); card.labToggled() }
            }
            Button {
                flat: true
                text: card.starred ? "★" : "☆"
                font.pixelSize: 18
                Material.roundedScale: theme.radius
                Material.foreground: card.starred ? theme.accent : card.text
                ToolTip.visible: hovered
                ToolTip.text: card.starred ? qsTr("Remove from favorites") : qsTr("Add to favorites")
                onClicked: { card.clicked(); card.starToggled() }
            }
        }
    }
}
