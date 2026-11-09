import QtQuick
import QtQuick.Layouts

// One practice idea with a checkbox; progress owns whether it is done. With
// `discussable`, a link opens the tutor on this idea.
Item {
    id: step

    property string text
    property bool checked: false
    property bool discussable: false

    signal toggled()
    signal discuss()

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
        width: parent.width - 32 - (step.discussable ? discussLink.width + 16 : 0)
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

    Text {
        id: discussLink
        anchors.right: parent.right
        y: 10
        visible: step.discussable
        text: qsTr("Discuss") + " →"
        color: discussHover.hovered ? Style.accent : Style.faint
        font.family: Style.sans
        font.pixelSize: 12
        HoverHandler { id: discussHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: step.discuss() }
    }
}
