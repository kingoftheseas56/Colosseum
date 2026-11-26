import QtQuick
import QtQuick.Controls
import QtTest
import "../../../qml" as App

TestCase {
    id: suite
    name: "HomeServerProviders"
    width: 1280; height: 900; visible: true
    when: windowShown
    FontLoader { source: "../../../assets/fonts/Fraunces-Regular.ttf" }
    property var service
    Component {
        id: serviceComponent
        QtObject {
            property var connections: []
            property var serverTabs: []
            property var libraries: ({})
            property var plexServers: []
            property bool available: true
            property bool busy: false
            property string status: ""
            property string plexCode: ""
            property string plexUrl: ""
            property bool httpConsentRequired: false
            property int profileGeneration: 4
            property string enabledId: ""
            property string removedId: ""
            property string refreshedId: ""
            property int cancels: 0
            property int connectCalls: 0
            property int plexStarts: 0
            signal changed()
            signal profileChanged()
            signal connectionAdded(string id)
            function serverLibraries(id) { return libraries[id] || [] }
            function isInsecureAddress(address) { return false }
            function cancelSignIn() { cancels++ }
            function connectServer() { connectCalls++ }
            function beginPlex() { plexStarts++ }
            function setEnabled(id, enabled) { enabledId=id }
            function remove(id) { removedId=id }
            function refresh(id) { refreshedId=id }
        }
    }
    Component { id: adapterComponent; App.VaultLibraryAdapter { library:null; progressStore:null } }
    Component { id: settingsComponent; QtObject { property bool railExpanded: true } }
    Component { id: pageComponent; App.VaultLibraryPage { width:1280; height:820 } }
    Component { id: cardComponent; App.HomeServerSyncCard { width:760 } }
    Component { id: trackersComponent; App.TrackerSyncCenterPage { width:1280; height:820; trackerModel:null; trackerConnector:null; stremioState:null; nuvioAccount:null; reducedMotion:true } }
    SignalSpy { id: world; signalName: "viewWorldRequested" }
    function init() { service=createTemporaryObject(serviceComponent,suite) }
    function cleanup() { world.target=null; world.clear(); wait(40) }
    function connection(id, provider, state, enabled) {
        return {id:id,name:"Server "+id,provider:provider,state:state,enabled:enabled !== false,
            dimmed:state !== "ready",offline:state === "unavailable",needsReconnect:state === "needsReconnect"}
    }
    function title(id, name) { return {id:id,title:name,kind:"movie",identity:{world:"Theatre",identityId:id,title:name}} }
    function publish(connections, libraries) {
        service.connections=connections; service.serverTabs=connections; service.libraries=libraries || ({}); service.changed()
    }
    function createAdapter() { return createTemporaryObject(adapterComponent,suite,{homeServers:service}) }
    function createPage(adapter) {
        const settings=createTemporaryObject(settingsComponent,suite)
        return createTemporaryObject(pageComponent,suite,{adapter:adapter,sidebarSettings:settings})
    }
    function panelFor(page) { return findChild(page,"vaultProviderConnectionLoader").item }
    function test_threeTrackerCardsFilterAndPreselect() {
        publish([connection("j1","jellyfin","ready"),connection("j2","jellyfin","cached"),connection("e1","emby","needsReconnect")])
        const trackers=createTemporaryObject(trackersComponent,suite,{homeServers:service})
        verify(trackers !== null); wait(50)
        compare(findChild(trackers,"homeServerSyncCard"),null)
        for(const provider of ["jellyfin","emby","plex"]) {
            const card=findChild(trackers,"homeServerSyncCard_"+provider)
            verify(card !== null)
            compare(card.provider,provider)
            compare(card.providerConnections.length,provider === "jellyfin" ? 2 : provider === "emby" ? 1 : 0)
            compare(card.providerStatusKind,provider === "jellyfin" ? "linked" : provider === "emby" ? "attention" : "unlinked")
            tryCompare(findChild(card,"homeServerProviderCardLogo_"+provider),"status",Image.Ready)
            trackers.openHomeServers(provider)
            const panel=findChild(card,"homeServerPanelLoader").item
            tryCompare(panel,"opened",true)
            compare(panel.provider,provider)
            panel.close(); wait(30)
        }
        compare(service.connectCalls,0); compare(service.plexStarts,0)
        trackers.openHomeServers()
        wait(30)
        for(const provider of ["jellyfin","emby","plex"])
            compare(findChild(trackers,"homeServerSyncCard_"+provider).panelOpen,false)
        const connectAction=findChild(trackers,"homeServerConnectButton")
        verify(!!connectAction, "Object exists")
        verify(connectAction.activeFocus)
        trackers.destroy(); wait(30)
    }
    function test_providerCardNativeControlsAndConnect() {
        publish([connection("j1","jellyfin","ready"),connection("e1","emby","ready")])
        const card=createTemporaryObject(cardComponent,suite,{service:service,provider:"emby"})
        wait(30)
        compare(findChild(card,"homeServerEnabled_j1"),null)
        mouseClick(findChild(card,"homeServerEnabled_e1")); compare(service.enabledId,"e1")
        mouseClick(findChild(card,"homeServerRefresh_e1")); compare(service.refreshedId,"e1")
        mouseClick(findChild(card,"homeServerRemove_e1")); compare(service.removedId,"e1")
        mouseClick(findChild(card,"homeServerConnectButton"))
        const panel=findChild(card,"homeServerPanelLoader").item
        tryCompare(panel,"opened",true); compare(panel.provider,"emby")
        panel.close(); wait(30)
    }
    function test_aggregateAllConnectionsAndKeepProvenance() {
        publish([connection("j1","jellyfin","ready"),connection("j2","jellyfin","cached"),
                 connection("e1","emby","ready"),connection("p1","plex","disabled",false)],{
            j1:[{id:"movies",name:"Movies",state:"ready",titles:[title("tt100","Shared"),title("tt101","First server")]}],
            j2:[{id:"movies",name:"Movies",state:"cached",titles:[title("tt100","Shared"),title("tt102","Second server")]}],
            e1:[{id:"shows",name:"Shows",titles:[title("tt103","Emby title")]}],
            p1:[{id:"disabled",name:"Disabled",titles:[title("tt104","Must stay absent")]}]})
        const adapter=createAdapter()
        compare(adapter.providers.length,3)
        compare(adapter.providers.map(row=>row.id).join(","),"jellyfin,emby,plex")
        const jellyfin=adapter.providers[0]
        compare(jellyfin.count,3); compare(jellyfin.connectionCount,2); compare(jellyfin.groups.length,2)
        const shared=jellyfin.rows.find(row=>row.id === "tt100")
        compare(shared.sourceServerIds.join(","),"j1,j2")
        compare(shared.serverId,"j1"); compare(shared.profileGeneration,4)
        compare(shared.identity.identityId,"tt100")
        verify(jellyfin.rows.some(row=>row.id === "tt102"))
        compare(jellyfin.groups[0].name,"Server j1 · Movies")
        compare(jellyfin.groups[1].name,"Server j2 · Movies")
        compare(jellyfin.groups[1].sourceLibraryId,"movies")
        compare(jellyfin.groups[1].serverId,"j2")
        const disabled=adapter.providers[2]
        compare(disabled.connected,true); compare(disabled.enabled,false)
        compare(disabled.rows.length,0); compare(disabled.groups.length,0)
        compare(adapter.serverRows.p1.length,0)
        verify(disabled.statusText.indexOf("disabled") >= 0)
    }
    function test_availableDuplicateWinsAndOfflineCacheRemains() {
        publish([connection("away","jellyfin","unavailable"),connection("ready","jellyfin","ready"),
                 connection("auth","emby","needsReconnect")],{
            away:[{id:"a",name:"Saved films",state:"unavailable",dimmed:true,titles:[title("tt100","Shared"),title("tt101","Cached only")]}],
            ready:[{id:"r",name:"Films",state:"ready",titles:[title("tt100","Shared")]}],
            auth:[{id:"e",name:"Saved shows",state:"needsReconnect",dimmed:true,titles:[title("tt102","Saved Emby")]}]})
        const adapter=createAdapter()
        const jellyfin=adapter.providers[0]
        compare(jellyfin.rows.length,2)
        compare(jellyfin.rows[0].serverId,"ready"); compare(jellyfin.rows[0].offline,false)
        compare(jellyfin.rows[0].sourceServerIds.join(","),"away,ready")
        verify(jellyfin.rows[1].offline)
        verify(jellyfin.statusText.indexOf("unavailable") >= 0)
        compare(jellyfin.dimmed,false)
        const emby=adapter.providers[1]
        compare(emby.rows.length,1); compare(emby.authRequired,true)
        compare(emby.rows[0].continueBlocked,true)
        const page=createPage(adapter)
        page.selectProvider("emby")
        compare(page.currentView,"server"); compare(page.currentServer,"emby")
        compare(page.providerConnectionOpen,false)
        world.target=page
        service.profileGeneration++
        page.openRow(jellyfin.rows[0]); compare(world.count,0)
        verify(page.notice.indexOf("changed") >= 0)
        compare(service.connectCalls,0); compare(service.plexStarts,0)
        page.destroy(); wait(30)
        publish([connection("cached","jellyfin","cached"),connection("live","jellyfin","ready")],{
            cached:[{id:"c",name:"Saved",state:"cached",dimmed:true,titles:[title("tt100","Shared")]}],
            live:[{id:"l",name:"Films",state:"ready",titles:[title("tt100","Shared")]}]})
        compare(adapter.providers[0].rows[0].serverId,"live")
        compare(adapter.providers[0].rows[0].dimmed,false)
    }
    function test_permanentRailCancelSuccessAndLastRemove() {
        const adapter=createAdapter()
        const page=createPage(adapter)
        wait(40)
        const sidebar=findChild(page,"vaultLibrarySidebar")
        for(const expanded of [true,false]) {
            page.sidebarSettings.railExpanded=expanded
            tryCompare(sidebar,"width",expanded ? 248 : 62)
            compare(sidebar.servers.length,3)
            for(const key of ["jellyfin","emby","plex"]) {
                const nav=findChild(page,"vaultNav_provider_"+key)
                verify(nav !== null); verify(nav.Accessible.name.indexOf("Not connected") >= 0)
                compare(nav.statusKind,"unlinked")
                compare(findChild(page,"vaultNav_provider_"+key+"Label").visible,expanded)
                tryCompare(findChild(page,"vaultNav_provider_"+key+"Icon"),"status",Image.Ready)
            }
        }
        const embyNav=findChild(page,"vaultNav_provider_emby")
        embyNav.forceActiveFocus(Qt.TabFocusReason); wait(30)
        mouseClick(embyNav)
        tryCompare(panelFor(page),"opened",true)
        compare(panelFor(page).provider,"emby")
        compare(page.currentView,"home"); compare(page.currentServer,"")
        panelFor(page).close(); wait(30)
        compare(page.currentView,"home"); compare(page.providerConnectionOpen,false)
        page.selectProvider("emby")
        tryCompare(panelFor(page),"opened",true)
        publish([connection("e1","emby","ready")],{e1:[{id:"movies",name:"Movies",titles:[title("tt100","Arrival")]}]})
        service.connectionAdded("e1")
        tryCompare(page,"currentServer","emby")
        compare(page.currentView,"server"); compare(page.providerConnectionOpen,false)
        compare(sidebar.servers.length,3)
        compare(findChild(page,"vaultLibraryContent").item.rows.length,1)
        verify(findChild(page,"vaultNav_provider_emby").active)
        compare(findChild(page,"vaultNav_server_e1"),null)
        publish([])
        tryCompare(page,"currentView","home")
        compare(sidebar.servers.length,3)
        compare(findChild(page,"vaultNav_provider_emby").statusText,"Not connected")
        page.selectProvider("emby")
        tryCompare(panelFor(page),"opened",true)
        panelFor(page).close(); wait(30)
        compare(service.connectCalls,0); compare(service.plexStarts,0)
        page.destroy(); wait(30)
    }
    function test_disabledProviderUsesExistingManagementWithoutAuth() {
        publish([connection("p1","plex","disabled",false)],{p1:[{id:"films",name:"Films",titles:[title("tt100","Disabled title")]}]})
        const adapter=createAdapter()
        const page=createPage(adapter)
        page.selectProvider("plex")
        compare(page.currentView,"server"); compare(page.currentServer,"plex")
        compare(page.providerConnectionOpen,false)
        const view=findChild(page,"vaultLibraryContent").item
        compare(view.rows.length,0); compare(view.groups.length,0)
        verify(view.statusText.indexOf("disabled") >= 0)
        view.reconnectRequested()
        compare(page.sourcesOpen,true); compare(page.providerConnectionOpen,false)
        page.handleBack(); wait(260)
        compare(service.connectCalls,0); compare(service.plexStarts,0)
        page.destroy(); wait(30)
    }
    function test_providerEntryAndKeyboardFocusSurvivePublish() {
        publish([connection("j1","jellyfin","cached")])
        const adapter=createAdapter()
        const page=createPage(adapter)
        wait(40)
        const nav=findChild(page,"vaultNav_provider_jellyfin")
        nav.forceActiveFocus(Qt.TabFocusReason)
        publish([connection("j1","jellyfin","ready"),connection("j2","jellyfin","ready")])
        wait(20)
        compare(findChild(page,"vaultNav_provider_jellyfin"),nav)
        verify(nav.activeFocus)
        page.destroy(); wait(30)
    }
}
