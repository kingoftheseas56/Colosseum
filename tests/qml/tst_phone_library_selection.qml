import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App

TestCase {
    id: testCase
    name: "PhoneLibrarySelection"
    when: windowShown
    Window {
        id: window
        width: 354; height: 792; visible: true
        property bool androidHost: true
        property var phoneLibrarySelectionOwner: null
        Item { id: page; anchors.fill: parent }
    }
    Component {
        id: stateComponent
        App.LibrarySelectionState {}
    }
    Component {
        id: hostComponent
        App.LibrarySelectionHost { owner: page }
    }
    Component { id: theatreComponent; App.LibraryPage { anchors.fill: parent } }
    Component { id: biblioComponent; App.BiblioLibraryPage { anchors.fill: parent } }
    Component { id: tankobanComponent; App.TankobanLibraryTab { anchors.fill: parent } }
    SignalSpy { id: removal; signalName: "removeRequested" }
    function row(id) { return {entry: {id:id, title:"Book " + id}} }
    function named(item, name) {
        if (item.objectName === name) return item
        for (const child of item.children) {
            const found = named(child, name)
            if (found) return found
        }
        return null
    }
    function selectMenu(item) {
        if (item["label"] === "Select" && item.visible) return item
        for (const child of item.children) {
            const found = selectMenu(child)
            if (found) return found
        }
        return null
    }
    function test_real_library_select_menu_and_card_toggle_data() {
        return [
            {tag:"theatre", component:theatreComponent, grid:"theatreLibraryGrid", dots:"theatreLibraryCardMenu_"},
            {tag:"biblio", component:biblioComponent, grid:"biblioLibraryGrid", dots:"biblioLibraryCardMenu_"},
            {tag:"tankoban", component:tankobanComponent, grid:"tankobanLibraryGrid", dots:"tankobanLibraryCardMenu_"}
        ]
    }
    function test_real_library_select_menu_and_card_toggle(data) {
        window.width = 354; window.height = 792; window.requestActivate()
        const rows = [row("a"),row("b"),row("c")]
        for (const row of rows) { row.state="unwatched"; row.progress=0; row.entry.type="movie"; row.canResume=false; row.downloaded=false }
        const library = createTemporaryObject(data.component, page, {allRows:rows,visibleRows:rows})
        verify(library !== null)
        wait(30)
        const dots = named(library,data.dots + "a")
        verify(dots !== null)
        mouseClick(dots,dots.width/2,dots.height/2)
        wait(25)
        const action = selectMenu(library)
        verify(action !== null)
        mouseClick(action,action.width/2,24)
        compare(library.librarySelection.count,1)
        const grid = named(library,data.grid)
        mouseClick(grid.itemAtIndex(1),grid.itemAtIndex(1).width/2,50)
        compare(library.librarySelection.count,2)
        verify(library.librarySelection.contains(rows[0]) && library.librarySelection.contains(rows[1]))
        const invoker = window.phoneLibrarySelectionOwner
        verify(invoker !== null)
        const received=[]
        library.removeSelectionEntry = function(entry) { received.push(entry.id); return true }
        invoker.confirmationOpen = true
        mouseClick(named(window.contentItem,"libraryRemoveConfirm"),30,24)
        compare(received.join(","),"a,b")
        verify(!library.librarySelection.active)
        compare(window.phoneLibrarySelectionOwner,null)
    }
    function test_failed_canonical_removal_stays_selected() {
        const state = createTemporaryObject(stateComponent,testCase,{rows:[row("a"),row("b")]})
        const host = createTemporaryObject(hostComponent,page,{selection:state,
            removeEntry:function(entry) { return entry.id !== "b" }})
        state.selectAll()
        host.applyRemove()
        compare(state.count,1)
        verify(state.contains(row("b")))
        verify(host.confirmationOpen && host.removalError.length > 0)
    }
    function test_identity_survives_sort_refresh_and_filter() {
        const a = row("a"), b = row("b"), c = row("__proto__")
        const state = createTemporaryObject(stateComponent, testCase, {rows:[a,b,c]})
        state.begin(b)
        state.toggle(c)
        state.rows = [row("__proto__"), row("b"), row("a")]
        compare(state.count, 2)
        compare(state.selectedRows[0].entry.id, "__proto__")
        compare(state.selectedRows[1].entry.id, "b")
        state.rows = [a,c]
        compare(state.count, 1)
        verify(state.contains(c))
        state.invert()
        compare(state.count, 1)
        verify(state.contains(a))
        state.selectAll()
        compare(state.count, 2)
        state.rows = []
        verify(!state.active)
    }
    function test_confirmation_back_and_owner_route() {
        window.requestActivate()
        const state = createTemporaryObject(stateComponent, testCase, {rows:[row("a"),row("b")]})
        const host = createTemporaryObject(hostComponent, page, {selection:state})
        removal.target = host; removal.clear()
        state.begin(state.rows[0])
        compare(window.phoneLibrarySelectionOwner, host)
        verify(host.reservedHeight >= 128)
        const button = named(window.contentItem, "libraryRemoveSelection")
        mouseClick(button, button.width / 2, 24)
        verify(host.confirmationOpen)
        verify(host.handleBack())
        verify(!host.confirmationOpen && state.active)
        compare(removal.count, 0)
        verify(host.handleBack())
        verify(!state.active)
        compare(window.phoneLibrarySelectionOwner, null)
        state.selectAll()
        host.confirmationOpen = true
        // Simulate the canonical owner rebuilding the page after the first removal.
        host.removeRequested.connect(function(entry) {
            state.rows = state.rows.filter(row => row.entry.id !== entry.id)
        })
        mouseClick(named(window.contentItem, "libraryRemoveConfirm"), 30, 24)
        compare(removal.count, 2)
        compare(removal.signalArguments[0][0].id, "a")
        compare(removal.signalArguments[1][0].id, "b")
        verify(!state.active && !host.confirmationOpen)
    }
    function test_rotation_and_navigation() {
        const state = createTemporaryObject(stateComponent, testCase, {rows:[row("a"),row("b")]})
        const host = createTemporaryObject(hostComponent, page, {selection:state})
        state.begin(state.rows[0])
        window.width = 792; window.height = 354
        wait(10)
        compare(state.count, 1)
        const bar = named(window.contentItem, "librarySelectionBar")
        verify(bar.y >= 0 && bar.y + bar.height <= window.height)
        verify(host.reservedHeight <= 80, "Landscape selection must leave room for the library")
        const cancel = named(window.contentItem, "librarySelectionCancel")
        const all = named(window.contentItem, "librarySelectAll")
        const cancelPoint = cancel.mapToItem(bar, 0, 0)
        const allPoint = all.mapToItem(bar, 0, 0)
        compare(cancelPoint.y, allPoint.y, "Landscape actions should share a single thumb row")
        verify(cancelPoint.x + cancel.width <= allPoint.x)
        host.pageActive = false
        verify(!state.active)
        compare(window.phoneLibrarySelectionOwner, null)
        window.width = 354; window.height = 792
    }
}
