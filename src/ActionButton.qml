import QtQuick
import QtQuick.Controls

// A flat button in the app's own style; `primary` fills it with the accent.
Rectangle {
    id: button

    property string text
    property bool primary: false
    property bool on: false
    property string hint

    signal clicked()

    implicitWidth: label.implicitWidth + 26
    implicitHeight: 34
    radius: Style.radius
    color: primary ? (hover.hovered ? Qt.lighter(Style.accent, 1.1) : Style.accent)
                   : hover.hovered ? Style.hover : Style.raised
    opacity: enabled ? 1 : 0.5

    Text {
        id: label
        anchors.centerIn: parent
        text: button.text
        color: button.primary ? Style.accentInk : button.on ? Style.accent : Style.ink
        font.family: Style.sans
        font.pixelSize: 13
        font.weight: button.primary ? Font.DemiBold : Font.Normal
    }

    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: button.clicked() }
    ToolTip.visible: hint.length > 0 && hover.hovered
    ToolTip.text: hint
    ToolTip.delay: 500
}
