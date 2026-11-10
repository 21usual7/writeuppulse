import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Help: how to learn with the app day to day, what each view is for, what
// stays on the machine, and every shortcut.
Flickable {
    id: help

    signal getStarted()

    contentHeight: column.implicitHeight
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {}

    component Heading: Text {
        Layout.topMargin: 14
        color: Style.ink
        font.family: Style.sans
        font.pixelSize: 16
        font.weight: Font.DemiBold
    }
    component Para: Text {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        lineHeight: 1.35
        color: Style.muted
        font.family: Style.sans
        font.pixelSize: 13
    }

    ColumnLayout {
        id: column
        width: help.width - 16
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: qsTr("Help")
                color: Style.ink
                font.family: Style.sans
                font.pixelSize: 24
                font.weight: Font.DemiBold
            }
            ActionButton { text: qsTr("Get started again"); onClicked: help.getStarted() }
        }

        Heading { text: qsTr("A day with WriteupPulse") }
        Para {
            text: qsTr("Every morning brings five popular technical writeups, security first, with at least one a beginner can follow and one deep dive. Fifteen minutes a day is enough:")
        }
        Repeater {
            model: [
                qsTr("Open today's writeup at your level (it comes first) and read the summary. Unknown terms under it explain themselves."),
                qsTr("Press Enter to study it step by step: remember the takeaways, then check yourself and say whether you knew each answer."),
                qsTr("Pick one practice idea and try it in one of the suggested labs. Mark it done in a lab with B."),
                qsTr("Write a line in Notes (6): what you learned, what to try next."),
                qsTr("Tomorrow, press V: the questions come back until you know them. A day of review keeps your streak too.")
            ]
            RowLayout {
                required property string modelData
                required property int index
                Layout.fillWidth: true
                spacing: 10
                Text {
                    Layout.alignment: Qt.AlignTop
                    text: (index + 1) + "."
                    color: Style.accent
                    font.family: Style.sans
                    font.pixelSize: 13
                    font.bold: true
                }
                Para { text: modelData; color: Style.ink }
            }
        }

        Heading { text: qsTr("Views, on the left") }
        Repeater {
            model: [
                ["☰", qsTr("Days"), qsTr("Each day's writeups; ← and → move between days, the title picks one. Search (Ctrl+F) covers every saved day and, with article texts saved, the articles themselves.")],
                ["★", qsTr("Favorites"), qsTr("Writeups you starred with F, with your tags to filter them.")],
                ["↻", qsTr("Review (V)"), qsTr("Today's questions from earlier writeups, spaced out: 1, 3, 7, 16, then 35 days after each right answer.")],
                ["∑", qsTr("Statistics (S)"), qsTr("Your streak, what you read and practiced, and how many answers you knew.")],
                ["◈", qsTr("Concepts (G)"), qsTr("A map of the ATT&CK techniques, CWE weaknesses, CAPEC patterns, and OWASP categories your writeups name, and how they relate. With Ollama, it can show the local model's concepts instead.")]
            ]
            RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 12
                Text { Layout.alignment: Qt.AlignTop; Layout.preferredWidth: 18; text: modelData[0]; color: Style.faint; font.pixelSize: 15 }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text { text: modelData[1]; color: Style.ink; font.family: Style.sans; font.pixelSize: 13; font.weight: Font.DemiBold }
                    Para { text: modelData[2] }
                }
            }
        }

        Heading { text: qsTr("A writeup, in tabs") }
        Para {
            text: qsTr("Summary (1) with the terms it uses; Takeaways (2); Check yourself (3), questions with hidden answers; Practice (4), ideas to try by hand and legal labs to try them in; Ask (5), the local tutor, when Ollama runs; Notes (6), your own words, which can go to an Obsidian folder or Notion.")
        }

        Heading { text: qsTr("For learning, not attacking") }
        Para {
            text: qsTr("Summaries explain ideas in prose: no code, commands, payloads, or exploits. Practice only on systems you own or on labs built for it.")
        }

        Heading { text: qsTr("What stays on your computer") }
        Para {
            text: qsTr("Your progress, answers, notes, and saved article texts live in a local database. The window downloads each writeup's page from its own site, and new days from the WriteupPulse site. Nothing goes anywhere else unless you send a note to Notion or run the agent with your own Claude Code.")
        }

        Heading { text: qsTr("Shortcuts") }
        GridLayout {
            columns: 2
            columnSpacing: 28
            rowSpacing: 8
            Repeater {
                model: [
                    ["↑ ↓", qsTr("Previous or next writeup")],
                    ["← →", qsTr("Older or newer day")],
                    ["1 – 6", qsTr("Summary, takeaways, questions, practice, ask, notes")],
                    ["Enter", qsTr("Study the writeup step by step")],
                    ["O", qsTr("Open the article in the browser")],
                    ["M", qsTr("Mark as read")],
                    ["B", qsTr("Mark as done in a lab")],
                    ["F", qsTr("Add to or remove from favorites")],
                    ["U", qsTr("Show only unread")],
                    ["Ctrl+F  /", qsTr("Search all writeups")],
                    ["V", qsTr("Review today's questions (Space, Y, N)")],
                    ["S", qsTr("Statistics")],
                    ["G", qsTr("Map of concepts")],
                    ["P", qsTr("Settings and directions")],
                    ["E", qsTr("Export the day or week as Markdown")],
                    ["R", qsTr("Fetch today's writeups now")],
                    ["?", qsTr("Show or hide help")],
                    ["Q", qsTr("Quit")]
                ]
                delegate: Repeater {
                    required property var modelData
                    model: modelData
                    Text {
                        required property string modelData
                        required property int index
                        text: modelData
                        color: index === 0 ? Style.accent : Style.ink
                        font.family: index === 0 ? "JetBrains Mono" : Style.sans
                        font.pixelSize: 13
                        font.bold: index === 0
                    }
                }
            }
        }
    }
}
