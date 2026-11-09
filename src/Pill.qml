import QtQuick

// A rounded toggle for filters and tags.
Rectangle {
    id: pill

    property string text
    property bool on: false
    property color tint: Style.accent
    property bool removable: false

    signal clicked()
    signal removed()

    implicitWidth: row.implicitWidth + 22
    implicitHeight: 28
    radius: height / 2
    color: on ? Qt.rgba(tint.r, tint.g, tint.b, 0.16) : hover.hovered ? Style.hover : "transparent"
    border.color: on ? tint : Style.line

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: pill.text
            textFormat: Text.PlainText
            color: pill.on ? pill.tint : Style.muted
            font.family: Style.sans
            font.pixelSize: 12
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: pill.removable
            text: "×"
            color: Style.muted
            font.pixelSize: 14
            TapHandler { onTapped: pill.removed() }
        }
    }

    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: pill.clicked() }
}
