import QtQuick
import QtTest
import QtQuick.Window
import "../../qml" as Colosseum

TestCase {
    id: test
    name: "VaultIdentityDialogs"
    property var pendingOwner: null
    property int cancellations: 0
    when: windowShown

    width:720; height:520
    Item { id: opener; objectName:"identityOpener"; focus:true }
    property var dialog

    Component {
        id: dialogComponent
        Colosseum.VaultIdentityCeremonyDialog {
            onCancelRequested: {test.pendingOwner=null;test.cancellations++;close();}
        }
    }

    SignalSpy {
        id: choiceSpy
        target: dialog
        signalName: "choiceMade"
    }

    function initTestCase() {
        dialog = dialogComponent.createObject(test)
        verify(dialog !== null)
    }

    function cleanup() {
        if (dialog) dialog.close()
        choiceSpy.clear();pendingOwner=null;cancellations=0
    }

    function openPending(type) {
        pendingOwner={relationship:"pending",type:type};opener.forceActiveFocus();
        dialog.openCeremony(pendingOwner);tryCompare(dialog,"opened",true);wait(0);
    }
    function test_escape_cancels_pending_owner_and_restores_focus_data() {
        return [{tag:"changed",type:"changed-content"},{tag:"copy",type:"likely-copy"}];
    }
    function test_escape_cancels_pending_owner_and_restores_focus(data) {
        openPending(data.type);keyClick(Qt.Key_Escape);tryCompare(dialog,"visible",false);
        compare(pendingOwner,null);compare(cancellations,1);compare(choiceSpy.count,0);
        tryCompare(opener,"activeFocus",true);
        openPending(data.type);verify(dialog.visible);verify(pendingOwner!==null);
    }
    function test_pointer_cancel_clears_owner_without_decision() {
        openPending("changed-content");var button=findChild(dialog.contentItem,"vaultCancelIdentity");verify(button!==null);
        mouseClick(button,button.width/2,button.height/2);tryCompare(dialog,"visible",false);
        compare(pendingOwner,null);compare(cancellations,1);compare(choiceSpy.count,0);tryCompare(opener,"activeFocus",true);
    }
    function test_keyboard_tab_cancel_clears_owner_without_decision() {
        openPending("likely-copy");keyClick(Qt.Key_Tab);keyClick(Qt.Key_Tab);keyClick(Qt.Key_Return);
        tryCompare(dialog,"visible",false);compare(pendingOwner,null);compare(cancellations,1);compare(choiceSpy.count,0);
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
