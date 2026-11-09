import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The streak, the counts, and which directions the user reads, as bars.
Flickable {
    id: stats

    readonly property color dim: "#9a9aa2"
    readonly property var byCategory: {
        const counts = backend.stats.byCategory || {}
        return Object.keys(counts).map(k => ({ name: k, count: counts[k] })).sort((a, b) => b.count - a.count)
    }
    readonly property int most: byCategory.length ? byCategory[0].count : 1

    contentHeight: column.implicitHeight
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {}

    ColumnLayout {
        id: column
        width: Math.min(stats.width - 12, 760)
        spacing: 20

        Label {
            text: qsTr("Statistics")
            font.pixelSize: 20
            font.bold: true
        }

        GridLayout {
            Layout.fillWidth: true
            columns: stats.width > 620 ? 4 : 2
            columnSpacing: 12
            rowSpacing: 12
            Repeater {
                model: [
                    [backend.streak, qsTr("days in a row")],
                    [backend.stats.read || 0, qsTr("writeups read")],
                    [backend.stats.lab || 0, qsTr("done in a lab")],
                    [backend.stats.favorites || 0, qsTr("in favorites")]
                ]
                Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 92
                    radius: theme.radius
                    color: "#17171a"
                    Column {
                        anchors.centerIn: parent
                        spacing: 4
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: modelData[0].toLocaleString(Qt.locale(), 'f', 0)
                            color: theme.accent
                            font.pixelSize: 30
                            font.bold: true
                        }
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: modelData[1]
                            color: stats.dim
                        }
                    }
                }
            }
        }

        Label {
            visible: stats.byCategory.length > 0
            text: qsTr("Read by direction")
            font.pixelSize: 16
            font.bold: true
        }

        Repeater {
            model: stats.byCategory
            RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 12
                Label {
                    Layout.preferredWidth: 170
                    text: modelData.name
                    elide: Text.ElideRight
                }
                Item {
                    Layout.fillWidth: true
                    implicitHeight: 14
                    Rectangle {
                        width: Math.max(4, parent.width * modelData.count / stats.most)
                        height: parent.height
                        radius: Math.min(4, theme.radius)
                        color: theme.accent
                    }
                }
                Label {
                    Layout.preferredWidth: 36
                    horizontalAlignment: Text.AlignRight
                    text: modelData.count.toLocaleString(Qt.locale(), 'f', 0)
                    color: stats.dim
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: (backend.stats.read || 0) === 0
            wrapMode: Text.Wrap
            text: qsTr("Mark writeups as read with M to see your progress here.")
            color: stats.dim
        }
    }
}
