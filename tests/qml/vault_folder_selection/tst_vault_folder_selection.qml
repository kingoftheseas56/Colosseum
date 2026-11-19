import QtQuick
import QtTest
import "../../../qml" as App

TestCase {
    id: suite
    name: "VaultFolderSelection"
    width: 800
    height: 620
    visible: true
    when: windowShown

    QtObject {
        id: library
        readonly property string root: "/fixture"
        readonly property string comics: root + "/Comics"
        property var selection: ({})
        property bool comicIncluded: true
        property bool rejectToggle: false
        property int confirmations: 0
        property int cancellations: 0

        function show(directory, personal) {
            const folder = {path: comics, name: "Comics", directory: true,
                            included: comicIncluded, inherited: false}
            const file = {path: comics + "/Story.cbz", name: "Story.cbz", directory: false,
                          included: true, inherited: false}
            const rows = personal ? (comicIncluded ? [] : [folder]) : (directory === root ? [folder] : [file])
            selection = {root: root, directory: directory, personalView: personal,
                         canSave: true, entries: rows}
        }

        function browseFolderSelection(path) { show(path, false) }
        function folderSelectionUp() { show(root, false) }
        function showPersonalFolderChoices(personal) { show(selection.directory, personal) }
        function toggleFolderSelectionEntry(path, isDirectory) {
            if (rejectToggle || path !== comics || !isDirectory)
                return
            comicIncluded = !comicIncluded
            show(selection.directory, selection.personalView)
        }
        function confirmFolderSelection() { confirmations++; selection = ({}) }
        function cancelFolderSelection() { cancellations++; selection = ({}) }
    }

    App.VaultFolderSelection {
        id: picker
        anchors.centerIn: parent
        width: 568
        height: 500
        visible: !!library.selection.root
        selection: library.selection
        library: library
    }

    function init() {
        library.comicIncluded = true
        library.rejectToggle = false
        library.confirmations = 0
        library.cancellations = 0
        library.show(library.root, false)
        tryCompare(picker, "opened", true)
    }

    function list() { return findChild(picker.contentItem, "vaultFolderSelectionEntries") }
    function firstRow() {
        tryVerify(() => list().itemAtIndex(0) !== null)
        return list().itemAtIndex(0)
    }

    function test_browsePersonalRestoreAndConfirm() {
        const disclosure = findChild(firstRow(), "vaultBrowseFolderEntry")
        mouseClick(disclosure)
        tryCompare(library.selection, "directory", library.comics)
        compare(findChild(firstRow(), "vaultFolderIncludeEntry").text, "Story.cbz")
        mouseClick(findChild(picker.contentItem, "vaultFolderSelectionUp"))

        const check = findChild(firstRow(), "vaultFolderIncludeEntry")
        compare(check.checked, true)
        library.rejectToggle = true
        mouseClick(check)
        compare(check.checked, true)
        library.rejectToggle = false
        mouseClick(check)
        tryCompare(check, "checked", false)
        verify(!findChild(firstRow(), "vaultBrowseFolderEntry").visible)

        mouseClick(findChild(picker.contentItem, "vaultFolderPersonalChoices"))
        verify(library.selection.personalView)
        const restore = findChild(firstRow(), "vaultFolderIncludeEntry")
        restore.forceActiveFocus()
        keyClick(Qt.Key_Space)
        tryCompare(list(), "count", 0)
        mouseClick(findChild(picker.contentItem, "vaultFolderBrowseFiles"))
        verify(!library.selection.personalView)
        compare(findChild(firstRow(), "vaultFolderIncludeEntry").checked, true)
        mouseClick(findChild(picker.contentItem, "vaultConfirmFolderSelection"))
        compare(library.confirmations, 1)
        tryCompare(picker, "opened", false)
    }

    function test_escapeCancelsSelection() {
        findChild(firstRow(), "vaultFolderIncludeEntry").forceActiveFocus()
        keyClick(Qt.Key_Escape)
        compare(library.cancellations, 1)
        tryCompare(picker, "opened", false)
    }
}
