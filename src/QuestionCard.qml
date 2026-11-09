import QtQuick
import QtQuick.Layouts

// A self-check question; the answer stays hidden until asked for.
Rectangle {
    id: card

    property string question
    property string answer
    property bool shown: false

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
    }

    HoverHandler { cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: card.shown = !card.shown }
}
