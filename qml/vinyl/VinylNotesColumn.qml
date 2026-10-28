import QtQuick
import ".."
import "VinylNotePicker.js" as Picker

Item {
    id: root
    objectName: "vinylNotesColumn"
    required property var pageData
    readonly property int shownLineIndex: pageData.activeNoteLine
    // Local supplied notes have no crowdsourced votes or reading-budget ranking.
    readonly property bool localNotes: !!pageData.playingEdition && pageData.playingEdition.origin === "local"
    readonly property bool autoMode: pageData.autoNotes && pageData.manualNoteLine < 0 && !localNotes
    readonly property var shownNotes: {
        var list = pageData.notes[shownLineIndex] || [];
        if (!autoMode)
            return list;
        var chosen = Picker.pick(list, pageData.noteWindow(shownLineIndex));
        return chosen ? [chosen] : [];
    }
    readonly property int noteCount: shownNotes.length
    Theme {
        id: theme
    }
    clip: true
    Column {
        width: parent.width
        spacing: 20
        y: root.autoMode ? Math.max(0, (root.height - height) / 2) : 0
        Text {
            visible: !root.autoMode
            text: "ANNOTATIONS · " + root.noteCount
            color: theme.inkDimmer
            font.family: theme.ui
            font.pixelSize: 11
            font.letterSpacing: 1.5
        }
        Repeater {
            model: root.shownNotes
            delegate: Column {
                required property var modelData
                required property int index
                width: parent.width
                spacing: 18
                Rectangle {
                    width: parent.width
                    height: 1
                    color: theme.edge
                    visible: parent.index > 0
                }
                Text {
                    width: parent.width
                    text: parent.modelData.text
                    color: theme.ink
                    font.family: root.autoMode ? theme.display : theme.ui
                    font.pixelSize: root.autoMode ? 24 : 15
                    wrapMode: Text.WordWrap
                    lineHeight: 1.45
                }
            }
        }
    }
}
