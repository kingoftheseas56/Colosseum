// Production views driven by explicit catalogue fixtures, not a fake native service.
// Native facade invocation/dedup is separately covered by source-extracted checks.
import QtQuick
import QtQuick.Window
import QtTest
import "../../qml/feria"
import "../../qml/feria/PorticoData.js" as FixtureData
import "../../qml/feria/FeriaRecoveryState.js" as Recovery

Window {
    id: window
    width: 1280; height: 800; visible: true
    flags: Qt.Window | Qt.FramelessWindowHint
    property bool completed: false
    property int assertions: 0
    property var rows: [{id:"fixture-feed",title:"Fixture source",src:"Fixture",items:["stranger","phm"],loading:false,error:"offline",stale:true}]
    property var feeds: [{shelfId:"fixture-feed",title:"Fixture source",sourceLabel:"Fixture",items:[{canonicalKey:"stranger"},{canonicalKey:"phm"}],loading:false,error:"offline"}]
    property var hits: ["stranger","phm"]
    property int retries: 0
    property string retryId: ""
    property int activations: 0
    FeriaWorld { id: base; visible: false }
    QtObject {
        id: fixture
        property alias unit: base.unit
        property alias marginX: base.marginX
        property alias galleryMetrics: base.galleryMetrics
        property alias ink: base.ink
        property alias mist: base.mist
        property alias slate: base.slate
        property alias gold: base.gold
        property alias dusk: base.dusk
        property alias night: base.night
        property alias uiFont: base.uiFont
        property alias displayFont: base.displayFont
        property alias backdrop: base.backdrop
        property alias activeApps: base.activeApps
        property alias selectedApp: base.selectedApp
        property alias movingApp: base.movingApp
        property alias focusArea: base.focusArea
        property alias focusIndex: base.focusIndex
        property alias shelfIndex: base.shelfIndex
        property alias shelfCardIndex: base.shelfCardIndex
        property alias chipSelection: base.chipSelection
        property alias lens: base.lens
        property string viewState: "home"
        property bool navigationSuspended: false
        property bool liveDiscovery: true
        property bool aggregateRetryPending: false
        property var feedRetryPending: ({})
        property var catalogueFeeds: window.feeds
        property var catalogueStatus: Recovery.aggregate(window.feeds, true)
        property var keyboardTarget: fixtureKeys
        property var accountStore: null
        property string query: "fixture query"
        property int searchResultIndex: 0
        property string seeAllKind: "shelf"
        property string seeAllShelfId: "fixture-feed"
        property string seeAllTitle: "Fixture source"
        property var seeAllShelf: window.rows.length ? window.rows[0] : ({})
        function shownShelves() { return window.rows }
        function shelfItems(row) { return row ? row.items || [] : [] }
        function continueEntries() { return [] }
        function providerName(pk) { return base.providerName(pk) }
        function titleObj(id) { return FixtureData.T[id] || null }
        function featured() { return {id:"stranger",item:FixtureData.T.stranger,source:"Fixture",app:"netflix"} }
        function artUrl(it, wide) { return "" }
        function shapeFor(it) { return "poster" }
        function toneFor(id) { return ["#334055","#111722"] }
        function kindLabel(it) { return "Fixture title" }
        function offers(it) { return [] }
        function visibleResults() { return window.hits }
        function searchableApps() { return ["netflix"] }
        function retryFeed(id) { window.retries++; window.retryId = id }
        function retryCatalogue() { window.retries++; window.retryId = "aggregate" }
        function noteNavigationInput() { home.cancelRestoration(); see.cancelRestoration() }
        function selectApp(index) { focusIndex=index;focusArea="app" }
        function selectLens(key) { lens=key }
        function openSeeAll(row) { viewState="seeAll" }
        function openTitle(id) { window.activations++ }
        function openHost(pk,id,mode,url) { window.activations++ }
        function openApps() {}
        function resumeSession(entry) { window.activations++ }
        function activateSearchChoice() {}
        function back() { viewState="home" }
    }
    Item { id: fixtureKeys }
    PorticoDiscoveryHome { id: home; anchors.fill: parent; controller:fixture; visible:fixture.viewState==="home" }
    PorticoDiscoverySearchRedesign { id: search; anchors.fill:parent; controller:fixture; visible:fixture.viewState==="search" }
    FeriaSeeAllView { id: see; anchors.fill:parent; controller:fixture; visible:fixture.viewState==="seeAll" }
    function find(item,name) {
        if (!item) return null
        if (item.objectName===name) return item
        for (var i=0;i<item.children.length;i++) {
            var found=find(item.children[i],name)
            if (found) return found
        }
        return null
    }
    TestCase {
        id: probe
        when: windowShown
        function check(ok,detail) { window.assertions++;browserReporter.report("feria-recovery-catalogue",!!ok,detail);verify(ok,detail) }
        function run() {
            wait(200)
            var retry=window.find(home,"feriaFeed_fixture-feedAction")
            check(retry && retry.enabled,"Cached failed feed has reachable Retry")
            retry.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Return)
            check(window.retries===1 && window.retryId==="fixture-feed","Home Retry passes real fixture feed identity once")
            check(fixture.shownShelves()[0].items.length===2,"Error status leaves cached titles visible")

            fixture.viewState="search"
            wait(80)
            fixture.searchResultIndex=1
            window.hits=["phm","stranger"]
            wait(80)
            check(fixture.searchResultIndex===0 && fixture.query==="fixture query","Search revision preserves selected identity and typed query")
            window.hits=["stranger"]
            wait(80)
            check(fixture.searchResultIndex===-1,"Removed hit is deselected rather than activating another title")
            window.hits=[]
            window.feeds=[{shelfId:"fixture-feed",title:"Fixture",items:[],loading:true}]
            wait(80)
            check(fixture.catalogueStatus.kind==="loading","Empty hits retain loading catalogue context")

            fixture.viewState="seeAll"
            window.rows=[]
            wait(80)
            check(see.recoveryStatus.kind==="loading","An existing empty loading feed is not misreported removed")
            window.feeds=[{shelfId:"fixture-feed",title:"Fixture",items:[],loading:false,error:"offline"}]
            wait(80)
            check(see.recoveryStatus.kind==="error","Empty failed feed has error recovery")
            window.feeds=[{shelfId:"fixture-feed",title:"Fixture",items:[],loading:false,error:""}]
            wait(80)
            check(see.recoveryStatus.kind==="empty","Ready empty feed differs from failure")
            window.feeds=[]
            wait(80)
            check(see.recoveryStatus.kind==="removed","Removed feed is distinct from empty")
            var before=window.activations
            see.activateSelection()
            check(window.activations===before,"Removed row cannot activate stale title")
            fixture.seeAllKind="continue"
            wait(80)
            check(see.recoveryStatus.kind==="empty" && !see.recoveryStatus.retry,"Empty Continue does not offer source retry")

            for (var size of [[960,640],[1280,800],[1920,1080]]) {
                window.width=size[0];window.height=size[1]
                for (var route of ["home","search","seeAll"]) {
                    fixture.viewState=route
                    wait(80)
                    var view=route==="home"?home:route==="search"?search:see
                    check(view.width===size[0] && view.height===size[1],route+" remains sized at "+size[0]+"x"+size[1])
                }
            }
            window.completed=true
        }
        function cleanup() {
            browserReporter.report("feria-recovery-catalogue",window.completed && window.assertions>=20,
                                   "Catalogue recovery phases executed; assertions="+window.assertions)
            browserReporter.finish()
        }
    }
    Timer { interval:100;running:true;onTriggered:{window.hide();window.show();window.requestActivate()} }
    Timer { interval:500;running:true;onTriggered:{try{probe.run()}finally{probe.cleanup()}} }
    Timer { interval:20000;running:true;onTriggered:{browserReporter.report("feria-recovery-catalogue",false,"Timed out");browserReporter.finish()} }
}
