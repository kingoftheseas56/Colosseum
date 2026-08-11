import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum

TestCase {
    name: "VaultIdentifyDialog"
    when: windowShown

    Window { id: testWindow; width: 720; height: 520; visible: true }
    Component { id: dialogComp; Colosseum.VaultIdentifyDialog {} }
    property var dialog: null
    SignalSpy { id: confirmSpy; signalName: "confirmRequested" }

    function init() {
        dialog = dialogComp.createObject(testWindow, { groupKey: "series-1", titleText: "Cowboy Bebop" })
        verify(dialog !== null)
        confirmSpy.target = dialog
        dialog.open()
        wait(40)
    }
    function cleanup() {
        confirmSpy.target = null
        if (dialog) dialog.destroy()
        dialog = null
    }

    function test_seeded_dialog_is_honest_and_confirmable() {
        verify(dialog.visible)
        verify(findText(dialog.contentItem, "Identify this Vault folder") !== null)
        verify(findChild(dialog.contentItem, "vaultIdentifyConfirm") !== null)
        mouseClick(findChild(dialog.contentItem, "vaultIdentifyConfirm"))
        compare(confirmSpy.count, 1)
        compare(confirmSpy.signalArguments[0][0], "series-1")
    }

    function findText(root, wanted) {
        if (!root) return null
        if (root.text === wanted) return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; i++) {
            var found = findText(kids[i], wanted)
            if (found) return found
        }
        return null
    }
    function findChild(root, wanted) {
        if (!root) return null
        if (root.objectName === wanted) return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; i++) {
            var found = findChild(kids[i], wanted)
            if (found) return found
        }
        return null
    }
}
