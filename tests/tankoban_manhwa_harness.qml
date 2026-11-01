import QtQuick
import "../qml" as UI

Window {
    id: root
    width: 390
    height: 760
    visible: true
    property bool androidHost: true
    property var failures: []
    property var opened: null
    function check(value, message) { if (!value) failures.push(message) }
    function find(node, name) {
        if (node.objectName === name) return node
        var children = node.children || []
        for (var i = 0; i < children.length; i++) {
            var hit = find(children[i], name)
            if (hit) return hit
        }
        return null
    }
    QtObject {
        id: mal
        property bool available: true
        signal readyChanged()
        function ready() { return available }
        function discoverFilters(axis) { return axis === "genre" ? [{value:"Action",count:1}] : [] }
        function discoverPageFiltered(catalog, filters, explicitContent, offset, limit) {
            var rows = available ? [{mal_id:121496,title:"Solo Leveling",type:"Manhwa",explicit:false}] : []
            return {items:rows,nextOffset:offset+rows.length,exhausted:true,freshness:"bundled"}
        }
    }
    UI.TankobanDiscoverPage {
        id: page
        anchors.fill: parent
        malCatalog: mal
        _xhrFactory: function() { return {open:function(){},send:function(){}} }
        onMangaSeriesRequested: item => root.opened = item
    }
    Window {
        id: desktopWindow
        visible: true
        width: 390; height: 760
        property bool androidHost: false
        UI.TankobanDiscoverPage {
            id: desktopPage
            anchors.fill: parent
            malCatalog: mal
            _xhrFactory: function() { return {open:function(){},send:function(){}} }
        }
    }
    Timer {
        interval: 250; running: true
        onTriggered: {
            try {
            var shell = page._shellForTest
            check(shell.currentType === "manga", "default remains Manga")
            shell.setFilter("Publication Type", "manhua")
            shell.selectType("manhwa")
            shell.setFilter("Genres", "action")
            shell.itemOpenRequested(shell.items[0])
            check(opened && opened.type === "manga" && opened.id === "121496", "Manhwa uses the existing Manga route and MAL ID")
            shell.selectType("comics")
            shell.selectType("manhwa")
            check(shell.filters.Genres === "action", "Manhwa session filters survive switching")
            shell.selectType("manga")
            check(shell.filters["Publication Type"] === "manhua", "Manga publication filter has independent state")
            shell.clearFilter("Publication Type")
            shell.selectType("manhwa")
            var last = find(page, "tankobanDiscoverType_comics")
            var category = find(page, "tankobanDiscoverCategoryPicker")
            check(last && category, "third lens and category controls exist")
            if (last && category) {
                var tabAt = last.mapToItem(page, 0, 0), categoryAt = category.mapToItem(page, 0, 0)
                check(tabAt.x + last.width + 4 <= categoryAt.x || tabAt.y + last.height + 4 <= categoryAt.y,
                      "narrow type lenses do not overlap the category picker")
            }
            var desktopLast = find(desktopPage, "tankobanDiscoverType_comics")
            var desktopCategory = find(desktopPage, "tankobanDiscoverCategoryPicker")
            if (desktopLast && desktopCategory) {
                var desktopAt = desktopLast.mapToItem(desktopPage, 0, 0)
                var shelfAt = desktopCategory.parent.mapToItem(desktopPage, 0, 0)
                check(desktopAt.y + desktopLast.height + 4 <= shelfAt.y,
                      "narrow desktop lenses clear the entire category masthead including its kicker")
            } else check(false, "narrow desktop controls exist")
            mal.available = false
            shell.reloadCurrent()
            check(shell.items.length === 0 && shell.exhausted, "cold Manhwa initially settles empty")
            shell.selectType("comics")
            mal.available = true
            mal.readyChanged()
            shell.selectType("manhwa")
            check(shell.items.length === 1 && shell.filters.Genres === "action", "readiness while another lens is current refreshes saved Manhwa and preserves its filters")
            page.active = false
            mal.available = false
            shell.clearFilters()
            shell.items = []
            shell.exhausted = true
            mal.available = true
            mal.readyChanged()
            page.active = true
            check(shell.items.length === 1, "cold Manhwa catalogue wakes after readiness while hidden")
            } catch (error) { failures.push(String(error)) }
            for (var i = 0; i < failures.length; i++) console.warn("FAIL: " + failures[i])
            console.warn(failures.length ? "TANKOBAN_MANHWA_FAIL" : "TANKOBAN_MANHWA_OK")
            Qt.exit(failures.length)
        }
    }
    Timer { interval: 10000; running: true; onTriggered: { console.warn("FAIL: Manhwa harness timed out"); Qt.exit(1) } }
}
