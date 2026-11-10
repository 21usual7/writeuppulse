import QtQuick

// One writeup in the list: its direction, level, and title; read ones fade.
Rectangle {
    id: row

    required property var item
    property var progress: ({})
    property bool selected: false
    property bool showDate: false
    property string dayText
    readonly property bool isRead: progress.read !== undefined

    signal clicked()

    implicitHeight: column.implicitHeight + 28
    color: selected ? Style.raised : hover.hovered ? Qt.rgba(1, 1, 1, 0.02) : "transparent"

    Rectangle { visible: row.selected; width: 3; height: parent.height; color: Style.accent }

    Column {
        id: column
        x: 22; y: 14
        width: parent.width - 44
        spacing: 6

        Row {
            spacing: 8
            Rectangle {
                width: 7; height: 7; radius: 4
                anchors.verticalCenter: parent.verticalCenter
                color: Style.topicColor(backend.topicOf(row.item.category || "Other"))
            }
            Text {
                text: [row.showDate ? row.dayText : "", row.item.category, row.item.level].filter(s => s).join("  ·  ")
                color: Style.muted
                font.family: Style.sans
                font.pixelSize: 11
            }
            Text {
                visible: backend.myLevel !== "" && row.item.level === backend.myLevel
                text: "·  " + qsTr("your level")
                color: Style.accent
                font.family: Style.sans
                font.pixelSize: 11
            }
        }
        Text {
            width: parent.width
            text: row.item.title
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
            lineHeight: 1.15
            color: row.isRead ? Style.muted : Style.ink
            font.family: Style.sans
            font.pixelSize: 14
            font.weight: row.isRead ? Font.Normal : Font.DemiBold
        }
        Text {
            width: parent.width
            text: [row.item.source, row.progress.favorite ? "★" : "", row.progress.lab !== undefined ? "⚗" : ""].filter(s => s).join("   ")
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: Style.faint
            font.family: Style.sans
            font.pixelSize: 11
        }
    }

    Rectangle { anchors.bottom: parent.bottom; x: 22; width: parent.width - 44; height: 1; color: Style.line }

    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: row.clicked() }
}
