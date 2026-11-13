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
    Component { id: storageComponent; App.VaultStorageSheet { width: 944; height: 616; backdrop: null } }
    Component {
        id: storageAdapterComponent
        QtObject {
            property var roots: []
            property bool busy: false
            property var savedPatterns: ["existing"]
            property int saveCount: 0
            function scanIgnore() { return savedPatterns }
            function setScanIgnore(patterns) { savedPatterns = patterns; saveCount++ }
        }
    }
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
    function test_serverEmptyGroupsKeepOrderedHeadingsAndDimState() {
        const groups = [{id:"empty-first",name:"Empty first",kind:"book",items:[]},
                        {id:"full",name:"Family books",kind:"book",items:rows},
                        {id:"empty-last",name:"Empty last",kind:"comic",items:[]}]
        const view = createTemporaryObject(serverComponent, testCase,
            {rows:rows, groups:groups, server:{name:"Home",enabled:true,state:"ready",dimmed:false}})
        wait(50)
        const first = findChild(view,"vaultServerLibrary_0")
        const second = findChild(view,"vaultServerLibrary_1")
        const last = findChild(view,"vaultServerLibrary_2")
        verify(first !== null && second !== null && last !== null)
        compare(first.title,"Empty first"); compare(second.title,"Family books"); compare(last.title,"Empty last")
        verify(first.height >= 40); verify(last.height >= 40)
        verify(second.y >= first.y + first.height)
        verify(last.y >= second.y + second.height)
        const action = findChild(second,"vaultPosterAction")
        verify(action !== null)
        compare(action.contextEnabled,false)
        spy.target = action; spy.signalName = "contextRequested"; spy.clear()
        action.requestContext(Qt.OtherFocusReason)
        compare(spy.count,0)
        const states = ["cached","pending","refreshing","ready","unavailable","needsReconnect","disabled"]
        const labels = ["Saved library · Refresh in Sync Center", "Updating library…", "Updating library…", "",
                        "Server unavailable · Reconnect in the Sync Center ›", "Sign-in required · Reconnect in the Sync Center ›", "Server disabled · Manage in the Sync Center ›"]
        for (let i = 0; i < states.length; ++i) {
            const dimmed = states[i] !== "ready"
            view.server = {name:"Home",enabled:states[i] !== "disabled",state:states[i],dimmed:dimmed}
            compare(view.dimmed,dimmed)
            compare(second.dimmed,dimmed)
            compare(action.parent.opacity,dimmed ? 0.48 : 1)
            compare(findChild(view,"vaultServerUnavailableBanner").visible,labels[i].length > 0)
            compare(findChild(view,"vaultServerStatusText").text,labels[i])
        }
        const localCard = createTemporaryObject(posterComponent,testCase,{row:rows[0]})
        const localAction = findChild(localCard,"vaultPosterAction")
        verify(localAction !== null); compare(localAction.contextEnabled,true)
        spy.target = localAction; spy.clear()
        localAction.requestContext(Qt.OtherFocusReason)
        compare(spy.count,1)
        spy.target = null; spy.signalName = "activated"; spy.clear()
        localCard.destroy()
    }
    function test_cancelIgnorePatternsReloadsSavedRulesBeforeNextSave() {
        const adapter = createTemporaryObject(storageAdapterComponent,testCase)
        const sheet = createTemporaryObject(storageComponent,testCase,{adapter:adapter})
        sheet.show(null); wait(260)
        const edit = findChild(sheet,"vaultEditIgnorePatterns")
        const cancel = findChild(sheet,"vaultCancelIgnorePatterns")
        const input = findChild(sheet,"vaultIgnorePatterns")
        mouseClick(edit); compare(sheet.editingIgnore,true)
        compare(input.text,"existing")
        input.text="cancelled-rule"
        mouseClick(cancel); compare(sheet.editingIgnore,false)
        compare(adapter.saveCount,0)
        mouseClick(edit); compare(sheet.editingIgnore,true)
        compare(input.text,"existing")
        input.text += "\naccepted-rule"
        mouseClick(edit)
        compare(adapter.saveCount,1)
        compare(adapter.savedPatterns.join("\n"),"existing\naccepted-rule")
        mouseClick(edit); compare(input.text,"existing\naccepted-rule")
        sheet.close()
    }
    function test_offlineCopyDistinguishesServerAndDrive() {
        const card=createTemporaryObject(posterComponent,testCase,{row:{title:"Offline title",kind:"movie",offline:true,serverId:"home"}})
        compare(findChild(card,"vaultPosterAction").contextEnabled,true)
        compare(card.opacity,0.48)
        const subtitle=findChild(card,"vaultPosterSubtitle")
        compare(subtitle.text,"Server unavailable")
        card.row={title:"Offline title",kind:"movie",offline:true}
        compare(subtitle.text,"Drive not connected")
        card.row={title:"Cached title",kind:"movie",dimmed:true,serverId:"home",state:"cached",subtitle:"Saved film"}
        compare(card.opacity,0.48)
        compare(subtitle.text,"Saved film")
    }
    function test_continueReadingShape() {
        const card = createTemporaryObject(cardComponent, testCase, {row: rows[0]})
        compare(card.reading, true)
        compare(card.width, 244)
        compare(card.height, 188)
    }
}
