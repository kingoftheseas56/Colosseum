import QtQuick
import QtTest
import "../../../qml" as App

TestCase {
    id: testCase
    name: "VaultLibraryViews"
    width: 944; height: 616; visible: true
    when: windowShown
    property var rows: [
        {title: "A title", kind: "book", progressFraction: 0.4, progressText: "Chapter 4 · 40%", year: 2020},
        {title: "B title", kind: "book", completed: true, year: 2023}
    ]
    Component { id: homeComponent; App.VaultHomeView { width: 944; height: 616 } }
    Component { id: kindComponent; App.VaultKindView { width: 944; height: 616 } }
    Component { id: serverComponent; App.VaultServerView { width: 944; height: 616 } }
    Component { id: cardComponent; App.VaultContinueCard {} }
    Component { id: posterComponent; App.VaultLibraryPosterCard {} }
    SignalSpy { id: spy; signalName: "activated" }
    function test_homeEmptyAndPopulated() {
        const home = createTemporaryObject(homeComponent, testCase)
        verify(home !== null)
        compare(home.heroRows.length, 0)
        home.heroRows = rows
        home.continueRows = [rows[0]]
        home.kindRows = {book: rows}
        wait(100)
        verify(home.contentHeight > home.height)
        home.focusShelf(-1)
        spy.target = home
        spy.signalName = "resumeRequested"
        keyClick(Qt.Key_Return)
        compare(spy.count, 1)
        compare(spy.signalArguments[0][0].title, "A title")
        spy.clear()
        spy.signalName = "activated"
        // Down skips absent movie/show shelves, and horizontal navigation stays on books.
        keyClick(Qt.Key_Down)
        keyClick(Qt.Key_Right)
        keyClick(Qt.Key_Return)
        compare(spy.count, 1)
        compare(spy.signalArguments[0][0].title, "B title")
        spy.clear()
    }
    function test_billboardResumeAndDetailsStayDistinct() {
        const home=createTemporaryObject(homeComponent,testCase,{heroRows:rows})
        const billboard=findChild(home,"vaultHomeBillboard")
        spy.target=home; spy.signalName="resumeRequested"
        billboard.activated(rows[0]); compare(spy.count,1); spy.clear()
        spy.signalName="activated"
        billboard.activated(rows[1]); compare(spy.count,1); spy.clear()
        spy.signalName="detailsRequested"
        billboard.detailsRequested(rows[0]); compare(spy.count,1); spy.clear()
    }
    function test_kindFiltersAndSort() {
        const view = createTemporaryObject(kindComponent, testCase, {rows: rows, kind: "book", title: "Books"})
        compare(view.displayedRows.length, 2)
        view.filterMode = "In progress"
        compare(view.displayedRows.length, 1)
        compare(view.displayedRows[0].title, "A title")
        view.filterMode = "Completed"
        compare(view.displayedRows[0].title, "B title")
        view.filterMode = "All"
        view.sortMode = "Year"
        compare(view.displayedRows[0].year, 2023)
    }
    function test_serverPreservesGroups() {
        const view = createTemporaryObject(serverComponent, testCase, {rows: rows, groups: [{title: "Family books", items: rows}], server: {name: "Home", offline: true}})
        compare(view.libraries.length, 1)
        compare(view.libraries[0].title, "Family books")
        view.groups = []
        compare(view.libraries[0].title, "Library")
    }
    function test_offlineCopyDistinguishesServerAndDrive() {
        const card=createTemporaryObject(posterComponent,testCase,{row:{title:"Offline title",kind:"movie",offline:true,serverId:"home"}})
        const subtitle=findChild(card,"vaultPosterSubtitle")
        compare(subtitle.text,"Server unavailable")
        card.row={title:"Offline title",kind:"movie",offline:true}
        compare(subtitle.text,"Drive not connected")
    }
    function test_continueReadingShape() {
        const card = createTemporaryObject(cardComponent, testCase, {row: rows[0]})
        compare(card.reading, true)
        compare(card.width, 244)
        compare(card.height, 188)
    }
}
