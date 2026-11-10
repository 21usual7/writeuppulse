import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Today's review: one self-check question at a time, from writeups read
// before. Saying whether it was known moves the question to a later day
// (see Progress::review), so the list only gets shorter.
Item {
    id: review

    signal writeupChosen(var item)

    readonly property var state: backend.review
    readonly property var cards: state.cards || []
    readonly property var card: cards.length ? cards[0] : null
    property bool shown: false

    onCardChanged: shown = false

    function judge(result) {
        if (!card || !shown) return
        backend.setCheck(card, card.question, result)
    }
    function boxText(box) {
        translator.language
        if (box === 0) return qsTr("New question")
        return qsTr("Seen before, step %1 of 5").arg(box)
    }

    Shortcut { sequence: "Space"; enabled: !win.typing && review.card !== null; onActivated: review.shown = true }
    Shortcut { sequence: "Y"; enabled: !win.typing; onActivated: review.judge("knew") }
    Shortcut { sequence: "N"; enabled: !win.typing; onActivated: review.judge("missed") }

    Flickable {
        anchors.fill: parent
        contentHeight: column.implicitHeight + 80
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: column
            x: Math.max(56, (parent.width - width) / 2)
            y: 40
            width: Math.min(review.width - 112, 720)
            spacing: 18

            Text {
                text: qsTr("Review")
                color: Style.ink
                font.family: Style.sans
                font.pixelSize: 28
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: {
                    translator.language
                    const n = x => x.toLocaleString(Qt.locale(), 'f', 0)
                    return qsTr("Left for today: %1  ·  answered today: %2").arg(n(review.cards.length)).arg(n(review.state.reviewedToday || 0))
                }
                color: Style.muted
                font.family: Style.sans
                font.pixelSize: 13
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 10
                visible: review.card !== null
                implicitHeight: cardColumn.implicitHeight + 56
                radius: 16
                color: Style.pane

                ColumnLayout {
                    id: cardColumn
                    x: 28; y: 28
                    width: parent.width - 56
                    spacing: 16

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Rectangle {
                            width: 8; height: 8; radius: 4
                            color: Style.topicColor(backend.topicOf(review.card ? review.card.category || "Other" : "Other"))
                        }
                        Text {
                            Layout.fillWidth: true
                            text: review.card ? review.card.title : ""
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: titleHover.hovered ? Style.accent : Style.muted
                            font.family: Style.sans
                            font.pixelSize: 13
                            HoverHandler { id: titleHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: review.writeupChosen(review.card) }
                        }
                        Text {
                            text: review.card ? review.boxText(review.card.box) : ""
                            color: review.card && review.card.box === 0 ? Style.accent : Style.faint
                            font.family: Style.sans
                            font.pixelSize: 12
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: review.card ? review.card.question : ""
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        color: Style.ink
                        font.family: Style.serif
                        font.pixelSize: 22
                        lineHeight: 1.3
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: !review.shown
                        text: qsTr("Answer it in your head first, then reveal.")
                        color: Style.faint
                        font.family: Style.sans
                        font.pixelSize: 13
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        visible: review.shown
                        implicitHeight: answerText.implicitHeight + 32
                        radius: 12
                        color: Style.raised
                        Text {
                            id: answerText
                            x: 16; y: 16
                            width: parent.width - 32
                            text: review.card ? review.card.answer : ""
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            lineHeight: 1.45
                            color: Style.done
                            font.family: Style.serif
                            font.pixelSize: 16
                        }
                    }
                    Row {
                        spacing: 10
                        ActionButton {
                            visible: !review.shown
                            text: qsTr("Reveal the answer")
                            hint: "Space"
                            primary: true
                            onClicked: review.shown = true
                        }
                        ActionButton {
                            visible: review.shown
                            text: qsTr("I knew it")
                            hint: "Y"
                            primary: true
                            onClicked: review.judge("knew")
                        }
                        ActionButton {
                            visible: review.shown
                            text: qsTr("I didn't")
                            hint: "N"
                            onClicked: review.judge("missed")
                        }
                    }
                }
            }

            // Done for today, or nothing to go over yet.
            Text {
                Layout.fillWidth: true
                Layout.topMargin: 30
                visible: review.card === null
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                text: {
                    translator.language
                    if (!backend.dates.length)
                        return qsTr("Every writeup comes with questions. Once you have writeups, they come back here a few at a time, until you know them.")
                    const next = review.state.nextDue
                    const when = next ? win.dayLabel(next) : ""
                    return (review.state.reviewedToday || 0) > 0
                        ? qsTr("That's all for today.") + (when ? " " + qsTr("Next questions: %1").arg(when) : "")
                        : qsTr("Nothing to review yet. Questions from writeups you read come back here the next day.")
                }
                color: Style.muted
                font.family: Style.sans
                font.pixelSize: 15
                lineHeight: 1.3
            }

            Text {
                Layout.fillWidth: true
                Layout.topMargin: 10
                wrapMode: Text.Wrap
                text: qsTr("A question you knew comes back later each time: in 1, 3, 7, 16, then 35 days. One you didn't comes back tomorrow. Up to five new questions join a day, from writeups you marked read first.")
                color: Style.faint
                font.family: Style.sans
                font.pixelSize: 12
            }
        }
    }
}
