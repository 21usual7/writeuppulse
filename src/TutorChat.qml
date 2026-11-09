import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The Ask tab: a chat with the local tutor about the open writeup, or about
// one practice idea. Model text is plain text, like the agent's.
ColumnLayout {
    id: chat

    property var item: null
    property string lastSent

    spacing: 12

    function send(text) {
        if (!text.trim().length || tutor.busy) return
        lastSent = text
        input.text = ""
        tutor.ask(text)
    }

    // A failed question comes back into the field, so it can be sent again.
    Connections {
        target: tutor
        function onMessagesChanged() {
            if (tutor.error.length && !tutor.busy && !input.text.length) input.text = chat.lastSent
        }
    }

    Text {
        Layout.fillWidth: true
        visible: tutor.status !== "ready"
        wrapMode: Text.Wrap
        lineHeight: 1.4
        color: Style.muted
        font.family: Style.sans
        font.pixelSize: 14
        text: tutor.status === "missing"
              ? qsTr("The tutor runs on your machine with Ollama, so your questions stay local. Install and start it (sudo pacman -S ollama, then systemctl enable --now ollama), and download the model: ollama pull %1").arg(tutor.model)
              : qsTr("Ollama is running, but the model %1 isn't downloaded yet. Run: ollama pull %1. Or pick another model in Settings (P).").arg(tutor.model)
    }
    ActionButton {
        visible: tutor.status !== "ready"
        text: qsTr("Check again")
        onClicked: tutor.check()
    }

    RowLayout {
        Layout.fillWidth: true
        visible: tutor.status === "ready" && tutor.practice >= 0
        spacing: 10
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("Practice: %1").arg(chat.item && chat.item.practice ? chat.item.practice[tutor.practice] || "" : "")
            textFormat: Text.PlainText
            color: Style.accent
            font.family: Style.sans
            font.pixelSize: 13
        }
        ActionButton { text: qsTr("About the whole writeup"); onClicked: tutor.open(chat.item, -1) }
    }

    Text {
        Layout.fillWidth: true
        visible: tutor.status === "ready" && tutor.messages.length === 0
        wrapMode: Text.Wrap
        color: Style.faint
        font.family: Style.sans
        font.pixelSize: 13
        text: tutor.practice >= 0
              ? qsTr("Describe what you did in your lab or where you are stuck, and the tutor will review it and suggest the next step.")
              : qsTr("Ask anything about this writeup. The tutor answers from its summary with a local model, so it can be wrong; check the original for details.")
    }
    Flow {
        Layout.fillWidth: true
        visible: tutor.status === "ready" && tutor.messages.length === 0
        spacing: 8
        Repeater {
            model: tutor.practice >= 0
                   ? [qsTr("Where do I start?"), qsTr("I'm stuck, give me a hint")]
                   : [qsTr("Explain it more simply"), qsTr("What should I know before reading the original?"), qsTr("Why does this matter in practice?")]
            Pill {
                required property string modelData
                text: modelData
                onClicked: chat.send(modelData)
            }
        }
    }

    Repeater {
        model: tutor.status === "ready" ? tutor.messages : []
        Rectangle {
            required property var modelData
            readonly property bool mine: modelData.role === "user"
            Layout.fillWidth: !mine
            Layout.maximumWidth: chat.width
            Layout.alignment: mine ? Qt.AlignRight : Qt.AlignLeft
            implicitWidth: Math.min(message.implicitWidth + 28, chat.width * 0.85)
            implicitHeight: message.implicitHeight + 22
            radius: 10
            color: mine ? Style.accentSoft : Style.raised
            Text {
                id: message
                x: 14; y: 11
                width: parent.width - 28
                text: modelData.text.length ? modelData.text : "…"
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                lineHeight: 1.4
                color: Style.ink
                font.family: Style.sans
                font.pixelSize: 14
            }
        }
    }

    Text {
        Layout.fillWidth: true
        visible: tutor.error.length > 0
        wrapMode: Text.Wrap
        text: tutor.error
        textFormat: Text.PlainText
        color: Style.danger
        font.family: Style.sans
        font.pixelSize: 12
    }

    RowLayout {
        Layout.fillWidth: true
        visible: tutor.status === "ready"
        spacing: 8
        TextArea {
            id: input
            Layout.fillWidth: true
            wrapMode: TextEdit.Wrap
            placeholderText: tutor.practice >= 0 ? qsTr("What did you try?") : qsTr("Ask about this writeup")
            placeholderTextColor: Style.faint
            color: Style.ink
            font.family: Style.sans
            font.pixelSize: 14
            // Enter sends, Shift+Enter starts a new line; Esc goes back to the list.
            Keys.onReturnPressed: event => {
                if (event.modifiers & Qt.ShiftModifier) event.accepted = false
                else chat.send(text)
            }
        }
        ActionButton {
            Layout.alignment: Qt.AlignBottom
            text: tutor.busy ? qsTr("Stop") : qsTr("Send")
            primary: !tutor.busy
            enabled: tutor.busy || input.text.trim().length > 0
            onClicked: tutor.busy ? tutor.stop() : chat.send(input.text)
        }
    }
}
