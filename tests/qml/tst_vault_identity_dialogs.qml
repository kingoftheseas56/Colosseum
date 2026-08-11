import QtQuick
import QtTest
import QtQuick.Window
import "../../qml" as Colosseum

TestCase {
    name: "VaultIdentityDialogs"
    when: windowShown

    Window { id: testWindow; width: 720; height: 520; visible: true }
    property var dialog

    Component {
        id: dialogComponent
        Colosseum.VaultIdentityCeremonyDialog {}
    }

    SignalSpy {
        id: choiceSpy
        target: dialog
        signalName: "choiceMade"
    }

    function initTestCase() {
        dialog = dialogComponent.createObject(testWindow)
        verify(dialog !== null)
    }

    function cleanup() {
        if (dialog) dialog.close()
        choiceSpy.clear()
    }

    function test_changed_content_dialog_renders_and_emits() {
        dialog.ceremony = {
            type: "changed-content",
            relationship: "changed|old|path",
            oldPath: "D:/lib/known.cbz",
            newPath: "D:/lib/known.cbz"
        }
        dialog.open()
        verify(dialog.visible)
        verify(dialog.sameMediaButton.visible)
        verify(dialog.newMediaButton.visible)
        dialog.choose("same-media")
        compare(choiceSpy.count, 1)
        compare(choiceSpy.signalArguments[0][0], "changed|old|path")
        compare(choiceSpy.signalArguments[0][1], "same-media")
    }

    function test_likely_copy_dialog_renders_and_emits() {
        dialog.ceremony = {
            type: "likely-copy",
            relationship: "copy|old|path",
            oldPath: "D:/lib/original.cbz",
            newPath: "D:/other/copy.cbz"
        }
        dialog.open()
        verify(dialog.useExistingStateButton.visible)
        verify(dialog.separateCopyButton.visible)
        dialog.choose("separate-copy")
        compare(choiceSpy.count, 1)
        compare(choiceSpy.signalArguments[0][1], "separate-copy")
    }
}
