import QtQuick
import QtQuick.Layouts

// A self-check question; the answer stays hidden until asked for. Once it
// shows, the reader says whether they knew it; tapping the same choice again
// takes it back.
Rectangle {
    id: card

    property string question
    property string answer
    property bool shown: false
    // "knew", "missed", or empty.
    property string result

    signal judged(string result)

    implicitHeight: column.implicitHeight + 32
    radius: 12
    color: shown ? Style.raised : "transparent"
    border.color: shown ? "transparent" : Style.raised

    ColumnLayout {
        id: column
        x: 20; y: 16
        width: parent.width - 40
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            HoverHandler { cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: card.shown = !card.shown }
            Text {
                visible: card.result.length > 0
                text: card.result === "knew" ? "✓" : "✗"
                color: card.result === "knew" ? Style.done : Style.danger
                font.family: Style.sans
                font.pixelSize: 15
            }
            Text {
                Layout.fillWidth: true
                text: card.question
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: Style.ink
                font.family: Style.sans
                font.pixelSize: 15
            }
            Text {
                text: card.shown ? qsTr("Hide") : qsTr("Reveal")
                color: Style.accent
                font.family: Style.sans
                font.pixelSize: 13
            }
        }
        Text {
            Layout.fillWidth: true
            visible: card.shown
            text: card.answer
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            lineHeight: 1.45
            color: Style.done
            font.family: Style.serif
            font.pixelSize: 15
        }
        Row {
            visible: card.shown
            spacing: 8
            Repeater {
                model: [["knew", qsTr("I knew it")], ["missed", qsTr("I didn't")]]
                Pill {
                    required property var modelData
                    text: modelData[1]
                    on: card.result === modelData[0]
                    tint: modelData[0] === "knew" ? Style.done : Style.danger
                    onClicked: card.judged(card.result === modelData[0] ? "" : modelData[0])
                }
            }
        }
    }
}
