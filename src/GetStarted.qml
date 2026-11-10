import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The first run, in four steps: what the app is, the reader's level and
// language, how a day of learning goes, and where things come from and go.
// Shown once; Help brings it back.
Rectangle {
    id: start

    property int step: 0
    readonly property int steps: 4

    signal finished()

    color: Qt.rgba(Style.bg.r, Style.bg.g, Style.bg.b, 0.97)

    // Swallows clicks and keys meant for the window underneath.
    MouseArea { anchors.fill: parent; hoverEnabled: true }
    Keys.onPressed: event => event.accepted = true

    function next() { if (step < steps - 1) step++; else finished() }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 80, 660)
        height: Math.min(parent.height - 80, card.implicitHeight + 64)
        radius: 18
        color: Style.pane
        border.color: Style.line

        Flickable {
            anchors.fill: parent
            anchors.margins: 32
            contentHeight: card.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: card
                width: parent.width
                spacing: 16

                // Progress dots
                Row {
                    spacing: 6
                    Repeater {
                        model: start.steps
                        Rectangle {
                            required property int index
                            width: index === start.step ? 22 : 8; height: 8; radius: 4
                            color: index <= start.step ? Style.accent : Style.raised
                            Behavior on width { NumberAnimation { duration: 150 } }
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: [qsTr("Learn something new every day"), qsTr("Make it yours"), qsTr("Fifteen minutes a day"),
                           qsTr("Where things come from")][start.step]
                    color: Style.ink
                    font.family: Style.sans
                    font.pixelSize: 26
                    font.weight: Font.DemiBold
                }

                // 1. What it is
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: start.step === 0
                    spacing: 12
                    Repeater {
                        model: [
                            qsTr("Every morning WriteupPulse brings five popular technical writeups, security first: web, pwn, reverse, crypto, cloud, red and blue team, malware, AI."),
                            qsTr("Each one comes summarized, with the main takeaways, questions to check yourself, and practice ideas for a lab you control."),
                            qsTr("It is for understanding, not attacking: no code, commands, or exploits, only ideas explained in words.")
                        ]
                        Text {
                            required property string modelData
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            lineHeight: 1.4
                            text: modelData
                            color: Style.muted
                            font.family: Style.sans
                            font.pixelSize: 15
                        }
                    }
                }

                // 2. Level and language
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: start.step === 1
                    spacing: 12
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: qsTr("Where are you now? Writeups at your level come first each day, and review starts with them. You can change it any time in Settings (P).")
                        color: Style.muted
                        font.family: Style.sans
                        font.pixelSize: 15
                        lineHeight: 1.4
                    }
                    Repeater {
                        model: [["Junior", qsTr("Just starting: I want the basics explained.")],
                                ["Middle", qsTr("I know the basics and want to go deeper.")],
                                ["Senior", qsTr("I want the hard, novel research.")],
                                ["", qsTr("Show me everything as it comes.")]]
                        Rectangle {
                            required property var modelData
                            readonly property bool on: backend.myLevel === modelData[0]
                            Layout.fillWidth: true
                            implicitHeight: 52
                            radius: 12
                            color: on ? Style.accentSoft : levelHover.hovered ? Style.hover : Style.raised
                            border.color: on ? Style.accent : "transparent"
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 16
                                anchors.rightMargin: 16
                                spacing: 14
                                Text {
                                    Layout.preferredWidth: 70
                                    text: modelData[0] || qsTr("Any")
                                    color: parent.parent.on ? Style.accent : Style.ink
                                    font.family: Style.sans
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData[1]
                                    elide: Text.ElideRight
                                    color: Style.muted
                                    font.family: Style.sans
                                    font.pixelSize: 13
                                }
                            }
                            HoverHandler { id: levelHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: backend.myLevel = modelData[0] }
                        }
                    }
                    RowLayout {
                        Layout.topMargin: 6
                        spacing: 10
                        Text { text: qsTr("Language"); color: Style.muted; font.family: Style.sans; font.pixelSize: 14 }
                        Repeater {
                            model: [["en", "English"], ["ru", "Русский"], ["uk", "Українська"]]
                            Pill {
                                required property var modelData
                                text: modelData[1]
                                on: backend.language === modelData[0]
                                onClicked: backend.language = modelData[0]
                            }
                        }
                    }
                }

                // 3. A day
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: start.step === 2
                    spacing: 12
                    Repeater {
                        model: [
                            ["1", qsTr("Read"), qsTr("Open today's writeup and its summary. Terms like XSS or Kerberoasting explain themselves, with links to MITRE and OWASP.")],
                            ["2", qsTr("Check yourself"), qsTr("Press Enter for focus mode: takeaways, then questions. Say honestly whether you knew each answer.")],
                            ["3", qsTr("Practice"), qsTr("Try one idea in a suggested lab, like OWASP Juice Shop or pwn.college, and write a note.")],
                            ["4", qsTr("Review"), qsTr("Tomorrow press V: questions come back, spaced out, until you know them.")]
                        ]
                        RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 14
                            Rectangle {
                                Layout.alignment: Qt.AlignTop
                                width: 28; height: 28; radius: 14
                                color: Style.accentSoft
                                Text { anchors.centerIn: parent; text: modelData[0]; color: Style.accent; font.family: Style.sans; font.pixelSize: 13; font.bold: true }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text { text: modelData[1]; color: Style.ink; font.family: Style.sans; font.pixelSize: 15; font.weight: Font.DemiBold }
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    text: modelData[2]
                                    color: Style.muted
                                    font.family: Style.sans
                                    font.pixelSize: 13
                                    lineHeight: 1.3
                                }
                            }
                        }
                    }
                }

                // 4. Sources and privacy
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: start.step === 3
                    spacing: 12
                    Repeater {
                        model: [
                            backend.source === "feed"
                                ? qsTr("New days come from the WriteupPulse site, where an AI agent picks and summarizes them each morning. No account needed.")
                                : qsTr("Your own Claude Code finds and summarizes the writeups every morning, using your Claude usage."),
                            qsTr("Your progress, answers, and notes stay on this computer. In Settings you can also save notes to an Obsidian folder or send them to Notion."),
                            qsTr("Optional: with Ollama installed, a local model can tutor you (tab 5) and draw its own concept map. It runs only on AC power."),
                            qsTr("Press ? any time for help and shortcuts.")
                        ]
                        Text {
                            required property string modelData
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            lineHeight: 1.4
                            text: modelData
                            color: Style.muted
                            font.family: Style.sans
                            font.pixelSize: 15
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 10
                    spacing: 10
                    Text {
                        text: qsTr("Skip")
                        visible: start.step < start.steps - 1
                        color: skipHover.hovered ? Style.ink : Style.faint
                        font.family: Style.sans
                        font.pixelSize: 13
                        HoverHandler { id: skipHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: start.finished() }
                    }
                    Item { Layout.fillWidth: true }
                    ActionButton { visible: start.step > 0; text: qsTr("Back"); onClicked: start.step-- }
                    ActionButton {
                        primary: true
                        text: start.step < start.steps - 1 ? qsTr("Next") : qsTr("Start learning")
                        onClicked: start.next()
                    }
                }
            }
        }
    }
}
