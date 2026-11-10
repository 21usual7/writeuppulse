import QtQuick
import QtQuick.Controls

// A thin scroll bar in the app's colours; Material's grows into a wide grey
// block that covers the content beside it.
ScrollBar {
    id: bar
    implicitWidth: 10
    padding: 2
    background: null
    contentItem: Rectangle {
        implicitWidth: 6
        radius: 3
        color: bar.pressed ? Style.muted : bar.hovered ? Style.faint : Style.line
        opacity: bar.policy === ScrollBar.AlwaysOn || bar.active ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
    }
}
