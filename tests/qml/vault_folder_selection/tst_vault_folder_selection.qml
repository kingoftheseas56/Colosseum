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
        readonly property string root: "d:/windows back up latest"
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
                         canSave: true, canGoUp: directory !== root, entries: rows,
                         includedCount: rows.filter(row => row.included).length,
                         excludedCount: comicIncluded ? 0 : 1,
                         location: {volume: "Seagate Backup Plus Drive (D:)",
                           segments: directory === root || personal ? ["windows back up latest"] : ["windows back up latest", "Comics"]}}
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
        picker.width = 568
        library.comicIncluded = true
        library.rejectToggle = false
        library.confirmations = 0
        library.cancellations = 0
        library.show(library.root, false)
        tryCompare(picker, "visible", true)
    }

    function list() { return findChild(picker, "vaultFolderSelectionEntries") }
    function firstRow() {
        tryVerify(() => list().itemAtIndex(0) !== null)
        return list().itemAtIndex(0)
    }

    function test_browseExcludeRestoreAndConfirm() {
        compare(findChild(picker, "vaultFolderVolumeLabel").text, "Seagate Backup Plus Drive (D:)")
        compare(findChild(picker, "vaultFolderLocationLabel").text, "windows back up latest")
        const disclosure = findChild(firstRow(), "vaultBrowseFolderEntry")
        mouseClick(disclosure)
        tryCompare(library.selection, "directory", library.comics)
        compare(findChild(firstRow(), "vaultFolderIncludeEntry").text, "Story.cbz")
        verify(findChild(picker, "vaultFolderLocationLabel").text.endsWith("Comics"))
        mouseClick(findChild(picker, "vaultFolderSelectionUp"))

        const check = findChild(firstRow(), "vaultFolderIncludeEntry")
        compare(check.checked, true)
        library.rejectToggle = true
        mouseClick(check)
        compare(check.checked, true)
        library.rejectToggle = false
        mouseClick(check)
        tryCompare(check, "checked", false)
        verify(!findChild(firstRow(), "vaultBrowseFolderEntry").visible)

        compare(check.Accessible.description, "Excluded")
        const excludedTab = findChild(picker, "vaultFolderExcludedChoices")
        compare(excludedTab.text, "Excluded (1)")
        mouseClick(excludedTab)
        verify(library.selection.personalView)
        const restore = findChild(firstRow(), "vaultFolderIncludeEntry")
        restore.forceActiveFocus()
        keyClick(Qt.Key_Space)
        tryCompare(list(), "count", 0)
        mouseClick(findChild(picker, "vaultFolderBrowseFiles"))
        verify(!library.selection.personalView)
        compare(findChild(firstRow(), "vaultFolderIncludeEntry").checked, true)
        mouseClick(findChild(picker, "vaultConfirmFolderSelection"))
        compare(library.confirmations, 1)
        tryCompare(picker, "visible", false)
    }

    function test_escapeCancelsSelection() {
        findChild(firstRow(), "vaultFolderIncludeEntry").forceActiveFocus()
        keyClick(Qt.Key_Escape)
        compare(library.cancellations, 1)
        tryCompare(picker, "visible", false)
    }

    function test_paginatedLayout_data() {
        return [{tag: "wide", width: 742}, {tag: "compact", width: 460}]
    }

    function test_paginatedLayout(data) {
        picker.width = data.width
        const rows = Array.from({length: 96}, (_, i) => ({
            path: library.root + "/File " + i + ".cbz",
            name: "File " + i + ".cbz",
            directory: false,
            included: true,
            inherited: false
        }))
        library.selection = Object.assign({}, library.selection, {
            entries: rows, includedCount: rows.length, page: 2, previous: true, more: true
        })
        const viewport = list()
        const row = firstRow()
        tryCompare(viewport, "count", rows.length)
        const previous = findChild(picker, "vaultFolderSelectionPreviousPage")
        tryVerify(() => previous.mapToItem(picker, 0, 0).y
                     - viewport.mapToItem(picker, 0, viewport.height).y >= 12, 1000,
                  "The list and pagination should have a visible gap")

        const close = findChild(picker, "vaultCloseFolderSelection")
        const header = findChild(picker, "vaultFolderSelectionTitle").parent
        const closeCenter = close.mapToItem(header, close.width / 2, close.height / 2)
        fuzzyCompare(closeCenter.y, header.height / 2, 1)
        const icon = close.contentItem.children[0].children[0]
        tryCompare(icon, "status", Image.Ready)
        verify(icon.width >= 24)

        const names = ["vaultCloseFolderSelection", "vaultFolderBrowseFiles",
            "vaultFolderExcludedChoices", "vaultFolderSelectionPreviousPage",
            "vaultFolderSelectionNextPage", "vaultCancelFolderSelection",
            "vaultConfirmFolderSelection"]
        for (const name of names) {
            const button = findChild(picker, name)
            const content = button.contentItem.children[0]
            const center = content.mapToItem(button, content.width / 2, content.height / 2)
            fuzzyCompare(center.x, button.width / 2, 1)
            fuzzyCompare(center.y, button.height / 2, 1)
        }

        const firstTop = row.mapToItem(viewport, 0, 0).y
        fuzzyCompare(firstTop, 0, 1)
        verify(viewport.height >= row.height)
        compare(viewport.height % row.height, 0)

        const count = findChild(picker, "vaultFolderSelectionCount")
        const note = findChild(picker, "vaultFolderSelectionFooterNote")
        if (picker.compact) {
            verify(!count.visible)
            verify(!note.visible)
        } else {
            const status = findChild(row, "vaultFolderEntryStatus")
            fuzzyCompare(count.mapToItem(picker, count.width, 0).x,
                         status.mapToItem(picker, status.width, 0).x, 1)
            const cancel = findChild(picker, "vaultCancelFolderSelection")
            fuzzyCompare(note.mapToItem(cancel, 0, note.height / 2).y, cancel.height / 2, 1)
        }

        viewport.positionViewAtEnd()
        tryVerify(() => viewport.itemAtIndex(rows.length - 1) !== null)
        const last = viewport.itemAtIndex(rows.length - 1)
        fuzzyCompare(last.mapToItem(viewport, 0, last.height).y, viewport.height, 1)
        mouseClick(close)
        compare(library.cancellations, 1)
    }
}
