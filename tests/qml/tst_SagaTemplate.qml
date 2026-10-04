import QtQuick
import QtTest
import "../../qml" as App

Item {
    width: 1200; height: 900
    App.SagaUniversePage { id: saga }
    // Bring a control into the upper half before a pointer test: a test window taller than
    // the screen delivers no pointer events to the part that hangs off it.
    function bringUp(item) {
        const page = saga.children.length ? findScroll(saga) : null
        page.contentY = Math.max(0, item.mapToItem(page.contentItem, 0, 0).y - 300)
    }
    function findScroll(item) {
        if (item.objectName === "sagaScroll") return item
        for (let i = 0; i < item.children.length; ++i) { const r = findScroll(item.children[i]); if (r) return r }
        return null
    }
    SignalSpy { id: books; target: saga; signalName: "bookRequested" }
    SignalSpy { id: films; target: saga; signalName: "watchRequested" }
    TestCase {
        name: "SagaTemplate"
        when: windowShown
        function init() {
            saga.forceActiveFocus()
            saga.previewProgress = null
            saga.selectedKey = ""
            saga.uni = { name: "Harry Potter", banner: "", metaline: "", books: [
                { title: "First book", id: "book-1", cover: "" },
                { title: "Second book", id: "book-2", cover: "" }
            ], films: [{ title: "First film", id: "film-1", cover: "" }], shows: [], comics: null }
            tryVerify(function() { return findChild(saga, "sagaScroll").contentHeight > 600 })
            books.clear(); films.clear()
            findChild(saga, "sagaScroll").contentY = 0
        }
        function test_firstBookAction() {
            const button = findChild(saga, "sagaReadButton")
            mouseClick(button, button.width / 2, button.height / 2)
            compare(books.count, 1)
            compare(books.signalArguments[0][0].id, "book-1")
        }
        function test_keyboardBookOrder() {
            const grid = findChild(saga, "sagaGridFocus")
            grid.forceActiveFocus()
            keyClick(Qt.Key_Right)
            keyClick(Qt.Key_Return)
            compare(books.count, 1)
            compare(books.signalArguments[0][0].id, "book-2")
        }
        // Up/Down move between the forms of one story: the book, then the film that adapts it.
        function test_keyboardSwapsForm() {
            const grid = findChild(saga, "sagaGridFocus")
            grid.forceActiveFocus()
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(films.count, 1)
            compare(films.signalArguments[0][0].id, "film-1")
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Return)
            compare(books.count, 1)
            compare(books.signalArguments[0][0].id, "book-1")
        }
        // The detail line follows the keyboard, and its button opens what it describes.
        function test_detailFollowsSelection() {
            const grid = findChild(saga, "sagaGridFocus")
            grid.forceActiveFocus()
            keyClick(Qt.Key_Right)
            compare(saga.shown.id, "book-2")
            const action = findChild(saga, "sagaDetailAction")
            bringUp(action)
            mouseClick(action, action.width / 2, action.height / 2)
            compare(books.count, 1)
            compare(books.signalArguments[0][0].id, "book-2")
        }
        // A returning visitor: Continue shows this universe's works and Read resumes, not restarts.
        function test_continueAndResume() {
            verify(!findChild(saga, "sagaContinue").visible)
            saga.previewProgress = [{ id: "book-2", kind: "book", title: "Second book", sub: "41%", progress: 0.41 }]
            tryVerify(function() { return findChild(saga, "sagaContinue").visible })
            compare(saga.continueItems.length, 1)
            const button = findChild(saga, "sagaReadButton")
            mouseClick(button, button.width / 2, button.height / 2)
            compare(books.signalArguments[0][0].id, "book-2")
            saga.previewProgress = null
        }
        function test_adaptationsAndWatch() {
            const button = findChild(saga, "sagaAdaptationsButton")
            mouseClick(button, button.width / 2, button.height / 2)
            compare(films.count, 1)
            compare(films.signalArguments[0][0].id, "film-1")
            films.clear()
        }
        function test_showOnlyWatchAction() {
            saga.uni = { name: "A Song of Ice and Fire", books: [], films: [],
                shows: [{ title: "Game of Thrones", id: "show-1", type: "series", cover: "" }],
                comics: null, banner: "", metaline: "" }
            const button = findChild(saga, "sagaAdaptationsButton")
            mouseClick(button, button.width / 2, button.height / 2)
            compare(films.count, 1)
            compare(films.signalArguments[0][0].id, "show-1")
        }
        function test_collectionChevrons() {
            const many = []
            for (let i = 0; i < 10; ++i) many.push({ title: "Book " + i, id: "book-" + i, cover: "" })
            saga.uni = { name: "Test saga", books: many, films: [], shows: [],
                         comics: null, banner: "", metaline: "" }
            const next = findChild(saga, "sagaGridNext")
            const previous = findChild(saga, "sagaGridPrevious")
            tryVerify(function() { return next.visible })
            bringUp(next)
            verify(!previous.visible)
            mouseClick(next, next.width / 2, next.height / 2)
            tryVerify(function() { return previous.visible })
            mouseClick(previous, previous.width / 2, previous.height / 2)
            tryVerify(function() { return !previous.visible })
        }
        function test_unresolvedWorkCannotOpen() {
            saga.uni = { name: "Dune", books: [{ title: "Pending book", resolved: false }],
                films: [{ title: "Future film", resolved: false, upcoming: true }], shows: [],
                comics: null, banner: "", metaline: "" }
            verify(!findChild(saga, "sagaReadButton").enabled)
            verify(!findChild(saga, "sagaAdaptationsButton").enabled)
            findChild(saga, "sagaGridFocus").forceActiveFocus()
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(books.count, 0)
            compare(films.count, 0)
            compare(saga.collections.upcoming.length, 1)
        }
        function test_emptyDisablesActions() {
            saga.uni = { name: "Dune", books: [], films: [], shows: [], comics: null,
                         banner: "", metaline: "" }
            verify(!findChild(saga, "sagaReadButton").enabled)
            verify(!findChild(saga, "sagaAdaptationsButton").enabled)
        }
    }
}
