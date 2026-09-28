// ExtensionsSeeAllPage — a Store row's "See all ›", and the Store's search. Back, then either the
// row's title and line or a search box, then the add-ons as the same big cards, most starred
// first. Floats over the Store the way TheatreSeeAllPage floats over Theatre, so the Store keeps
// its scroll position underneath.
import QtQuick
import QtQuick.Controls

Item {
    id: page

    property var row: null                   // { title, sub, all } from ExtensionsStoreApi.rows
    property bool searching: false           // search mode: `pool` filtered by the typed query
    property var pool: []                    // every add-on the Store shows, most starred first
    property string query: ""
    property var installedFn: function(addon) { return false }
    property var busy: ({})
    property int revision: 0
    signal addonActivated(var addon)
    signal backRequested()

    readonly property var items: {
        if (!page.searching) return page.row ? (page.row.all || page.row.items || []) : []
        var words = page.query.toLowerCase().split(/\s+/).filter(function(w) { return w.length > 0 })
        if (!words.length) return []
        // Every word must appear somewhere; a hit in the name outranks a category, which
        // outranks the description. Stars break ties.
        var hits = []
        for (var j = 0; j < page.pool.length; j++) {
            var a = page.pool[j]
            var name = a.name.toLowerCase(), cats = a.categories.join(" "), desc = a.description.toLowerCase()
            var score = 0, all = true
            for (var i = 0; i < words.length && all; i++) {
                var w = words[i]
                if (name.indexOf(w) >= 0) score += 100
                else if (cats.indexOf(w) >= 0) score += 10
                else if (desc.indexOf(w) >= 0) score += 1
                else all = false
            }
            if (all) hits.push({ a: a, score: score })
        }
        hits.sort(function(x, y) { return (y.score - x.score) || (y.a.stars - x.a.stars) })
        return hits.map(function(h) { return h.a })
    }
    visible: page.row !== null || page.searching
    onSearchingChanged: if (page.searching) { page.query = ""; box.text = "" }

    Theme { id: theme }

    function takeKeyboardFocus() { if (page.searching) box.forceActiveFocus(); else grid.forceActiveFocus() }

    // Transparent: the Store hides its board while this is open, so the wallpaper shows through.
    MouseArea { anchors.fill: parent }

    BackAction {
        id: back
        anchors.left: parent.left; anchors.leftMargin: theme.margin
        anchors.top: parent.top; anchors.topMargin: 44
        label: "Store"
        labelSize: 15
        onTriggered: page.backRequested()
    }

    Column {
        id: head
        anchors.left: parent.left; anchors.leftMargin: theme.margin
        anchors.right: parent.right; anchors.rightMargin: theme.margin
        anchors.top: back.bottom; anchors.topMargin: 22
        spacing: 6
        // Search box: the serif headline itself becomes the field.
        TextField {
            id: box
            objectName: "extensionsSearchField"
            visible: page.searching
            width: Math.min(parent.width, 900)
            placeholderText: "Search " + page.pool.length + " add-ons"
            color: theme.ink
            placeholderTextColor: Qt.rgba(1, 1, 1, 0.32)
            font.family: theme.display; font.pixelSize: 40
            leftPadding: 0
            background: Rectangle {
                y: parent.height - 1; width: parent.width; height: 1
                color: box.activeFocus ? Qt.rgba(240 / 255, 196 / 255, 74 / 255, 0.6) : Qt.rgba(1, 1, 1, 0.18)
            }
            onTextChanged: page.query = text
            Keys.onDownPressed: if (page.items.length) grid.forceActiveFocus()
            Keys.onReturnPressed: if (page.items.length) grid.forceActiveFocus()
        }
        Text {
            visible: !page.searching
            text: page.row ? page.row.title : ""
            color: theme.ink
            font.family: theme.display; font.pixelSize: 40; font.weight: Font.DemiBold
        }
        Text {
            text: page.searching
                  ? (page.query.trim().length ? page.items.length + (page.items.length === 1 ? " add-on" : " add-ons")
                                                 + " match." : "Names, what they do, or a category: anime, subtitles, debrid, Hindi…")
                  : page.row ? (page.row.sub + "  " + page.items.length + " add-ons.") : ""
            color: theme.inkDimmer
            font.family: theme.ui; font.pixelSize: 15
        }
    }

    GridView {
        id: grid
        objectName: "extensionsSeeAllGrid"
        anchors.top: head.bottom; anchors.topMargin: 30
        anchors.bottom: parent.bottom
        anchors.left: parent.left; anchors.leftMargin: theme.margin
        anchors.right: parent.right; anchors.rightMargin: theme.margin - 22
        readonly property int cardWidth: width < 1100 ? 320 : 365
        cellWidth: Math.floor(width / Math.max(1, Math.floor(width / (cardWidth + 22))))
        cellHeight: Math.round(cardWidth * 220 / 365) + 79 + 34
        clip: true
        focus: page.visible
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        model: page.items
        ScrollBar.vertical: HouseScrollBar { flick: grid }
        Keys.onReturnPressed: page.addonActivated(page.items[grid.currentIndex])
        Keys.onUpPressed: (event) => {
            if (page.searching && grid.currentIndex < Math.floor(grid.width / grid.cellWidth)) box.forceActiveFocus()
            else event.accepted = false
        }
        Keys.onEnterPressed: page.addonActivated(page.items[grid.currentIndex])
        delegate: ExtensionsStoreCard {
            required property var modelData
            required property int index
            width: grid.cardWidth
            addon: modelData
            installed: { page.revision; return page.installedFn(modelData) }
            busy: page.busy[modelData.slug] === true
            selected: grid.activeFocus && grid.currentIndex === index
            onActivated: page.addonActivated(modelData)
        }
    }
    ScrollGlide { flick: grid }

    Text {
        visible: page.searching && page.query.trim().length > 0 && page.items.length === 0
        anchors.left: parent.left; anchors.leftMargin: theme.margin
        anchors.top: head.bottom; anchors.topMargin: 40
        text: "No add-on matches that. Try a site name, a language or a genre."
        color: theme.inkDim
        font.family: theme.ui; font.pixelSize: 15
    }
}
