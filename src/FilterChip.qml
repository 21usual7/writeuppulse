import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A small toggle used for filters and tags. Drawn by hand rather than as a
// highlighted Material button, whose text stays white on a light accent.
Rectangle {
    id: chip

    property string text
    property bool on: false
    property bool removable: false

    signal clicked()
    signal removed()

    implicitWidth: row.implicitWidth + 20
    implicitHeight: 28
    radius: Math.min(4, theme.radius)
    color: on ? theme.accent : hover.hovered ? "#26262c" : "#1d1d21"
    border.color: on ? theme.accent : "#2c2c32"

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6
        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: chip.text
            textFormat: Text.PlainText
            color: chip.on ? theme.accentForeground : "#d0d0d6"
            font.pixelSize: 13
        }
        Label {
            anchors.verticalCenter: parent.verticalCenter
            visible: chip.removable
            text: "×"
            color: chip.on ? theme.accentForeground : "#9a9aa2"
            font.pixelSize: 15
            TapHandler { onTapped: chip.removed() }
        }
    }

    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: chip.clicked() }
}
