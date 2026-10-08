import QtQuick
import QtTest
import QtWebEngine
import "../../../qml/feria" as Feria

Item {
    id: surface
    width: 1024; height: 760
    property var page: null
    property var owner: null
    property var nativeProfile: null
    property var viewer: null
    property int serial: 0
    Feria.FeriaSaveContextBar { id: contextBar; width: parent.width; tools: surface.page ? surface.page.pageTools : null }
    Component { id: profileFactory; WebEngineProfile { offTheRecord: true } }
    Component {
        id: ownerFactory
        Feria.FeriaBrowserDownloads {
            profile: null; policy: FeriaBrowserPolicy; accountStore: FeriaAccount
            privateBrowsing: true; destinationPolicy: "ask"
            // Qualified only within this fixture: every SavePage ingress below
            // goes through the real wrapper's gate. Default product gate is off.
            pageSaveIngressExclusive: true
        }
    }
    Component {
        id: pageFactory
        Feria.FeriaQtWebEngine {
            width: surface.width; y: contextBar.height; height: surface.height - y
            sourceUrl: Fixture.baseUrl + "/save-context"; profilePath: ""
            observationEnabled: false; freezeBackground: false
            saveContextControlsAvailable: true
            accountSessionId: "save-fixture"; accountVisitToken: ""
            saveBinding: ({accountEpoch: 1, profileId: "save-fixture", spaceId: "save-fixture",
                pageGeneration: 1, documentGeneration: 1, hostId: "fixture", hostGeneration: 1})
        }
    }
    Component { id: viewerFactory; WebEngineView { width: 500; height: 400; visible: false } }
    TestCase {
        id: suite
        name: "FeriaNativeSave20"
        when: windowShown
        function init() {
            Fixture.reset()
            nativeProfile = profileFactory.createObject(surface, {downloadPath: Fixture.downloadsDirectory})
            verify(nativeProfile !== null)
            owner = ownerFactory.createObject(surface, {profile: nativeProfile})
            verify(owner !== null)
            page = pageFactory.createObject(surface, {sharedProfile: nativeProfile, saveAdmissionOwner: owner})
            verify(page !== null)
            tryVerify(function() { return page.documentReady && !page.loading }, 15000)
            page.forceActiveFocus()
            verify(page.mainFrame.isValid)
            compare(page.mainFrame.children.length, 0, "Qt 6.11.1 public mainFrame.children is exercised")
        }
        function cleanup() {
            if (viewer) { viewer.destroy(); viewer = null }
            if (owner) {
                for (var entry of owner.items) if (entry.request && !entry.request.isFinished) entry.request.cancel()
                wait(100)
                owner.retire("fixture-cleanup")
            }
            if (page) {
                page.prepareForClose(false)
                tryCompare(page, "pendingJavaScript", 0, 5000)
            }
            // Do not exercise the unsafe pre-delivery source-close race as
            // cleanup after a failed assertion. Retire that profile while its
            // source object remains retained. Normal teardown destroys views first.
            var awaitingNative = owner && owner.pageSaveIntent && !owner.pageSaveIntent.downloadId
            if (awaitingNative && nativeProfile) { nativeProfile.destroy(); nativeProfile = null; wait(0) }
            if (page) { page.destroy(); page = null; wait(0) }
            if (owner) { owner.destroy(); owner = null; wait(0) }
            if (nativeProfile) { nativeProfile.destroy(); nativeProfile = null }
            wait(30)
        }
        function evaluate(script) {
            var done = false, result
            verify(page.evaluateJavaScript(script, function(value) { result = value; done = true }))
            tryVerify(function() { return done }, 5000)
            return result
        }
        function context(id) {
            var point = evaluate("(()=>{let r=document.getElementById(" + JSON.stringify(id) + ").getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2}})()")
            var old = page.pageTools.contextSerial
            mouseClick(page, point.x, point.y, Qt.RightButton)
            tryVerify(function() { return page.pageTools.contextSerial > old && page.pageTools.contextSnapshot !== null }, 5000)
            return page.pageTools.contextSnapshot
        }
        function waitOffer(count) {
            tryVerify(function() { return owner.items.length === count }, 15000)
            var entry = owner.items[count - 1]
            compare(entry.request.state, WebEngineDownloadRequest.DownloadRequested)
            compare(entry.request.isFinished, false)
            verify(!Fixture.fileExists(entry.path), "Requested must not write before destination admission")
            return entry
        }
        function accept(entry, suffix) {
            var name = "save-" + (++serial) + "-" + suffix
            var result = owner.decideDownload(entry.downloadId, entry.decisionToken,
                {directory: Fixture.downloadsDirectory, fileName: name})
            compare(result.status, "dispatched")
            tryVerify(function() { return entry.finished }, 15000)
            compare(entry.state, WebEngineDownloadRequest.DownloadCompleted)
            verify(Fixture.fileExists(entry.path))
            verify(Fixture.pathSize(entry.path) > 0)
            compare(entry.metadataState, "private", "Private saves must not enter ordinary history")
            return entry.path
        }
        function test_contextSerialNavigationAndHostFences() {
            var a = context("linkA")
            var b = context("linkB")
            verify(a.linkUrl.indexOf("alpha.bin") >= 0)
            verify(b.linkUrl.indexOf("beta.bin") >= 0)
            compare(page.pageTools.saveFromContext(a.contextId, "link").status, "invalidated")
            compare(owner.items.length, 0)
            var old = page.navigationGeneration
            page.reload()
            tryVerify(function() { return page.navigationGeneration > old && page.documentReady && !page.loading }, 15000)
            compare(page.pageTools.saveFromContext(b.contextId, "link").status, "invalidated")
            var current = context("linkA")
            page.saveBinding = Object.assign({}, page.saveBinding, {hostGeneration: 2})
            compare(page.pageTools.saveFromContext(current.contextId, "link").status, "invalidated")
            compare(owner.items.length, 0)
        }
        function test_saveBearingNativeMenusAreReplaced() {
            var accepted = false
            var observed = 0
            function record(request) { accepted = request.accepted; ++observed }
            page.contextMenuRequested.connect(record)
            context("linkA")
            verify(observed > 0)
            verify(accepted, "The native menu must not expose an unguarded SavePage ingress")
            verify(contextBar.open)
            verify(page.y >= contextBar.y + contextBar.height, "Save controls must not overlay the website")
            verify(findChild(contextBar, "feriaContextCopy").visible)
            verify(findChild(contextBar, "feriaContextOpenLink").visible)
            page.contextMenuRequested.disconnect(record)
            if (Fixture.evidenceRoot.length) verify(Fixture.captureWindow(surface, "category20-native-context-controls.png"))
        }
        function test_linkAndImageUseNativeProfilePipeline() {
            for (var row of [{id:"linkA",kind:"link",suffix:"alpha.bin"}, {id:"image",kind:"image",suffix:"image.png"}]) {
                var before = owner.items.length
                var snapshot = context(row.id)
                compare(page.pageTools.saveFromContext(snapshot.contextId, row.kind).status, "dispatched")
                var entry = waitOffer(before + 1)
                compare(entry.request.isSavePageDownload, false)
                compare(entry.snapshot().sourceAvailable, false, "No URL/order source inference for resource requests")
                var path = accept(entry, row.suffix)
                if (row.kind === "link") compare(Fixture.checksum(path), Fixture.expectedChecksum(""))
                else compare(Fixture.checksum(path), Fixture.expectedChecksum("/image.png"))
            }
        }
        function test_mediaCanSaveAndUnsupportedContexts() {
            tryVerify(function() { return evaluate("document.getElementById('audio').readyState >= 2") }, 10000)
            var audio = context("audio")
            compare(audio.mediaType, ContextMenuRequest.MediaTypeAudio)
            verify((audio.mediaFlags & ContextMenuRequest.MediaCanSave) !== 0)
            verify(page.action(WebEngineView.DownloadMediaToDisk).enabled)
            // This isolated policy injection strips one public capability flag
            // on a real native context; it is not a claim the engine emitted it.
            var limited = page.pageTools.captureSaveContext({linkUrl:audio.linkUrl,mediaUrl:audio.mediaUrl,
                mediaType:audio.mediaType,mediaFlags:audio.mediaFlags & ~ContextMenuRequest.MediaCanSave})
            compare(page.pageTools.saveFromContext(limited.contextId,"media").status,"unsupported")
            audio = context("audio")
            compare(page.pageTools.saveFromContext(audio.contextId,"media").status,"dispatched")
            var audioPath = accept(waitOffer(1), "audio.wav")
            compare(Fixture.checksum(audioPath), Fixture.expectedChecksum("/media.wav"))
            var canvas = context("canvas")
            compare(page.pageTools.saveFromContext(canvas.contextId,"image").status,"unsupported")
            var scriptLink = context("javascriptLink")
            compare(page.pageTools.saveFromContext(scriptLink.contextId,"link").status,"unsupported")
            compare(owner.items.length, 1)
        }
        function test_subframeFidelityIsExplicitlyUnavailable() {
            evaluate("let f=document.createElement('iframe');f.id='subframe';f.src='/save-page';document.body.appendChild(f);true")
            tryVerify(function() { return page.mainFrame.children.length === 1 }, 10000)
            var snapshot = context("linkA")
            compare(snapshot.frameProvenance,"unavailable")
            compare(snapshot.mainFrameOnly,false)
            compare(page.pageTools.saveFromContext(snapshot.contextId,"link").status,"unsupported")
            compare(owner.items.length,0)
        }
        function test_nativePageFormatsRequestedRoundtripAndSerialization() {
            page.url = Fixture.baseUrl + "/save-page"
            tryVerify(function() { return page.documentReady && !page.loading && String(page.url).indexOf('/save-page') >= 0 },15000)
            var formats = [WebEngineDownloadRequest.SingleHtmlSaveFormat,
                WebEngineDownloadRequest.CompleteHtmlSaveFormat, WebEngineDownloadRequest.MimeHtmlSaveFormat]
            for (var format of formats) {
                var before = owner.items.length
                compare(page.pageTools.savePage(format).status,"dispatched")
                compare(page.pageTools.savePage(format).status,"blocked", "Second native Page Save cannot overwrite pending target info")
                var entry = waitOffer(before + 1)
                compare(entry.request.isSavePageDownload,true)
                compare(entry.request.savePageFormat,format)
                compare(entry.snapshot().sourceStatus,"exclusive-app-page-save")
                verify(owner.pendingPageSaveForView(page))
                var path = accept(entry,format === WebEngineDownloadRequest.MimeHtmlSaveFormat ? 'page.mhtml' : 'page.html')
                tryVerify(function() { return !owner.pendingPageSaveForView(page) },5000)
                viewer = viewerFactory.createObject(surface)
                verify(viewer !== null)
                var loaded = false, succeeded = false
                function loadedPage(info) { if (info.status === WebEngineView.LoadSucceededStatus) { loaded=true;succeeded=true } else if(info.status===WebEngineView.LoadFailedStatus) loaded=true }
                viewer.loadingChanged.connect(loadedPage)
                viewer.url = "file://" + path
                tryVerify(function() { return loaded },15000)
                verify(succeeded,"Saved native format must load again")
                var done=false, marker=false
                viewer.runJavaScript("document.body.innerText.includes('FERIA_SAVE_PAGE_MARKER')",function(value){marker=value;done=true})
                tryVerify(function(){return done},5000); verify(marker)
                viewer.destroy(); viewer=null
            }
        }
        function test_nativePageDestinationCancelAndNavigationBlocked() {
            compare(page.pageTools.savePage().status,"dispatched")
            var entry=waitOffer(1), generation=page.navigationGeneration
            evaluate("location.href='/save-page';true")
            wait(150)
            compare(page.navigationGeneration,generation,"Navigation must not replace a document during serialization admission")
            compare(owner.decideDownload(entry.downloadId,entry.decisionToken,{cancel:true}).status,"cancelled")
            tryVerify(function(){return entry.finished && !owner.pendingPageSaveForView(page)},5000)
            compare(entry.state,WebEngineDownloadRequest.DownloadCancelled)
            verify(!Fixture.fileExists(entry.path))
        }
        function test_noOwnerNoExplicitPathFallback() {
            page.saveAdmissionOwner=null
            compare(page.pageTools.savePage().status,"unsupported")
            compare(owner.items.length,0)
            page.saveAdmissionOwner=owner
        }
    }
}
