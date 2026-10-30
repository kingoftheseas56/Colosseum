import QtQuick
import QtTest
import "../../qml" as App

TestCase {
    name: "CollectionGridTemplate"
    when: windowShown
    Window { id: host; width:800; height:1280; visible:true; property bool androidHost:true }
    Component { id: theatre; App.LibraryPage { anchors.fill:parent } }
    Component { id: biblio; App.BiblioLibraryPage { anchors.fill:parent } }
    Component { id: tankoban; App.TankobanLibraryTab { anchors.fill:parent } }

    function find(item, name) {
        if (item.objectName === name) return item
        const children = item.children || []
        for (let i=0; i<children.length; ++i) {
            const found = find(children[i], name)
            if (found) return found
        }
        return null
    }
    function test_tablet_has_touch_grid_and_selection_data() {
        const result = []
        for (const size of [[800,1280],[1280,800]])
            for (const data of [{factory:theatre,prefix:"theatre"},{factory:biblio,prefix:"biblio"},{factory:tankoban,prefix:"tankoban"}])
                result.push({tag:data.prefix+"-"+size[0],factory:data.factory,prefix:data.prefix,width:size[0],height:size[1]})
        return result
    }
    function test_tablet_has_touch_grid_and_selection(data) {
        host.width=data.width; host.height=data.height
        const page = createTemporaryObject(data.factory, host.contentItem)
        const rows = [{entry:{id:"tablet-item",title:"A tablet library item",cover:"",type:"movie"},state:"unwatched",progress:0,downloaded:false,isSeries:false}]
        page.allRows=rows; page.visibleRows=rows
        wait(50)
        compare(page.phoneLayout, data.width < 840)
        const grid = find(page, data.prefix + "LibraryGrid")
        verify(grid !== null)
        const dots = find(page, data.prefix + "LibraryCardMenu_tablet-item")
        verify(dots !== null)
        verify(dots.width >= 48 && dots.height >= 48)
        page.openMenu(rows[0], 780, 400, grid)
        const panel = find(page, data.prefix + "LibraryMenuPanel")
        verify(panel !== null)
        verify(panel.x + panel.width <= page.width)
        if (page.triggerMenuAction) page.triggerMenuAction("select")
        else page.triggerMenuIndex(1)
        verify(page.librarySelection.active)
    }
}
