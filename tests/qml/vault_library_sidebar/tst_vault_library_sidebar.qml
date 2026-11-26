import QtQuick
import QtQuick.Controls
import QtCore
import QtTest
import "../../../qml" as App

TestCase {
    id: suite
    name: "VaultLibrarySidebar"
    width: 1920; height: 1080; visible: true
    when: windowShown
    property var page
    property var model
    property int interceptedDockClicks: 0
    property var preferences
    readonly property string captureTag: String(Date.now())
    Component { id: settingsComponent; QtObject { property bool railExpanded: true } }
    Component {
        id: diskSettingsComponent
        Settings { category: "vaultBrowseV1"; property bool railExpanded: true }
    }
    Component { id: adapterComponent; App.VaultLibraryAdapter { library: null; progressStore: null; homeServers: null } }
    Component { id: pageComponent; App.VaultLibraryPage {} }
    SignalSpy { id: addFolder; signalName: "addFolderRequested" }
    SignalSpy { id: rescan; signalName: "rescanRequested" }
    SignalSpy { id: navClick; signalName: "clicked" }
    Rectangle {
        id: dock
        x: 57; y: suite.page ? suite.page.height - 80 : 0
        width: 190; height: 64; z: 100; visible: false
        MouseArea { anchors.fill: parent; onClicked: suite.interceptedDockClicks++ }
    }

    function init() {
        dock.visible=false; interceptedDockClicks=0
        preferences=createTemporaryObject(settingsComponent,suite)
        model=createTemporaryObject(adapterComponent,suite)
        model.books=[{id:"book",title:"A book",kind:"book",copies:[],items:[],path:"/fixture/book.epub"}]
        model.allItems=model.books
        model.roots=[{path:"/fixture",name:"Stories",available:true}]
        model.unmatchedItems=[{key:"unknown",title:"Unknown",kind:"movie",copies:[],items:[]}]
        page=createTemporaryObject(pageComponent,suite,{width:1280,height:720,adapter:model,sidebarSettings:preferences})
        verify(page !== null)
        addFolder.target=page
        wait(260)
    }
    function cleanup() { navClick.target=null; addFolder.target=null; addFolder.clear(); rescan.target=null; rescan.clear(); page.destroy(); wait(0) }
    function sidebar() { return findChild(page,"vaultLibrarySidebar") }
    function pane() { return findChild(page,"vaultLibraryContent") }
    function toggle() { return findChild(page,"vaultSidebarToggle") }
    function checkInside(item, container) {
        const p=item.mapToItem(container,0,0)
        verify(p.x >= 0 && p.y >= 0 && p.x+item.width <= container.width+0.1 && p.y+item.height <= container.height+0.1,
               item.objectName+" must fit inside its container: "+p.x+","+p.y+" "+item.width+"x"+item.height)
    }
    function test_defaultExpandedToggleAndRestoration() {
        compare(sidebar().expanded,true); compare(sidebar().width,248)
        toggle().forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Return)
        compare(preferences.railExpanded,false)
        tryCompare(sidebar(),"width",62)
        verify(toggle().activeFocus)
        compare(toggle().Accessible.name,"Expand library sidebar")
        page.destroy(); wait(0)
        page=createTemporaryObject(pageComponent,suite,{width:1280,height:720,adapter:model,sidebarSettings:preferences})
        wait(260)
        compare(sidebar().expanded,false); compare(sidebar().width,62)
        toggle().forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Return)
        tryCompare(sidebar(),"width",248)
        compare(preferences.railExpanded,true)
        page.destroy(); wait(0)
        page=createTemporaryObject(pageComponent,suite,{width:1280,height:720,adapter:model,sidebarSettings:preferences})
        wait(260)
        compare(sidebar().expanded,true)
    }
    function test_selectionFocusAndContentSurviveCollapse() {
        const books=[]
        for(let i=0;i<40;++i) books.push({id:"book"+i,title:"Book "+i,kind:"book",copies:[],items:[]})
        model.books=books; model.allItems=books
        page.showView("books",""); wait(260)
        const content=pane().item
        const viewModel=content.rows
        const grid=content.children.find(child => child.contentY !== undefined)
        verify(grid !== undefined)
        grid.contentY=200
        const savedScroll=grid.contentY
        const bookNav=findChild(page,"vaultNav_books")
        bookNav.forceActiveFocus(Qt.TabFocusReason)
        preferences.railExpanded=false
        tryCompare(sidebar(),"width",62)
        compare(page.currentView,"books"); verify(bookNav.active); verify(bookNav.activeFocus)
        compare(pane().item,content); compare(content.rows,viewModel); compare(grid.contentY,savedScroll)
        compare(findChild(page,"vaultNav_booksLabel").visible,false)
        compare(findChild(page,"vaultNav_booksCount").visible,false)
        compare(bookNav.Accessible.name,"Books, 40")
        compare(bookNav.ToolTip.visible,false)
        preferences.railExpanded=true
        tryCompare(sidebar(),"width",248)
        compare(pane().item,content); verify(bookNav.activeFocus); compare(grid.contentY,savedScroll)
        compare(findChild(page,"vaultNav_booksLabel").visible,true)
        compare(findChild(page,"vaultNav_booksCount").visible,true)
    }
    function test_sameAddFolderAndSourcesRoutesBothStates() {
        for (const expanded of [true,false]) {
            preferences.railExpanded=expanded
            tryCompare(sidebar(),"width",expanded ? 248 : 62)
            const add=findChild(page,"vaultSidebarAddFolder")
            checkInside(add,sidebar())
            mouseClick(add)
            compare(addFolder.count,expanded ? 1 : 2)
            compare(page.sourcesOpen,false)
            const sources=findChild(page,"vaultsourcesTile")
            sources.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Return)
            compare(page.sourcesOpen,true)
            page.handleBack(); tryCompare(findChild(page,"vaultStorageSheet"),"visible",false)
            compare(page.sourcesOpen,false)
            rescan.target=sidebar(); rescan.clear()
            mouseClick(findChild(page,"vaultrescanTile"))
            compare(rescan.count,1)
            page.handleBack(); tryCompare(findChild(page,"vaultStorageSheet"),"visible",false)
            mouseClick(findChild(page,"vaulthiddenTile"))
            compare(page.currentView,"hidden")
        }
    }
    function test_fixedProviderGroupBounds_data() {
        return [{tag:"1280-native",width:1280,height:720},
            {tag:"1920-native",width:1920,height:1080},
            {tag:"1280-at-150-percent",width:853,height:480},
            {tag:"1920-at-150-percent",width:1280,height:720}]
    }
    function test_collapsedHoverDoesNotExpand() {
        preferences.railExpanded=false
        tryCompare(sidebar(),"width",62)
        toggle().forceActiveFocus(Qt.TabFocusReason)
        const home=findChild(page,"vaultNav_home")
        mouseMove(home)
        compare(home.ToolTip.visible,false)
        compare(home.Accessible.name,"Home")
        compare(sidebar().expanded,false)
        verify(toggle().activeFocus)
    }
    function test_fixedProviderGroupBounds(data) {
        page.width=data.width; page.height=data.height
        model.servers=[]
        page.currentView="server"; page.currentServer="plex"
        wait(30)
        for (const expanded of [true,false]) {
            preferences.railExpanded=expanded
            tryCompare(sidebar(),"width",expanded ? 248 : 62)
            checkInside(sidebar(),page)
            checkInside(toggle(),sidebar())
            for(const name of ["vaultSidebarAddFolder","vaultsourcesTile","vaulthiddenTile","vaultrescanTile"]) checkInside(findChild(page,name),sidebar())
            verify(pane().x >= sidebar().x+sidebar().width+31)
            verify(pane().x+pane().width <= page.width-40+0.1)
            const navigation=findChild(page,"vaultSidebarNavigation")
            const navItems=findChild(page,"vaultSidebarNavigationItems")
            verify(!!navigation, "Navigation area exists")
            verify(!!navItems, "Navigation items exist")
            compare(navigation.interactive, navigation.contentHeight > navigation.height)
            if (data.height >= 672) verify(!navigation.interactive, "Normal and short windows need no scrolling")
            for(const key of ["home","movies","shows","books","comics","match"]) {
                const item=findChild(page,"vaultNav_"+key)
                verify(!!item, "Missing navigation row: "+key)
                verify(item.visible, "Navigation row is visible: "+key)
                item.forceActiveFocus(Qt.TabFocusReason)
                checkInside(item,navigation)
            }
            const providerGroup=findChild(page,"vaultSidebarProviderGroup")
            verify(!!providerGroup, "Object exists")
            checkInside(providerGroup,navItems)
            for(const key of ["jellyfin","emby","plex"]) {
                const row=findChild(page,"vaultNav_provider_"+key)
                verify(!!row, "Object exists")
                row.forceActiveFocus(Qt.TabFocusReason)
                checkInside(row,providerGroup)
                checkInside(row,navigation)
                verify(row.visible)
                compare(row.height,sidebar().providerRowHeight)
                compare(row.statusKind,"unlinked")
                compare(row.statusText,"Not connected")
                const logo=findChild(page,"vaultNav_provider_"+key+"Icon")
                verify(!!logo, "Object exists")
                const logoPoint=logo.mapToItem(providerGroup,0,0)
                verify(logoPoint.x>=0 && logoPoint.y>=0 && logoPoint.x+logo.width<=providerGroup.width+0.1
                       && logoPoint.y+logo.height<=providerGroup.height+0.1)
                const stateGlyph=findChild(page,"vaultNav_provider_"+key+"StatusGlyph")
                verify(!!stateGlyph, "Object exists")
                tryCompare(stateGlyph,"status",Image.Ready)
            }
            const providerRows=["jellyfin","emby","plex"].map(key=>findChild(page,"vaultNav_provider_"+key))
            verify(providerRows[0].y < providerRows[1].y && providerRows[1].y < providerRows[2].y,
                   "Provider icons stay vertically stacked")
            if(data.tag==="1280-at-150-percent" || data.tag==="1280-native" || data.tag==="1920-native") {
                const view=(expanded?"expanded":"collapsed")
                const capture=grabImage(page)
                compare(page.width,data.width)
                compare(page.height,data.height)
                verify(capture.width > 0 && capture.height > 0)
                const tempDir=StandardPaths.writableLocation(StandardPaths.TempLocation).toString().replace("file:///","")
                capture.save(tempDir+"/colosseum-vault-sidebar-"+captureTag+"-"+data.tag+"-"+view+".png")
            }
            const lastKind=findChild(page,"vaultNav_comics")
            lastKind.forceActiveFocus(Qt.TabFocusReason)
            wait(30)
            verify(lastKind.activeFocus)
            keyClick(Qt.Key_Down)
            verify(findChild(page,"vaultNav_match").activeFocus)
            keyClick(Qt.Key_Down)
            verify(findChild(page,"vaultNav_provider_jellyfin").activeFocus)
            keyClick(Qt.Key_Down)
            verify(findChild(page,"vaultNav_provider_emby").activeFocus)
            keyClick(Qt.Key_Down)
            const server=findChild(page,"vaultNav_provider_plex")
            verify(server.activeFocus)
            compare(server.Accessible.name,"Plex, Not connected")
            const providerIcon=findChild(page,"vaultNav_provider_plexIcon")
            compare(String(providerIcon.source),"../assets/icons/servers/plex.svg")
            tryCompare(providerIcon,"status",Image.Ready)
            if(!expanded) {
                compare(server.ToolTip.visible,false)
                compare(findChild(page,"vaultNav_provider_plexLabel").visible,false)
                compare(findChild(page,"vaultNav_provider_plexCount").visible,false)
            }
            keyClick(Qt.Key_Down)
            verify(findChild(page,"vaultSidebarAddFolder").activeFocus)
        }
    }
    function test_dockFixtureReproducesOriginalInterception() {
        dock.visible=true
        compare(page.desktopDockInset,0)
        mouseClick(findChild(page,"vaulthiddenTile"))
        compare(interceptedDockClicks,1,"The fixture detects a real dock interception without clearance")
        compare(page.currentView,"home")
    }
    function test_dockClearanceAndOverflow_data() {
        return [{tag:"normal",height:720,overflow:false},
                {tag:"short",height:672,overflow:false},
                {tag:"tall-full-footer",height:900,overflow:false},
                {tag:"overflow",height:420,overflow:true}]
    }
    function test_dockClearanceAndOverflow(data) {
        page.height=data.height
        const originalContentHeight=pane().height
        dock.visible=true
        page.desktopDockInset=Qt.binding(function() { return dock.visible ? 80 : 0 })
        wait(30)
        const navigation=findChild(page,"vaultSidebarNavigation")
        const footer=findChild(page,"vaultSidebarFooter")
        const header=findChild(page,"vaultSidebarHeader")
        const scrollBar=findChild(page,"vaultSidebarScrollBar")
        for (const expanded of [true,false]) {
            preferences.railExpanded=expanded
            tryCompare(sidebar(),"width",expanded ? 248 : 62)
            compare(pane().height,originalContentHeight)
            compare(sidebar().compactFooter,data.height < 900)
            verify(sidebar().y+sidebar().height <= dock.y-8+0.1,"Sidebar clears dock top")
            checkInside(footer,sidebar()); checkInside(header,sidebar())
            verify(navigation.y >= header.y+header.height,"Navigation clears header")
            verify(navigation.y+navigation.height <= footer.y,"Navigation clears fixed footer")
            compare(navigation.interactive,data.overflow)
            compare(scrollBar.visible,data.overflow)
            verify(navigation.clip,"Overflow is clipped between header and footer")
            for(const name of ["vaultSidebarAddFolder","vaultsourcesTile","vaulthiddenTile","vaultrescanTile"]) {
                const action=findChild(page,name)
                checkInside(action,footer)
                verify(action.height >= 28,"Footer hit area remains usable")
                const top=action.mapToItem(page,0,0).y
                verify(top+action.height <= dock.y,"Action stays above dock")
            }
            const keys=["home","movies","shows","books","comics","match","provider_jellyfin","provider_emby","provider_plex"]
            // Focus every row via the real arrow chain.
            findChild(page,"vaultSidebarToggle").forceActiveFocus(Qt.TabFocusReason)
            for(const key of keys) {
                keyClick(Qt.Key_Down)
                const row=findChild(page,"vaultNav_"+key)
                verify(row.activeFocus,"Arrow focus reaches "+key)
                checkInside(row,navigation)
            }
            verify(!data.overflow || navigation.contentY > 0,"Focus reveals lower provider rows")
            keyClick(Qt.Key_Down)
            verify(findChild(page,"vaultSidebarAddFolder").activeFocus)
            keyClick(Qt.Key_Up)
            verify(findChild(page,"vaultNav_provider_plex").activeFocus)
            checkInside(findChild(page,"vaultNav_provider_plex"),navigation)
            // Tab must reveal the same clipped rows, including the reverse trip.
            for(let i=0;i<keys.length-1;++i) keyClick(Qt.Key_Backtab)
            verify(findChild(page,"vaultNav_home").activeFocus)
            checkInside(findChild(page,"vaultNav_home"),navigation)
            for(let i=0;i<keys.length-1;++i) keyClick(Qt.Key_Tab)
            verify(findChild(page,"vaultNav_provider_plex").activeFocus)
            checkInside(findChild(page,"vaultNav_provider_plex"),navigation)
            addFolder.clear()
            mouseClick(findChild(page,"vaultSidebarAddFolder"))
            compare(addFolder.count,1)
            mouseClick(findChild(page,"vaulthiddenTile"))
            compare(page.currentView,"hidden")
            mouseClick(findChild(page,"vaultsourcesTile"))
            compare(page.sourcesOpen,true)
            page.handleBack(); tryCompare(findChild(page,"vaultStorageSheet"),"visible",false)
            rescan.target=sidebar(); rescan.clear()
            mouseClick(findChild(page,"vaultrescanTile"))
            compare(rescan.count,1)
            page.handleBack(); tryCompare(findChild(page,"vaultStorageSheet"),"visible",false)
            wait(0)
            for(const key of keys) {
                const row=findChild(page,"vaultNav_"+key)
                row.forceActiveFocus(Qt.TabFocusReason)
                checkInside(row,navigation)
                navClick.target=row; navClick.clear()
                mouseClick(row)
                compare(navClick.count,1,"Revealed row receives its own click: "+key)
                if(key.indexOf("provider_") === 0) {
                    compare(page.providerConnectionOpen,true)
                    tryCompare(findChild(page,"vaultProviderConnectionLoader"),"status",Loader.Ready)
                    page.handleBack()
                    tryCompare(page,"providerConnectionOpen",false)
                } else compare(page.currentView,key)
                wait(0)
            }
            navClick.target=null
            compare(interceptedDockClicks,0,"Sidebar clicks never reach the dock")
            dock.visible=false; wait(0)
            compare(page.desktopDockInset,0)
            compare(sidebar().y+sidebar().height,page.height-4)
            compare(pane().height,originalContentHeight)
            dock.visible=true; wait(0)
            compare(page.desktopDockInset,80)
            verify(sidebar().y+sidebar().height <= dock.y-8+0.1)
            const focusedProvider=findChild(page,"vaultNav_provider_plex")
            focusedProvider.forceActiveFocus(Qt.TabFocusReason)
            page.height=420; wait(0)
            verify(focusedProvider.activeFocus)
            checkInside(focusedProvider,navigation)
            page.height=data.height; wait(0)
            verify(focusedProvider.activeFocus)
            checkInside(focusedProvider,navigation)
            compare(pane().height,originalContentHeight)
        }
    }
    function test_emptyOfflineAndUnmatchedKeepUtilities() {
        model.books=[]; model.allItems=[]; model.roots=[]; model.unmatchedItems=[]
        const emptyStatus=findChild(page,"vaultLibrarySourcesStatus")
        const emptyStatusLabel=findChild(page,"vaultLibraryStatus")
        verify(!!emptyStatus, "Object exists")
        verify(!!emptyStatusLabel, "Object exists")
        compare(emptyStatus.Accessible.name,"Sources status: No sources connected")
        compare(emptyStatusLabel.visible,false)
        emptyStatus.forceActiveFocus(Qt.TabFocusReason)
        compare(emptyStatus.ToolTip.visible,false)
        for(const view of ["home","match","hidden"]) {
            page.showView(view,"")
            for(const expanded of [true,false]) {
                preferences.railExpanded=expanded
                tryCompare(sidebar(),"width",expanded ? 248 : 62)
                for(const name of ["vaultSidebarAddFolder","vaultsourcesTile","vaulthiddenTile","vaultrescanTile"]) checkInside(findChild(page,name),sidebar())
            }
        }
        model.servers=[{id:"away",name:"Offline family server",provider:"jellyfin",offline:true}]
        page.showView("server","away")
        preferences.railExpanded=false
        tryCompare(sidebar(),"width",62)
        const server=findChild(page,"vaultNav_provider_jellyfin")
        compare(server.Accessible.name,"Jellyfin, Server unavailable")
        compare(server.statusKind,"attention")
        verify(server.active)
    }
    function test_preferencePersistsThroughSettingsOwner() {
        const file=StandardPaths.writableLocation(StandardPaths.TempLocation)+"/colosseum-sidebar-test-"+Date.now()+".ini"
        let owner=createTemporaryObject(diskSettingsComponent,suite,{location:file})
        page.sidebarSettings=owner
        compare(sidebar().expanded,true)
        mouseClick(toggle())
        compare(owner.railExpanded,false)
        owner.sync()
        page.sidebarSettings=preferences
        owner.destroy(); wait(0)
        owner=createTemporaryObject(diskSettingsComponent,suite,{location:file})
        page.sidebarSettings=owner
        tryCompare(sidebar(),"expanded",false)
        mouseClick(toggle())
        compare(owner.railExpanded,true)
        owner.sync()
        page.sidebarSettings=preferences
        owner.destroy(); wait(0)
        owner=createTemporaryObject(diskSettingsComponent,suite,{location:file})
        compare(owner.railExpanded,true)
        page.sidebarSettings=owner
    }
}

