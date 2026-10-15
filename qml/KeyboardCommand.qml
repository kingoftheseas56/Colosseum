// KeyboardCommand — the non-visual semantic command behind pointer, keyboard,
// accessibility, and guide presentation.
import QtQuick

QtObject {
    id: command

    property string semanticId: ""
    property string label: ""
    property string category: ""
    property string scope: ""
    property var sequences: []
    property bool enabled: true
    property string icon: ""
    property string whenFocused: ""
    property string notes: ""

    signal triggered(var source)
    signal invocationRejected(string reason)
    signal aboutToDestroy()

    function invoke(source) {
        if (!command.enabled) {
            command.invocationRejected("disabled")
            return false
        }
        if (command.semanticId.trim().length === 0) {
            command.invocationRejected("missing-semantic-id")
            return false
        }
        command.triggered(source === undefined ? "" : source)
        return true
    }

    Component.onDestruction: command.aboutToDestroy()
}
