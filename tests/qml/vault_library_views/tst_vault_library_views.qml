import QtQuick
import QtTest
import "../../../qml" as App

TestCase {
    id: testCase
    name: "VaultLibraryViews"
    width: 944; height: 616; visible: true
    when: windowShown
    FontLoader { source: "../../../assets/fonts/Fraunces-Regular.ttf" }
    property var rows: [
        {title: "A title", kind: "book", progressFraction: 0.4, progressText: "Chapter 4 · 40%", year: 2020},
        {title: "B title", kind: "book", completed: true, year: 2023}
    ]
    Component { id: homeComponent; App.VaultHomeView { width: 944; height: 616 } }
    Component { id: billboardComponent; App.VaultBillboard {} }
    Component { id: carouselComponent; App.FeaturedCarousel {} }
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
    function cleanup() { spy.target=null; spy.signalName="activated"; spy.clear() }
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
    function test_billboardCopyStaysInsetWithoutDuplicateFallback_data() {
        return [{tag:"actual-desktop",width:942,height:308},
                {tag:"desktop-wide",width:1414,height:336},
                {tag:"narrow-desktop",width:560,height:308},
                {tag:"long-title",width:942,height:308,title:"A much longer library title that wraps onto two lines"}]
    }
    function test_billboardCopyStaysInsetWithoutDuplicateFallback(data) {
        const billboard=createTemporaryObject(billboardComponent,testCase,
            {width:data.width,height:data.height,rows:[{title:data.title || "Barry Lyndon",kind:"movie",year:1975,
                runtimeText:"3h 5m",blurb:"A real synopsis.",genres:["Drama","Adventure"],imdbRating:8.1,
                progressFraction:0.4,progressText:"50 min left"}]})
        verify(billboard !== null)
        wait(40)
        const carousel=findChild(billboard,"vaultFeaturedCarousel")
        verify(carousel !== null)
        const copy=findChild(billboard,"featuredCopy_0")
        verify(copy !== null)
        verify(copy.x >= 20 && copy.x + copy.width <= billboard.width - 20)
        verify(copy.y >= 20 && copy.y + copy.height <= billboard.height - 20,
               "Copy must fit inside the shared hero: y=" + copy.y + ", height=" + copy.height)
        const title=findChild(billboard,"featuredTitle_0")
        verify(title !== null && title.visible)
        compare(title.text,data.title || "Barry Lyndon")
        compare(findChild(billboard,"featuredMetadata_0").text,"1975  ·  3h 5m  ·  Drama, Adventure  ·  IMDb 8.1")
        const actions=findChild(billboard,"featuredActions_0")
        const point=actions.mapToItem(billboard,0,0)
        verify(point.x >= 20 && point.x + actions.width <= billboard.width - 20)
        verify(point.y > title.mapToItem(billboard,0,0).y + title.height)
        verify(point.y + actions.height < billboard.height - 20)
        compare(visibleTextCount(billboard,data.title || "Barry Lyndon"),1)
    }
    function visibleTextCount(item, text) {
        let count=item.visible && item.text === text ? 1 : 0
        for (const child of item.children || []) count += visibleTextCount(child,text)
        return count
    }
    function test_billboardLogoAndMissingFallback() {
        const billboard=createTemporaryObject(billboardComponent,testCase,
            {width:944,height:308,rows:[{title:"Barry Lyndon",kind:"movie",
                logo:Qt.resolvedUrl("../../../assets/icons/colosseum-wordmark.svg")} ]})
        const logo=findChild(billboard,"featuredLogo_0")
        const title=findChild(billboard,"featuredTitle_0")
        tryCompare(logo,"status",Image.Ready)
        verify(logo.visible); verify(!title.visible)
        compare(visibleTextCount(billboard,"Barry Lyndon"),0)
        billboard.rows=[{title:"Barry Lyndon",kind:"movie",logo:"file:///missing-vault-logo.png"}]
        tryCompare(findChild(billboard,"featuredLogo_0"),"status",Image.Error)
        verify(findChild(billboard,"featuredTitle_0").visible)
        compare(visibleTextCount(billboard,"Barry Lyndon"),1)
    }
    function test_billboardSharedNavigationAndRowActions() {
        const mediaRows=[{id:"movie",title:"A film",kind:"movie"},
            {id:"book",title:"A book",kind:"book"},
            {id:"comic",title:"A comic",kind:"comic",progressFraction:2,progressText:"Almost done"}]
        const billboard=createTemporaryObject(billboardComponent,testCase,{width:944,height:308,rows:mediaRows})
        const carousel=findChild(billboard,"vaultFeaturedCarousel")
        const view=findChild(billboard,"vaultFeaturedCarouselView")
        wait(30)
        compare(findChild(billboard,"featuredPrimaryLabel_0").text,"Watch")
        compare(findChild(billboard,"featuredPrimaryLabel_1").text,"Read")
        compare(findChild(billboard,"featuredPrimaryLabel_2").text,"Resume")
        const fill=findChild(billboard,"featuredProgressFill_2")
        compare(fill.width,fill.parent.width)
        billboard.forceActiveFocus()
        tryCompare(view,"activeFocus",true)
        compare(findChild(billboard,"vaultBillboardAdvance").running,false)
        keyClick(Qt.Key_Right)
        compare(billboard.currentIndex,1); compare(carousel.index,1)
        spy.target=billboard; spy.signalName="activated"; spy.clear()
        keyClick(Qt.Key_Return)
        compare(spy.count,1); compare(spy.signalArguments[0][0].id,"book")
        spy.signalName="detailsRequested"; spy.clear()
        wait(350) // SwipeView's slide transition must finish before pointer hit-testing.
        mouseClick(findChild(billboard,"featuredSecondaryAction_1"))
        compare(spy.count,1); compare(spy.signalArguments[0][0].id,"book")
        spy.signalName="moveDown"; spy.clear()
        keyClick(Qt.Key_Down)
        compare(spy.count,1)
        carousel.index=2; compare(billboard.currentIndex,2)
        wait(350)
        spy.signalName="activated"; spy.clear()
        mouseClick(findChild(billboard,"featuredPrimaryAction_2"))
        compare(spy.count,1); compare(spy.signalArguments[0][0].id,"comic")
        carousel.index=0
        wait(350)
        spy.clear()
        mouseClick(findChild(billboard,"featuredPrimaryAction_0"))
        compare(spy.count,1); compare(spy.signalArguments[0][0].id,"movie")
        billboard.rows=[mediaRows[0]]
        tryCompare(billboard,"currentIndex",0)
        spy.target=null; spy.clear()
    }
    function test_sharedCarouselDefaultsStayUnchanged() {
        const carousel=createTemporaryObject(carouselComponent,testCase,
            {width:944,height:308,slides:[{title:"Existing world title",blurb:"A description."}]})
        wait(30)
        compare(findChild(carousel,"featuredCopy_0").x,42)
        compare(findChild(carousel,"featuredLogo_0").visible,false)
        compare(findChild(carousel,"featuredMetadata_0").visible,false)
        compare(findChild(carousel,"featuredProgress_0").visible,false)
        compare(findChild(carousel,"featuredTitle_0").visible,true)
        compare(findChild(carousel,"featuredTitle_0").font.pixelSize,42)
        compare(findChild(carousel,"featuredPrimaryLabel_0").text,"Read")
        compare(findChild(carousel,"featuredSecondaryAction_0").accessibleName,"Details")
    }
    function test_billboardAdvancesAndPausesForHover() {
        const billboard=createTemporaryObject(billboardComponent,testCase,{width:944,height:308,rows:rows})
        const timer=findChild(billboard,"vaultBillboardAdvance")
        compare(timer.interval,9000)
        testCase.forceActiveFocus()
        mouseMove(testCase,900,580)
        tryCompare(timer,"running",true)
        timer.interval=60
        tryCompare(billboard,"currentIndex",1)
        mouseMove(billboard,500,150)
        tryCompare(timer,"running",false)
        const index=billboard.currentIndex
        wait(150)
        compare(billboard.currentIndex,index)
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
