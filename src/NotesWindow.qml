import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The note on one writeup, in a small window of its own. There is one such
// window: it stays on the writeup it was opened for while the main window
// moves on, and opening notes on another writeup saves this one and switches.
Window {
    id: notes

    property var item: null
    property string note: ""

    // A dialog of the main window: tiling window managers (Hyprland) float it
    // over the reader instead of halving the screen for it.
    transientParent: win
    flags: Qt.Dialog
    width: 480
    height: 560
    minimumWidth: 340
    minimumHeight: 280
    title: item ? qsTr("Notes") + " · " + item.title : qsTr("Notes")
    color: Style.bg

    Material.theme: Material.Dark
    Material.accent: Style.accent
    Material.background: Style.raised
    Material.foreground: Style.ink

    function openFor(writeup) {
        if (!writeup) return
        if (!item || item.url !== writeup.url) {
            save()
            item = writeup
            note = backend.note(writeup.url)
            area.text = note
            area.cursorPosition = area.length
        }
        show()
        raise()
        requestActivate()
        area.forceActiveFocus()
    }
    function save() {
        if (saveTimer.running) {
            saveTimer.stop()
            if (item) backend.setNote(item, note)
        }
    }

    Timer { id: saveTimer; interval: 800; onTriggered: if (notes.item) backend.setNote(notes.item, notes.note) }
    onClosing: save()
    onActiveChanged: if (!active) save()

    Shortcut { sequence: "Esc"; onActivated: notes.close() }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12

        Text {
            Layout.fillWidth: true
            text: notes.item ? notes.item.title : ""
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            color: Style.ink
            font.family: Style.sans
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        Text {
            Layout.fillWidth: true
            text: notes.item ? [notes.item.category, notes.item.date].filter(s => s).join("  ·  ") : ""
            textFormat: Text.PlainText
            color: Style.faint
            font.family: Style.sans
            font.pixelSize: 12
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 12
            color: Style.raised
            border.color: area.activeFocus ? Style.accentSoft : "transparent"

            ScrollView {
                anchors.fill: parent
                anchors.margins: 4
                TextArea {
                    id: area
                    wrapMode: TextEdit.Wrap
                    color: Style.ink
                    selectByMouse: true
                    font.family: Style.sans
                    font.pixelSize: 14
                    background: null
                    rightPadding: 16
                    placeholderText: qsTr("What did you learn, what to try next, what to look up. Markdown works.")
                    placeholderTextColor: Style.faint
                    onTextChanged: if (text !== notes.note) { notes.note = text; saveTimer.restart() }
                }
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: 8
            ActionButton {
                visible: backend.notesFolder !== ""
                text: qsTr("Save to notes folder")
                hint: backend.notesFolder
                onClicked: { notes.save(); backend.saveToFolder(notes.item) }
            }
            ActionButton {
                visible: backend.notionToken !== ""
                text: backend.sending ? qsTr("Sending…") : qsTr("Send to Notion")
                enabled: !backend.sending
                onClicked: { notes.save(); backend.sendToNotion(notes.item) }
            }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: backend.notesFolder !== ""
                  ? qsTr("Each saved note is also written to %1/WriteupPulse with the summary, takeaways, and terms as links. A file you edit there is yours: the app won't overwrite it.").arg(backend.notesFolder)
                  : qsTr("Notes stay on this computer. To keep them in Obsidian or another Markdown app, or in Notion, set it up in Settings (P).")
            color: Style.faint
            font.family: Style.sans
            font.pixelSize: 11
        }
    }
}
