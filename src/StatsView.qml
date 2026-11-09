import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The streak, the counts, and which directions the user reads, as bars.
Flickable {
    id: stats

    readonly property var byCategory: {
        const counts = backend.stats.byCategory || {}
        return Object.keys(counts).map(k => ({ name: k, count: counts[k] })).sort((a, b) => b.count - a.count)
    }
    readonly property int most: byCategory.length ? byCategory[0].count : 1

    contentHeight: column.implicitHeight + 80
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {}

    ColumnLayout {
        id: column
        x: 56; y: 40
        width: Math.min(stats.width - 112, 760)
        spacing: 22

        Text {
            text: qsTr("Statistics")
            color: Style.ink
            font.family: Style.sans
            font.pixelSize: 28
            font.weight: Font.DemiBold
        }

        GridLayout {
            Layout.fillWidth: true
            columns: column.width > 680 ? 5 : 3
            columnSpacing: 12
            rowSpacing: 12
            Repeater {
                model: [
                    [backend.streak, qsTr("days in a row"), Style.accent],
                    [backend.stats.read || 0, qsTr("writeups read"), Style.ink],
                    [backend.stats.lab || 0, qsTr("done in a lab"), Style.done],
                    [backend.stats.favorites || 0, qsTr("in favorites"), Style.ink],
                    [backend.stats.knew || 0, qsTr("answers known of %1").arg((backend.stats.checked || 0).toLocaleString(Qt.locale(), 'f', 0)), Style.done]
                ]
                Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 104
                    radius: 14
                    color: Style.pane
                    Column {
                        anchors.centerIn: parent
                        spacing: 6
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: modelData[0].toLocaleString(Qt.locale(), 'f', 0)
                            color: modelData[2]
                            font.family: Style.sans
                            font.pixelSize: 34
                            font.weight: Font.DemiBold
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: modelData[1]
                            color: Style.muted
                            font.family: Style.sans
                            font.pixelSize: 13
                        }
                    }
                }
            }
        }

        Text {
            Layout.topMargin: 10
            visible: stats.byCategory.length > 0
            text: qsTr("Read by direction")
            color: Style.ink
            font.family: Style.sans
            font.pixelSize: 17
            font.weight: Font.DemiBold
        }

        Repeater {
            model: stats.byCategory
            RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 14
                Text {
                    Layout.preferredWidth: 170
                    text: modelData.name
                    elide: Text.ElideRight
                    color: Style.ink
                    font.family: Style.sans
                    font.pixelSize: 14
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 10
                    radius: 5
                    color: Style.raised
                    Rectangle {
                        width: Math.max(10, parent.width * modelData.count / stats.most)
                        height: parent.height
                        radius: 5
                        color: Style.topicColor(backend.topicOf(modelData.name))
                    }
                }
                Text {
                    Layout.preferredWidth: 36
                    horizontalAlignment: Text.AlignRight
                    text: modelData.count.toLocaleString(Qt.locale(), 'f', 0)
                    color: Style.muted
                    font.family: Style.sans
                    font.pixelSize: 13
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: (backend.stats.read || 0) === 0
            wrapMode: Text.Wrap
            text: qsTr("Mark writeups as read with M to see your progress here.")
            color: Style.muted
            font.family: Style.sans
            font.pixelSize: 14
        }
    }
}
