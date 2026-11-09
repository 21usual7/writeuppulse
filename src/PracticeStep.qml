import QtQuick
import QtQuick.Layouts

// One practice idea with a checkbox; progress owns whether it is done.
Item {
    id: step

    property string text
    property bool checked: false

    signal toggled()

    implicitHeight: Math.max(label.implicitHeight, 20) + 16

    Rectangle {
        id: box
        x: 0; y: 10
        width: 18; height: 18; radius: 4
        color: step.checked ? Style.done : "transparent"
        border.color: step.checked ? Style.done : Style.faint
        Text {
            anchors.centerIn: parent
            visible: step.checked
            text: "✓"
            color: Style.bg
            font.pixelSize: 12
            font.bold: true
        }
    }
    Text {
        id: label
        x: 32; y: 8
        width: parent.width - 32
        text: step.text
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        lineHeight: 1.3
        color: step.checked ? Style.muted : Style.ink
        font.family: Style.sans
        font.pixelSize: 15
    }

    HoverHandler { cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: step.toggled() }
}
