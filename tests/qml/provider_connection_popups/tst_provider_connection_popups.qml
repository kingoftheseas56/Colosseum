import QtQuick
import QtQuick.Controls
import QtTest
import "../../../qml" as App

TestCase {
    id: suite
    name: "ProviderConnectionPopups"
    width: 1000; height: 720; visible: true
    when: windowShown
    FontLoader { source: "../../../assets/fonts/Fraunces-Regular.ttf" }
    Button { id: invoker; x:8; y:8; text:"Connection invoker" }
    QtObject {
        id: home
        property bool available: true
        property bool busy: false
        property int profileGeneration: 3
        property string status: ""
        property string plexCode: ""
        property string plexUrl: ""
        property var plexServers: []
        property bool httpConsentRequired: false
        property int cancels: 0
        property int submits: 0
        property string submittedProvider: ""
        signal changed()
        signal connectionAdded(string id)
        function isInsecureAddress(address) { return address.indexOf("http://") === 0 }
        function cancelSignIn() { cancels++ }
        function connectServer(provider) { submits++; submittedProvider=provider }
        function beginPlex() { fail("No real Plex sign-in is needed") }
    }
    QtObject {
        id: stremio
        property string status: "notConnected"
        property bool linkedAccount: false
        property string accountDisplayName: ""
        property string lastResultSummary: ""
        property int cancels: 0
        function cancelAuthentication() { cancels++; status="notConnected" }
        function connectAccount() { status="connecting" }
        function switchAccount() {}
        function disconnectCurrentProfile() {}
    }
    QtObject {
        id: actionFixture
        property string stremioImportPhase: "idle"
        property string stremioImportMessage: ""
        property bool stremioImportBecomesPrimary: false
        property var stremioImportPreview: []
        property int canceled: 0
        function beginStremioFirstRun() { stremioImportPhase="loading" }
        function previewStremioImport() { stremioImportPhase="preview" }
        function applyStremioImport() { stremioImportPhase="applying" }
        function cancelStremioImport() { canceled++; stremioImportPhase="idle" }
        function syncStremioNow() {}
    }
    QtObject {
        id: nuvio
        property bool available: true
        property bool connected: false
        property bool busy: false
        property bool needsReconnect: false
        property string phase: "idle"
        property string message: ""
        property string primaryProvider: ""
        property string selectedProfileName: ""
        property string connectedProfileName: ""
        property string deviceUserCode: ""
        property var profiles: []
        property var preview: []
        property int cancels: 0
        function cancel() { cancels++; phase="idle" }
        function cancelPin() { phase="profiles" }
        function cancelDeviceSignIn() { phase="signIn" }
    }
    Component { id: homeComponent; App.HomeServerConnectPanel { service:home } }
    Component { id: syncComponent; App.StremioSyncPanel { width:1000; height:720; syncState:stremio; actions:actionFixture; nuvioAccount:nuvio } }
    SignalSpy { id: closed; signalName:"closeRequested" }
    property var active: null
    function init() {
        home.cancels=0; home.submits=0; home.status=""; home.profileGeneration=3
        stremio.cancels=0; stremio.status="notConnected"
        actionFixture.canceled=0; actionFixture.stremioImportPhase="idle"
        nuvio.cancels=0; nuvio.phase="idle"; nuvio.message=""
        invoker.forceActiveFocus(Qt.TabFocusReason)
    }
    function cleanup() {
        closed.target=null; closed.clear()
        if(active) { if(active.close) active.close(); else active.shown=false; active.destroy(); active=null }
        wait(30)
    }
    function visibleText(item, text) {
        if (item.visible && item.text === text) return item
        for(const child of item.children || []) { const match=visibleText(child,text); if(match) return match }
        return null
    }
    function openHome(provider) {
        active=createTemporaryObject(homeComponent,suite,{initialProvider:provider,invoker:invoker})
        active.open(); tryCompare(active,"opened",true)
        return active
    }
    function openSync(provider) {
        active=createTemporaryObject(syncComponent,suite)
        verify(active.openForProvider(provider,invoker))
        wait(20)
        closed.target=active
        return active
    }
    function test_homeProviderIsFrozenAndBranded_data() {
        return [{tag:"jellyfin",provider:"jellyfin",title:"Jellyfin"}, {tag:"emby",provider:"emby",title:"Emby"}, {tag:"plex",provider:"plex",title:"Plex"}]
    }
    function test_homeProviderIsFrozenAndBranded(data) {
        const panel=openHome(data.provider)
        compare(panel.provider,data.provider)
        compare(findChild(panel,"homeServerProviderTitle").text,"Connect "+data.title)
        tryCompare(findChild(panel,"homeServerProviderLogo_"+data.provider),"status",Image.Ready)
        compare(findChild(panel,"homeServerProviderTiles"),null)
        compare(findChild(panel,"homeServerProvider"),null)
        for(const key of ["jellyfin","emby","plex"]) compare(findChild(panel,"homeServerProvider_"+key),null)
        panel.initialProvider=data.provider === "plex" ? "emby" : "plex"
        compare(panel.provider,data.provider)
        compare(findChild(panel,"homeServerPassword").visible,data.provider !== "plex")
        const close=findChild(panel,"providerConnectionClose")
        mouseClick(close)
        tryCompare(panel,"visible",false)
        compare(home.cancels,1); compare(stremio.cancels,0); compare(nuvio.cancels,0)
        tryCompare(invoker,"activeFocus",true)
    }
    function test_homeErrorConsentAndReopenClearSecrets() {
        const panel=openHome("emby")
        const address=findChild(panel,"homeServerAddress"), user=findChild(panel,"homeServerUsername"), password=findChild(panel,"homeServerPassword")
        address.text="http://fixture.invalid"; user.text="viewer"; password.text="fixture-secret"
        const submit=findChild(panel,"homeServerSubmit"), consent=findChild(panel,"homeServerHttpConsent")
        compare(submit.enabled,false)
        verify(consent.visible); verify(consent.text.indexOf("HTTP") >= 0)
        consent.checked=true
        compare(submit.enabled,true)
        home.status="The server refused this sign-in. Check the address and try again."; home.changed()
        compare(findChild(panel,"homeServerStatus").text,home.status)
        verify(panel.visible)
        panel.close(); tryCompare(panel,"visible",false)
        compare(password.text,""); compare(consent.checked,false)
        panel.initialProvider="emby"; panel.open(); tryCompare(panel,"opened",true)
        compare(panel.provider,"emby"); compare(password.text,"")
        panel.close(); tryCompare(panel,"visible",false)
        compare(home.cancels,2)
    }
    function test_syncProviderIsolation_data() { return [{tag:"stremio",provider:"stremio"},{tag:"nuvio",provider:"nuvio"}] }
    function test_syncProviderIsolation(data) {
        const panel=openSync(data.provider)
        compare(panel.providerKey,data.provider)
        compare(findChild(panel,"syncStremioService"),null); compare(findChild(panel,"syncNuvioService"),null)
        compare(findChild(panel,"nuvioAccountFlowIcon").visible,data.provider === "nuvio")
        compare(findChild(panel,"stremioPanelOfficialAsset").visible,data.provider === "stremio")
        verify(!panel.openForProvider(data.provider === "nuvio" ? "stremio" : "nuvio",invoker))
        compare(panel.providerKey,data.provider)
        if(data.provider === "nuvio") {
            findChild(panel,"nuvioEmail").text="fixture@example.invalid"
            findChild(panel,"nuvioPassword").text="fixture-secret"
            nuvio.message="Check your sign-in details and try again."
            verify(visibleText(panel,nuvio.message) !== null)
        } else stremio.status="connecting"
        mouseClick(findChild(panel,"providerConnectionClose"))
        compare(panel.shown,false)
        compare(closed.count,1)
        compare(nuvio.cancels,data.provider === "nuvio" ? 1 : 0)
        compare(stremio.cancels,data.provider === "stremio" ? 1 : 0)
        if(data.provider === "nuvio") compare(findChild(panel,"nuvioPassword").text,"")
        tryCompare(invoker,"activeFocus",true)
    }
    function test_stremioFirstRunApplyingGuardAndConfirmationsRemain() {
        active=createTemporaryObject(syncComponent,suite,{firstRun:true,shown:true})
        closed.target=active
        active.firstRunStarted=true
        actionFixture.stremioImportPhase="applying"
        active.closePanel()
        compare(closed.count,0); compare(actionFixture.canceled,0)
        actionFixture.stremioImportPhase="preview"
        active.closePanel()
        compare(closed.count,1); compare(actionFixture.canceled,1)
        compare(nuvio.cancels,0)
    }
    function test_syncEscapeCancelsOnlySelectedFlow_data() { return [{tag:"stremio",provider:"stremio"},{tag:"nuvio",provider:"nuvio"}] }
    function test_syncEscapeCancelsOnlySelectedFlow(data) {
        const panel=openSync(data.provider)
        if(data.provider === "stremio") stremio.status="connecting"
        findChild(panel,"providerConnectionClose").forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Escape)
        compare(panel.shown,false)
        compare(stremio.cancels,data.provider === "stremio" ? 1 : 0)
        compare(nuvio.cancels,data.provider === "nuvio" ? 1 : 0)
        tryCompare(invoker,"activeFocus",true)
    }
    function test_syncCloseDoesNotCancelOtherServiceImport() {
        actionFixture.stremioImportPhase="preview"
        const panel=openSync("nuvio")
        panel.closePanel()
        compare(nuvio.cancels,1); compare(actionFixture.canceled,0)
        compare(actionFixture.stremioImportPhase,"preview")
    }
}
