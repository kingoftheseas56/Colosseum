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
    property Item presentedTools: null
    property int serial: 0
    Feria.FeriaSaveContextBar { id: contextBar; width: parent.width; tools: surface.presentedTools || (surface.page ? surface.page.pageTools : null) }
    Component {
        id: profileFactory
        WebEngineProfile {
            property bool ordinary: false
            property string partitionName: ""
            storageName: ordinary ? partitionName : ""
            offTheRecord: !ordinary
            persistentStoragePath: ordinary ? Fixture.downloadsDirectory + "/profile-" + partitionName : ""
            cachePath: ordinary ? Fixture.downloadsDirectory + "/cache-" + partitionName : ""
        }
    }
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
    // Presentation-only replacement controller. Native capture/actions remain
    // exercised on the real PageTools; this double observes accidental retargeting.
    Component {
        id: replacementToolsFactory
        Item {
            property var contextSnapshot: null
            property bool canSavePage: false
            property bool canSaveLink: true
            property bool canSaveImage: false
            property bool canSaveMedia: false
            property int calls: 0
            function admittedSaveUrl(value) { return true }
            function contextNativeAction(id, kind) { ++calls }
            function saveFromContext(id, kind) { ++calls }
            function invalidateSaveContext(reason) { contextSnapshot = null }
        }
    }
    TestCase {
        id: suite
        name: "FeriaNativeSave20"
        when: windowShown
        function init() { initialize(false) }
        function initialize(ordinary) {
            surface.width = 1024
            Fixture.reset()
            ++surface.serial
            if (ordinary) Fixture.switchProfile("save-context-account-" + surface.serial)
            nativeProfile = profileFactory.createObject(surface, {ordinary: ordinary, partitionName: "save-context-" + surface.serial, downloadPath: Fixture.downloadsDirectory})
            verify(nativeProfile !== null)
            owner = ownerFactory.createObject(surface, {profile: nativeProfile, privateBrowsing: !ordinary})
            verify(owner !== null)
            page = pageFactory.createObject(surface, {sharedProfile: nativeProfile, saveAdmissionOwner: owner})
            verify(page !== null)
            if (ordinary) {
                page.accountVisitToken = FeriaAccount.beginSessionVisit(page.accountSessionId,
                    {pk: "mangaplus", privateBrowsing: false, title: "Native Save fixture"})
                verify(page.accountVisitToken.length > 0)
            }
            tryVerify(function() { return page.documentReady && !page.loading }, 15000)
            page.forceActiveFocus()
            verify(page.mainFrame.isValid)
            compare(page.mainFrame.children.length, 0, "Qt 6.11.1 public mainFrame.children is exercised")
        }
        function cleanup() {
            if (presentedTools) { presentedTools.destroy(); presentedTools = null; wait(0) }
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
            // A failed fixture may leave a pre-delivery Save with no public
            // cancellation/drain handle. Do not turn cleanup into the unsafe
            // source-close race, or destroy a profile under a live source.
            // Exit only this test process nonzero; remaining cases are NOT RUN.
            var awaitingNative = owner && owner.pageSaveIntent && !owner.pageSaveIntent.downloadId
            if (awaitingNative) Fixture.unsupportedCleanup("PageSave request was not delivered; safe source/profile teardown is unqualified")
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
        function accept(entry, suffix, expectedMetadata) {
            var name = "save-" + (++serial) + "-" + suffix
            var result = owner.decideDownload(entry.downloadId, entry.decisionToken,
                {directory: Fixture.downloadsDirectory, fileName: name})
            compare(result.status, "dispatched")
            tryVerify(function() { return entry.finished }, 15000)
            compare(entry.state, WebEngineDownloadRequest.DownloadCompleted)
            verify(Fixture.fileExists(entry.path))
            verify(Fixture.pathSize(entry.path) > 0)
            tryCompare(entry, "metadataState", expectedMetadata || "private", 5000)
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
            compare(contextBar.color, "#202936", "Chrome needs its own readable backdrop")
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
            verify(findChild(contextBar,"feriaContextCopy").visible)
            compare(page.pageTools.contextNativeAction(canvas.contextId,"copyImage").status,"dispatched")
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
            var previousGeneration = page.navigationGeneration
            page.url = Fixture.baseUrl + "/save-page"
            tryVerify(function() { return page.navigationGeneration > previousGeneration && page.documentReady
                && !page.loading && String(page.url).indexOf('/save-page') >= 0 },15000)
            verify(evaluate("document.body.innerText.includes('FERIA_SAVE_PAGE_MARKER')"), "The intended fixture document must commit before native Save")
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
                var loaded = false, succeeded = false, targetUrl = "file://" + path
                function loadedPage(info) {
                    console.info("SAVE_ROUNDTRIP_LOAD",JSON.stringify({format:format,target:targetUrl,url:String(info.url),status:info.status}))
                    if (String(info.url) !== targetUrl) return
                    if (info.status === WebEngineView.LoadSucceededStatus) { loaded=true;succeeded=true }
                    else if(info.status===WebEngineView.LoadFailedStatus) loaded=true
                }
                viewer.loadingChanged.connect(loadedPage)
                viewer.url = targetUrl
                tryVerify(function() { return loaded && !viewer.loading && String(viewer.url) === targetUrl },15000)
                verify(succeeded,"Saved native format must load again")
                if (Fixture.evidenceRoot.length)
                    verify(Fixture.copyToEvidence(path,"category20-saved-format-" + format + (format === WebEngineDownloadRequest.MimeHtmlSaveFormat ? ".mhtml" : ".html")))
                console.info("SAVE_ROUNDTRIP", JSON.stringify({format:format, path:path, bytes:Fixture.pathSize(path),
                    fileContainsMarker:Fixture.readText(path).indexOf("FERIA_SAVE_PAGE_MARKER") >= 0,
                    viewerUrl:String(viewer.url), title:viewer.title}))
                // MHTML intentionally disables main-world scripts. Native Find
                // verifies actual rendered text without changing its sandbox.
                var found=false, matches=0
                function findFinished(result) { matches=result.numberOfMatches; found=true }
                viewer.findTextFinished.connect(findFinished)
                viewer.findText("FERIA_SAVE_PAGE_MARKER")
                tryVerify(function(){return found},5000)
                compare(matches,1,"Saved format must render the fixture marker: " + format)
                viewer.findTextFinished.disconnect(findFinished)
                viewer.loadingChanged.disconnect(loadedPage)
                viewer.destroy(); viewer=null
            }
        }
        function test_nativePageDestinationCancelWritesNoFile() {
            compare(page.pageTools.savePage().status,"dispatched")
            var entry=waitOffer(1)
            compare(owner.decideDownload(entry.downloadId,entry.decisionToken,{cancel:true}).status,"cancelled")
            tryVerify(function(){return entry.finished && !owner.pendingPageSaveForView(page)},5000)
            compare(entry.state,WebEngineDownloadRequest.DownloadCancelled)
            verify(!Fixture.fileExists(entry.path))
        }
        function test_sourceNavigationInvalidatesDocumentDependentSave() {
            var historyBefore = FeriaAccount.browserDownloadHistory.length
            compare(page.pageTools.savePage().status,"dispatched")
            var entry=waitOffer(1)
            evaluate("location.href='/save-page';true")
            // Qt can announce LoadStarted before navigationRequested. That
            // invalidates/cancels serialization; generation staying unchanged
            // is not a supported navigation-prevention guarantee.
            tryVerify(function(){return entry.finished && !owner.pendingPageSaveForView(page)},5000)
            compare(owner.nativeSaveOutcome.status,"invalidated")
            compare(entry.state,WebEngineDownloadRequest.DownloadCancelled)
            verify(!Fixture.fileExists(entry.path))
            compare(FeriaAccount.browserDownloadHistory.length,historyBefore)
        }
        function test_contextButtonPressCannotTargetReplacementContext() {
            for (var name of ["feriaContextSaveResource", "feriaContextOpenLink", "feriaContextCopy"]) {
                var original = context("linkA")
                var button = findChild(contextBar, name)
                verify(button !== null && button.visible && button.enabled)
                page.pageTools.saveOutcome = {status:"not-dispatched"}
                mousePress(button, button.width / 2, button.height / 2, Qt.LeftButton)
                compare(button.contextIdAtPress, original.contextId)
                // This presentation-boundary injection simulates a new native
                // context notification while the actual QML button is held.
                // The separate real A/right-click-B test qualifies native capture.
                var replacement = page.pageTools.captureSaveContext({linkUrl:Fixture.baseUrl + "/download?name=beta.bin",
                    mediaUrl:"",mediaType:ContextMenuRequest.MediaTypeNone,mediaFlags:0})
                verify(replacement.contextId !== original.contextId)
                mouseRelease(button, button.width / 2, button.height / 2, Qt.LeftButton)
                compare(page.pageTools.saveOutcome.status,"invalidated", "Held context action must reject replacement before release")
                compare(owner.items.length,0)
            }
            compare(findChild(contextBar,"feriaContextTarget").textFormat,Text.PlainText)
        }
        function test_contextButtonRetainsOwnerAcrossEqualIds() {
            var original = context("linkA")
            var button = findChild(contextBar,"feriaContextCopy")
            page.pageTools.saveOutcome = {status:"not-dispatched"}
            mousePress(button,button.width/2,button.height/2,Qt.LeftButton)
            var replacement = replacementToolsFactory.createObject(surface,{contextSnapshot:Object.assign({},original)})
            verify(replacement !== null)
            surface.presentedTools = replacement
            compare(contextBar.snapshot.contextId,original.contextId,"Fixture intentionally collides local context IDs")
            mouseRelease(button,button.width/2,button.height/2,Qt.LeftButton)
            compare(replacement.calls,0,"Click must never invoke a replacement context owner")
            compare(page.pageTools.saveOutcome.status,"dispatched","Valid original Copy must still target its original native view")
            surface.presentedTools = null
            replacement.destroy()
        }
        function test_contextButtonDestroyedOwnerFailsClosed() {
            var original = context("linkA")
            var retiring = replacementToolsFactory.createObject(surface,{contextSnapshot:Object.assign({},original)})
            surface.presentedTools = retiring
            var button = findChild(contextBar,"feriaContextCopy")
            page.pageTools.saveOutcome = {status:"not-dispatched"}
            mousePress(button,button.width/2,button.height/2,Qt.LeftButton)
            compare(button.toolsAtPress,retiring)
            retiring.destroy()
            tryCompare(button,"toolsAtPress",null,5000)
            surface.presentedTools = null
            mouseRelease(button,button.width/2,button.height/2,Qt.LeftButton)
            compare(page.pageTools.saveOutcome.status,"not-dispatched","Destroyed original must not fall back to current tools")
            compare(owner.items.length,0)
        }
        function test_narrowContextControlsReserveWrappedHeight() {
            surface.width = 460
            context("linkA")
            tryCompare(contextBar,"width",460,5000)
            verify(waitForRendering(surface))
            console.info("NARROW_CONTEXT_LAYOUT", JSON.stringify({surfaceWidth:surface.width,barWidth:contextBar.width,barHeight:contextBar.height}))
            if (Fixture.evidenceRoot.length) verify(Fixture.captureWindow(surface,"category20-narrow-context-controls.png"))
            var firstY = -1, wrapped = false
            for (var name of ["feriaContextSavePage", "feriaSavePageFormat", "feriaContextSaveResource",
                    "feriaContextOpenLink", "feriaContextCopy", "feriaContextDismiss"]) {
                var control = findChild(contextBar, name)
                verify(control !== null)
                if (!control.visible) continue
                var point = control.mapToItem(contextBar, 0, 0)
                console.info("NARROW_CONTEXT_CONTROL",JSON.stringify({name:name,x:point.x,y:point.y,width:control.width,height:control.height}))
                if (firstY < 0) firstY = point.y
                else if (point.y > firstY + control.height) wrapped = true
                verify(point.x >= 0 && point.x + control.width <= contextBar.width,
                    name + " must fit inside narrow chrome")
                verify(point.y >= 0 && point.y + control.height <= contextBar.height,
                    name + " must fit inside reserved chrome height")
            }
            verify(wrapped,"Narrow controls must wrap to a second row")
            verify(page.y >= contextBar.height)
        }
        function test_pageSaveCommitsSettledNativeBytesExactlyOnce() {
            cleanup()
            initialize(true)
            compare(nativeProfile.offTheRecord, false)
            compare(FeriaAccount.browserDownloadHistory.length, 0)
            compare(page.pageTools.savePage(WebEngineDownloadRequest.MimeHtmlSaveFormat).status, "dispatched")
            var entry = waitOffer(1)
            compare(entry.snapshot().sourceStatus, "exclusive-app-page-save")
            compare(entry.snapshot().source.sessionId, page.accountSessionId)
            var path = accept(entry, "history.mhtml", "recorded")
            compare(FeriaAccount.browserDownloadHistory.length, 1)
            var row = FeriaAccount.browserDownloadHistory[0]
            compare(row.id, entry.downloadId)
            compare(row.path, path)
            compare(row.receivedBytes, entry.request.receivedBytes)
            compare(row.totalBytes, Math.max(0, entry.request.totalBytes))
            verify(row.receivedBytes > 0)
            entry.update(); entry.update()
            compare(FeriaAccount.browserDownloadHistory.length, 1, "Duplicate terminal delivery must not consume another grant")
            compare(FeriaAccount.browserDownloadHistory[0].receivedBytes, row.receivedBytes)
        }
        function test_pageSaveCapabilityReflectsReceivingEngineGate() {
            owner.pageSaveIngressExclusive = false
            context("linkA")
            compare(page.pageTools.canSavePage,false)
            compare(findChild(contextBar,"feriaContextSavePage").enabled,false)
            compare(page.pageTools.savePage().status,"blocked")
            compare(owner.items.length,0)
            owner.pageSaveIngressExclusive = true
        }
        function test_noOwnerNoExplicitPathFallback() {
            page.saveAdmissionOwner=null
            compare(page.pageTools.savePage().status,"unsupported")
            compare(owner.items.length,0)
            page.saveAdmissionOwner=owner
        }
    }
}
