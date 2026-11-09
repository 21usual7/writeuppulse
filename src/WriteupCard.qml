import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// One writeup: collapsed shows the gist, expanded the full summary and the
// takeaways. Agent text is shown as plain text, never as rich text.
Rectangle {
    id: card

    required property var item
    required property int number
    property bool selected: false
    property bool expanded: false
    property var progress: ({})
    readonly property bool isRead: progress.read !== undefined
    readonly property bool inLab: progress.lab !== undefined
    readonly property bool starred: progress.favorite === true

    signal clicked()
    signal toggled()
    signal opened()
    signal readToggled()
    signal labToggled()
    signal starToggled()

    readonly property color dim: "#9a9aa2"

    implicitHeight: content.implicitHeight + 32
    radius: theme.radius
    color: "#17171a"
    border.width: selected ? 2 : 1
    border.color: selected ? theme.accent : "#26262b"

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: { card.clicked(); card.toggled() }
    }

    ColumnLayout {
        id: content
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Label {
                Layout.alignment: Qt.AlignTop
                text: card.number.toLocaleString(Qt.locale(), 'f', 0)
                color: theme.accent
                font.pixelSize: 18
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                text: card.item.title
                opacity: card.isRead ? 0.6 : 1
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                font.pixelSize: 17
                font.bold: true
            }
        }

        Label {
            Layout.fillWidth: true
            text: [card.item.source, card.item.popularity].concat(card.item.tags || []).filter(s => s).join("  ·  ")
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: card.dim
            font.pixelSize: 13
        }

        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: card.item.why || ""
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: card.dim
            font.italic: true
        }

        Label {
            Layout.fillWidth: true
            text: card.expanded ? card.item.summary : card.item.summary.split(/\n\s*\n/)[0]
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            maximumLineCount: card.expanded ? 1000 : 3
            elide: Text.ElideRight
            lineHeight: 1.2
            font.pixelSize: 15
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            visible: card.expanded && (card.item.takeaways || []).length > 0
            spacing: 6

            Label {
                text: qsTr("Takeaways")
                color: theme.accent
                font.bold: true
            }
            Repeater {
                model: card.item.takeaways || []
                RowLayout {
                    required property string modelData
                    Layout.fillWidth: true
                    spacing: 8
                    Label { Layout.alignment: Qt.AlignTop; text: "•"; color: theme.accent }
                    Label {
                        Layout.fillWidth: true
                        text: modelData
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                    }
                }
            }
        }

        // Wraps instead of pushing past the card in longer languages.
        Flow {
            Layout.fillWidth: true
            spacing: 4
            Button {
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
                Material.foreground: card.isRead ? theme.accent : "#e6e6e6"
                onClicked: { card.clicked(); card.readToggled() }
            }
            Button {
                flat: true
                text: card.inLab ? "✓ " + qsTr("Done in lab") : qsTr("Done in lab")
                Material.roundedScale: theme.radius
                Material.foreground: card.inLab ? theme.accent : "#e6e6e6"
                onClicked: { card.clicked(); card.labToggled() }
            }
            Button {
                flat: true
                text: card.starred ? "★" : "☆"
                font.pixelSize: 18
                Material.roundedScale: theme.radius
                Material.foreground: card.starred ? theme.accent : "#e6e6e6"
                ToolTip.visible: hovered
                ToolTip.text: card.starred ? qsTr("Remove from favorites") : qsTr("Add to favorites")
                onClicked: { card.clicked(); card.starToggled() }
            }
        }
    }
}
