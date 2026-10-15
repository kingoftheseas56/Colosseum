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
    function focusGrid() { grid.keyboardMode = true; grid.forceActiveFocus(Qt.OtherFocusReason) }
    function resetSelection() {
        grid.currentIndex = entries.length ? 0 : -1
        grid.contentY = 0
        focusGrid()
    }
    function activateSelection() {
        var entry = entries[grid.currentIndex]
        if (entry === undefined) return
        grid.itemRequested(entry)
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
                    onEntered: { grid.currentIndex = cell.index }
                    onTriggered: { grid.currentIndex = cell.index; grid.itemRequested(cell.modelData) }
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
                    onResumeRequested: { grid.currentIndex = cell.index; grid.itemRequested(cell.modelData) }
                    onDetailRequested: { grid.currentIndex = cell.index; grid.itemRequested(cell.modelData) }
                    onRemoveRequested: controller.accountStore.dismissContinue(cell.modelData.id)
                }
            }
        }
    }
}
