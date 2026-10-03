import QtQuick
import QtTest
import "../../qml" as App

Item {
    width: 1200; height: 900
    App.SagaUniversePage { id: saga }
    SignalSpy { id: books; target: saga; signalName: "bookRequested" }
    SignalSpy { id: films; target: saga; signalName: "watchRequested" }
    TestCase {
        name: "SagaTemplate"
        when: windowShown
        function init() {
            saga.forceActiveFocus()
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
            const rail = findChild(saga, "sagaBooksFocus")
            rail.forceActiveFocus()
            keyClick(Qt.Key_Right)
            keyClick(Qt.Key_Return)
            compare(books.count, 1)
            compare(books.signalArguments[0][0].id, "book-2")
        }
        function test_adaptationsAndWatch() {
            const button = findChild(saga, "sagaAdaptationsButton")
            mouseClick(button, button.width / 2, button.height / 2)
            compare(films.count, 1)
            compare(films.signalArguments[0][0].id, "film-1")
            films.clear()
            findChild(saga, "sagaAdaptationsFocus").forceActiveFocus()
            keyClick(Qt.Key_Return)
            compare(films.count, 1)
            compare(films.signalArguments[0][0].id, "film-1")
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
            const next = findChild(saga, "sagaBooksNext")
            const previous = findChild(saga, "sagaBooksPrevious")
            tryVerify(function() { return next.visible })
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
            findChild(saga, "sagaBooksFocus").forceActiveFocus()
            keyClick(Qt.Key_Return)
            findChild(saga, "sagaAdaptationsFocus").forceActiveFocus()
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
