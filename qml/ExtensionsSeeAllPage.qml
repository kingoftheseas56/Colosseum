// ExtensionsSeeAllPage — a Store row's "See all ›", and the Store's search. Back, then either the
// row's title and line or a search box, then the add-ons as the same Store cards, most starred
// first. Floats over the Store the way TheatreSeeAllPage floats over Theatre, so the Store keeps
// its scroll position underneath.
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import "ExtensionsStoreApi.js" as StoreApi

Item {
    id: page

    property var row: null                   // { title, sub, all } from ExtensionsStoreApi.rows
    property bool searching: false           // search mode: `pool` filtered by the typed query
    property var pool: []                    // every add-on the Store shows, most starred first
    property string query: ""
    property var installedFn: function(addon) { return false }
    property var disabledFn: function(addon) { return false }
    property var busy: ({})
    property int revision: 0
    property real bottomInset: 24
    readonly property bool phoneLayout: {
        const w = page.Window.window
        return !!(w && w.androidHost && w.width < 840)
    }
    readonly property real sideMargin: phoneLayout ? 16 : theme.margin
    signal addonActivated(var addon)
    signal addonConfigured(var addon)
    signal addonRemoved(var addon)
    signal catalogueRequested(var addon)
    signal backRequested()

    function activateCard(index, card) {
        if (index >= 0 && card) card.focusActions()
    }

    readonly property var items: page.searching ? StoreApi.search(page.pool, page.query)
        : page.row ? (page.row.all || page.row.items || []) : []
    visible: page.row !== null || page.searching
    onSearchingChanged: if (page.searching) { page.query = ""; box.text = "" }

    Theme { id: theme }

    function takeKeyboardFocus() { if (page.searching) box.forceActiveFocus(); else if (page.phoneLayout) phoneList.forceActiveFocus(); else grid.forceActiveFocus() }

    // Transparent: the Store hides its board while this is open, so the wallpaper shows through.
    MouseArea { anchors.fill: parent }

    BackAction {
        id: back
        anchors.left: parent.left; anchors.leftMargin: page.sideMargin
        anchors.top: parent.top; anchors.topMargin: 44
        label: qsTr("Extensions")
        labelSize: 15
        onTriggered: page.backRequested()
    }

    Column {
        id: head
        anchors.left: parent.left; anchors.leftMargin: page.sideMargin
        anchors.right: parent.right; anchors.rightMargin: page.sideMargin
        anchors.top: back.bottom; anchors.topMargin: 22
        spacing: 6
        // Search box: the serif headline itself becomes the field.
        TextField {
            id: box
            objectName: "extensionsSearchField"
            visible: page.searching
            width: page.phoneLayout ? parent.width : Math.min(parent.width, 900)
            placeholderText: "Search " + page.pool.length + " add-ons"
            color: theme.ink
            placeholderTextColor: Qt.rgba(1, 1, 1, 0.32)
            font.family: theme.display; font.pixelSize: page.phoneLayout ? 26 : 40
            leftPadding: 0
            background: Rectangle {
                y: parent.height - 1; width: parent.width; height: 1
                color: box.activeFocus ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.6) : Qt.rgba(1, 1, 1, 0.18)
            }
            onTextChanged: page.query = text
            Keys.onEscapePressed: page.backRequested()
            Keys.onDownPressed: if (page.items.length) (page.phoneLayout ? phoneList : grid).forceActiveFocus()
            Keys.onReturnPressed: if (page.items.length) (page.phoneLayout ? phoneList : grid).forceActiveFocus()
        }
        Text {
            visible: !page.searching
            width: page.phoneLayout ? parent.width : implicitWidth
            wrapMode: page.phoneLayout ? Text.WordWrap : Text.NoWrap
            text: page.row ? page.row.title : ""
            color: theme.ink
            font.family: theme.display; font.pixelSize: page.phoneLayout ? 28 : 40; font.weight: Font.DemiBold
        }
    }

    GridView {
        id: grid
        visible: !page.phoneLayout
        objectName: "extensionsSeeAllGrid"
        anchors.top: head.bottom; anchors.topMargin: 30
        anchors.bottom: parent.bottom
        anchors.bottomMargin: page.bottomInset
        anchors.left: parent.left; anchors.leftMargin: page.sideMargin
        anchors.right: parent.right; anchors.rightMargin: page.phoneLayout ? page.sideMargin : theme.margin - 22
        readonly property int columns: Math.max(1, Math.floor((width + 14) / (theme.extensionCardWidth + 14)))
        readonly property real cardWidth: Math.max(0, cellWidth - 14)
        cellWidth: Math.floor(width / columns)
        cellHeight: 190
        clip: true
        focus: page.visible
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        model: page.items
        ScrollBar.vertical: HouseScrollBar { flick: grid; glassStyle: true }
        Keys.onReturnPressed: page.activateCard(grid.currentIndex, grid.currentItem)
        Keys.onUpPressed: (event) => {
            if (page.searching && grid.currentIndex < grid.columns) box.forceActiveFocus()
            else event.accepted = false
        }
        Keys.onEnterPressed: page.activateCard(grid.currentIndex, grid.currentItem)
        Keys.onEscapePressed: page.backRequested()
        delegate: ResultCard {
            required property var modelData
            required property int index
            width: grid.cardWidth
            addon: modelData
            selected: grid.activeFocus && grid.currentIndex === index
        }
    }
    ScrollGlide { flick: grid }
    ListView {
        id: phoneList
        objectName: "extensionsSeeAllList"
        visible: page.phoneLayout
        anchors.top: head.bottom; anchors.topMargin: 22
        anchors.bottom: parent.bottom
        anchors.bottomMargin: page.bottomInset
        anchors.left: parent.left; anchors.leftMargin: page.sideMargin
        anchors.right: parent.right; anchors.rightMargin: page.sideMargin
        spacing: 18
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: page.phoneLayout ? page.items : []
        focus: page.visible && page.phoneLayout
        ScrollBar.vertical: HouseScrollBar { flick: phoneList; glassStyle: true }
        Keys.onReturnPressed: page.activateCard(currentIndex, currentItem)
        Keys.onEnterPressed: page.activateCard(currentIndex, currentItem)
        Keys.onEscapePressed: page.backRequested()
        delegate: ResultCard {
            required property var modelData
            required property int index
            width: phoneList.width
            phoneLayout: true
            addon: modelData
            selected: phoneList.activeFocus && phoneList.currentIndex === index
        }
    }
    ScrollGlide { flick: phoneList }


    component ResultCard: ExtensionsStoreCard {
        essentialsLayout: true
        installed: { page.revision; return page.installedFn(addon) }
        disabled: { page.revision; return page.disabledFn(addon) }
        busy: page.busy[addon.slug] === true
        onActivated: page.addonActivated(addon)
        onConfigureRequested: page.addonConfigured(addon)
        onRemoveRequested: page.addonRemoved(addon)
        onBrowseRequested: page.catalogueRequested(addon)
    }

    Text {
        visible: page.searching && page.query.trim().length > 0 && page.items.length === 0
        anchors.left: parent.left; anchors.leftMargin: page.sideMargin
        anchors.top: head.bottom; anchors.topMargin: 40
        width: page.phoneLayout ? parent.width - 2 * page.sideMargin : implicitWidth
        wrapMode: page.phoneLayout ? Text.WordWrap : Text.NoWrap
        text: qsTr("No matching add-ons")
        color: theme.inkDim
        font.family: theme.ui; font.pixelSize: 15
    }
}
