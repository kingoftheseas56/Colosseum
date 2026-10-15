import QtQuick
import ".." as Colosseum

Item {
    id: root
    objectName: "feriaSeeAllView"
    required property var controller
    readonly property bool continuing: controller.seeAllKind === "continue"
    readonly property var entries: continuing ? controller.continueEntries() : controller.shelfItems(controller.seeAllShelf)
    readonly property bool posterCards: entries.some(function(id) {
        return !root.continuing && controller.shapeFor(controller.titleObj(id)) === "poster"
    })
    readonly property string sourceLabel: continuing ? ""
        : controller.seeAllShelf.chips ? controller.providerName(controller.chipSelection[controller.seeAllShelfId]) + " chart"
        : controller.seeAllShelf.src || ""
    property var pendingReturnSnapshot: null
    property int returnRestoreAttempts: 0
    property int returnRestoreStablePasses: 0
    property bool hoverSelectionEnabled: true
    function focusGrid() {
        if (!visible || controller.viewState !== "seeAll" || controller.navigationSuspended) return
        grid.keyboardMode = true
        grid.forceActiveFocus(Qt.OtherFocusReason)
    }
    function cancelRestoration() {
        pendingReturnSnapshot = null
        restoreReturnPosition.stop()
        armHoverSelection.stop()
        returnRestoreAttempts = 0
        returnRestoreStablePasses = 0
        hoverSelectionEnabled = true
    }
    function restoreReturnStep() {
        if (!pendingReturnSnapshot) { restoreReturnPosition.stop(); return }
        if (!visible || controller.viewState !== "seeAll" || controller.navigationSuspended) {
            cancelRestoration()
            return
        }
        var complete = applyReturn(pendingReturnSnapshot)
        returnRestoreAttempts++
        returnRestoreStablePasses = complete ? returnRestoreStablePasses + 1 : 0
        if (returnRestoreStablePasses >= 10 || returnRestoreAttempts >= 15) {
            pendingReturnSnapshot = null
            returnRestoreAttempts = 0
            returnRestoreStablePasses = 0
            restoreReturnPosition.stop()
            armHoverSelection.restart()
        }
    }
    FeriaRestorationInput {
        id: restorationInput
        enabled: root.visible && controller.viewState === "seeAll" && !controller.navigationSuspended
        onUserInput: controller.noteNavigationInput()
    }
    function returnSnapshot() {
        if (pendingReturnSnapshot)
            return { index:Number(pendingReturnSnapshot.index), y:Number(pendingReturnSnapshot.y || 0) }
        return { index:grid.currentIndex, y:grid.contentY }
    }
    function applyReturn(snapshot) {
        if (!snapshot) return true
        if (!visible || controller.viewState !== "seeAll" || controller.navigationSuspended) return false
        var requestedIndex = grid.count > 0
                ? Math.max(0, Math.min(grid.count - 1, Number(snapshot.index))) : -1
        focusGrid()
        grid.currentIndex = Math.max(-1, Math.min(grid.count - 1, requestedIndex))
        grid.forceLayout()
        var minY = grid.originY - grid.topMargin
        var maxY = Math.max(minY, grid.originY + grid.contentHeight - grid.height + grid.bottomMargin)
        var requestedY = Number(snapshot.y || 0)
        grid.contentY = Math.max(minY, Math.min(maxY, requestedY))
        return grid.currentIndex === requestedIndex
            && Math.abs(grid.contentY - Math.max(minY, Math.min(maxY, requestedY))) < 0.5
    }
    function restoreReturn(snapshot) {
        cancelRestoration()
        if (!visible || controller.viewState !== "seeAll" || controller.navigationSuspended) return
        if (!snapshot) {
            pendingReturnSnapshot = null
            focusGrid()
            return
        }
        pendingReturnSnapshot = { index:Number(snapshot.index), y:Number(snapshot.y || 0) }
        returnRestoreAttempts = 0
        returnRestoreStablePasses = 0
        hoverSelectionEnabled = false
        armHoverSelection.stop()
        restoreReturnPosition.restart()
    }
    function resetSelection() {
        pendingReturnSnapshot = null
        restoreReturnPosition.stop()
        armHoverSelection.stop()
        hoverSelectionEnabled = true
        grid.currentIndex = entries.length ? 0 : -1
        grid.contentY = 0
        focusGrid()
    }
    function activateSelection() {
        var entry = entries[grid.currentIndex]
        if (entry === undefined) return
        grid.itemRequested(entry)
    }
    Timer {
        id: restoreReturnPosition
        interval: 60
        repeat: true
        onTriggered: root.restoreReturnStep()
    }
    Timer {
        id: armHoverSelection
        interval: 400
        onTriggered: root.hoverSelectionEnabled = true
    }

    Rectangle { anchors.fill: parent; color: controller.night }
    Column {
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: controller.marginX; topMargin: 112 }
        spacing: 8
        Text {
            text: controller.seeAllTitle
            color: controller.ink
            font.family: controller.displayFont
            font.pixelSize: 30
            font.weight: Font.Medium
        }
        Text {
            text: (root.sourceLabel ? root.sourceLabel + " · " : "") + root.entries.length
                + (root.entries.length === 1 ? " title" : " titles")
            color: controller.slate
            font.family: controller.uiFont
            font.pixelSize: 14
            Accessible.role: Accessible.StatusBar
        }
    }
    Colosseum.CataloguePosterGrid {
        id: grid
        objectName: "feriaSeeAllGrid"
        anchors { left: parent.left; right: parent.right; top: parent.top; bottom: parent.bottom;
            leftMargin: controller.marginX; rightMargin: controller.marginX; topMargin: 200; bottomMargin: 24 }
        visualProfile: "gallery"
        focus: false
        Keys.forwardTo: [restorationInput]
        onDraggingChanged: if (dragging) controller.noteNavigationInput()
        items: root.entries
        emptyMessage: root.continuing ? "Nothing to continue in this tab." : "No titles in this row."
        cellHeight: controller.galleryMetrics.posterWidth * (root.continuing || root.posterCards ? controller.galleryMetrics.posterRatio : 1)
            + controller.galleryMetrics.hoverLift + controller.galleryMetrics.cardGap + (root.continuing ? 0
                : controller.galleryMetrics.hoverLift + 10 + controller.galleryMetrics.titleMinHeight + 24)
        keyNavigationWraps: false
        onCountChanged: if (currentIndex >= count) currentIndex = count - 1
        onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, GridView.Contain)
        onItemRequested: function(entry) {
            if (root.continuing) controller.resumeSession(entry)
            else controller.openTitle(entry)
        }
        delegate: Item {
            id: cell
            required property int index
            readonly property var modelData: grid.items[index]
            width: grid.cellWidth
            height: grid.cellHeight
            Loader {
                anchors.horizontalCenter: parent.horizontalCenter
                y: controller.galleryMetrics.hoverLift
                width: controller.galleryMetrics.posterWidth
                sourceComponent: root.continuing ? resumeCard : titleCard
            }
            Component {
                id: titleCard
                PorticoCombinedMediaCard {
                    readonly property var it: controller.titleObj(cell.modelData)
                    width: controller.galleryMetrics.posterWidth
                    unit: controller.unit
                    title: it ? it.t : ""
                    sub: it ? (it.k === "artist" ? (it.f[0] || it.by || "")
                        : it.k === "album" ? it.by || "" : (it.y ? it.y + "   " : "") + controller.kindLabel(it)) : ""
                    artSource: controller.artUrl(it, false)
                    shape: controller.shapeFor(it)
                    rankText: controller.seeAllShelf.rank ? String(cell.index + 1) : ""
                    selected: grid.activeFocus && grid.currentIndex === cell.index
                    toneA: controller.toneFor(cell.modelData)[0]
                    toneB: controller.toneFor(cell.modelData)[1]
                    displayFont: controller.displayFont
                    ink: controller.ink; mist: controller.mist; slate: controller.slate; gold: controller.gold
                    onEntered: { if (root.hoverSelectionEnabled) grid.currentIndex = cell.index }
                    onTriggered: { controller.noteNavigationInput(); grid.currentIndex = cell.index; grid.itemRequested(cell.modelData) }
                }
            }
            Component {
                id: resumeCard
                Colosseum.ContinueTile {
                    objectName: "feriaSeeAllContinue_" + cell.index
                    variant: "world"
                    entry: cell.modelData
                    collectionManaged: true
                    collectionSelected: grid.activeFocus && grid.currentIndex === cell.index
                    onResumeRequested: { controller.noteNavigationInput(); grid.currentIndex = cell.index; grid.itemRequested(cell.modelData) }
                    onDetailRequested: { controller.noteNavigationInput(); grid.currentIndex = cell.index; grid.itemRequested(cell.modelData) }
                    onRemoveRequested: controller.accountStore.dismissContinue(cell.modelData.id)
                }
            }
        }
    }
}
