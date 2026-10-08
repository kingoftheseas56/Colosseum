import QtQuick
import QtTest
import QtWebEngine
import "../../../qml/feria" as Feria

// Actual QtWebEngine downloads against a deterministic loopback HTTP server.
// Generic source attribution is deliberately unavailable on pinned Quick.
// Accepted-transfer source close is safe to exercise; pre-delivery stale-source
// and originless getter races are not run inside this product fixture.
Item {
    id: surface
    width: 1000; height: 700
    Feria.FeriaSessionDeck {
        id: deck
        anchors.fill: parent
        sessions: Sessions; accountStore: FeriaAccount; policy: FeriaBrowserPolicy
        displayed: true
    }
    Item { id: downloadSurface; anchors.fill: parent; visible: deck.downloadsPageOpen }
    Component.onCompleted: deck.attachDownloadsSurface(downloadSurface)
    TestCase {
        name: "FeriaProfileDownloads17To19"
        when: windowShown
        property int serial: 0
        function init() {
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 15000)
            Fixture.reset()
            Fixture.switchProfile("download-case-" + (++serial))
            tryCompare(deck, "quiescing", false, 15000)
            deck.displayed = true
        }
        function cleanup() {
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 15000)
            Fixture.reset()
        }
        function open(privateMode) {
            var id = Sessions.openBrowser({providerId:"mangaplus",providerName:"Fixture",
                title:"Download fixture",mode:"read",url:Fixture.baseUrl + "/save-page",
                privateBrowsing:!!privateMode}, true)
            verify(id.length > 0)
            tryVerify(function() { return deck.entries[id] && deck.entries[id].browser }, 10000)
            var view = deck.entries[id].browser
            tryVerify(function() { return view.documentReady && !view.loading }, 15000)
            view.profile.downloadPath = Fixture.downloadsDirectory
            return id
        }
        function evaluate(view, script) {
            var done = false, result
            verify(view.evaluateJavaScript(script, function(value) { result = value; done = true }))
            tryVerify(function() { return done }, 5000)
            return result
        }
        function start(id, suffix, gate, extra) {
            var view = deck.entries[id].browser
            var owner = view.saveAdmissionOwner
            verify(owner !== null)
            var name = "case-" + serial + "-" + suffix + ".bin"
            var url = Fixture.baseUrl + "/download?name=" + encodeURIComponent(name)
                + (gate ? "&gate=" + encodeURIComponent(gate) : "") + (extra || "")
            evaluate(view, "location.href=" + JSON.stringify(url) + ";true")
            tryVerify(function() { return owner.items.some(function(entry) { return entry.name === name }) }, 15000)
            var entry = owner.items.filter(function(item) { return item.name === name })[0]
            return {owner:owner,entry:entry,name:name,path:Fixture.downloadsDirectory + "/" + name}
        }
        function complete(transfer, gate) {
            if (gate) Fixture.release(gate)
            tryCompare(transfer.entry, "completed", true, 20000)
            tryCompare(transfer.entry, "settlePending", false, 5000)
            tryVerify(function() { return transfer.entry.fileSnapshot.path !== undefined }, 5000)
            verify(Fixture.fileExists(transfer.path))
            compare(Fixture.checksum(transfer.path), Fixture.expectedChecksum(gate || ""))
            compare(transfer.entry.received, Fixture.pathSize(transfer.path))
        }
        function test_profileListenerDeduplicatesConcurrentSpacesAndWindow() {
            var a = open(false), b = open(false), c = open(false)
            var owner = deck.entries[a].browser.saveAdmissionOwner
            compare(owner, deck.entries[b].browser.saveAdmissionOwner)
            compare(owner, deck.entries[c].browser.saveAdmissionOwner)
            verify(deck.moveToDesktop(c, {x:40,y:40,width:800,height:600}, false))
            var first = start(a,"a","multi-a"), second = start(b,"b","multi-b"), third = start(c,"window","multi-window")
            compare(owner.items.length,3)
            compare(owner.rows.length,3)
            var request = first.entry.request
            owner.receive(request) // duplicate real native QObject, no second admission
            compare(owner.items.length,3)
            compare(Fixture.requestCount("multi-a"),1)
            Sessions.switchTo(b)
            compare(owner.rows[0].sourceStatus,"unavailable")
            compare(owner.rows[0].sourceAvailable,false)
            verify(owner.rows[0].request === undefined)
            compare(owner.rows[0].downloadId,first.entry.downloadId)
            complete(first,"multi-a");complete(second,"multi-b");complete(third,"multi-window")
            compare(FeriaAccount.browserDownloadHistory.length,0)
            compare(first.entry.metadataState,"blockedSourceIdentity")
        }
        function test_knownUnknownAndEmptyProgressUsesNativeState() {
            var id = open(false)
            var known = start(id,"known","known")
            tryVerify(function() { return known.entry.received > 0 },10000)
            verify(!known.entry.completed)
            compare(known.entry.total,1024*1024)
            verify(known.owner.hasTransferDependencies)
            known.owner.openPanel()
            waitForRendering(surface)
            if (Fixture.evidenceRoot.length) verify(Fixture.captureWindow(surface,"downloads-known-active.png"))
            complete(known,"known")
            var unknown = start(id,"unknown","unknown","&unknown=1")
            tryVerify(function() { return unknown.entry.received > 0 },10000)
            verify(unknown.entry.total <= 0)
            verify(unknown.entry.snapshot().indeterminate)
            waitForRendering(surface)
            if (Fixture.evidenceRoot.length) verify(Fixture.captureWindow(surface,"downloads-unknown-active.png"))
            complete(unknown,"unknown")
            var empty = start(id,"empty","","&bytes=0")
            complete(empty,"")
            compare(empty.entry.received,0)
            compare(Fixture.pathSize(empty.path),0)
            compare(empty.entry.state,WebEngineDownloadRequest.DownloadCompleted)
            verify(!empty.entry.snapshot().indeterminate)
            waitForRendering(surface)
            if (Fixture.evidenceRoot.length) verify(Fixture.captureWindow(surface,"downloads-completed.png"))
        }
        function test_delayedDecisionRetainsRequestAndCannotReplay() {
            var id = open(false), owner = deck.entries[id].browser.saveAdmissionOwner
            owner.destinationPolicy = "ask"
            var transfer = start(id,"delayed","delayed")
            var row = transfer.entry.snapshot(), token = row.decisionToken
            compare(transfer.entry.state,WebEngineDownloadRequest.DownloadRequested)
            wait(150)
            verify(transfer.entry.request !== null)
            compare(transfer.entry.request.state,WebEngineDownloadRequest.DownloadRequested)
            verify(owner.hasTransferDependencies)
            compare(owner.decideDownload(row.downloadId,token,{}).status,"dispatched")
            compare(owner.decideDownload(row.downloadId,token,{cancel:true}).status,"invalidated")
            complete(transfer,"delayed")
            owner.destinationPolicy = "autoSave"
        }
        function test_destinationCancelNeverStartsFile() {
            var id = open(false), owner = deck.entries[id].browser.saveAdmissionOwner
            owner.destinationPolicy = "ask"
            var transfer = start(id,"rejected","reject")
            var row = transfer.entry.snapshot()
            compare(owner.decideDownload(row.downloadId,row.decisionToken,{cancel:true}).status,"cancelled")
            tryCompare(transfer.entry,"finished",true,5000)
            compare(transfer.entry.completed,false)
            verify(!Fixture.fileExists(transfer.path))
            compare(FeriaAccount.browserDownloadHistory.length,0)
            owner.destinationPolicy = "autoSave"
        }
        function test_pauseResumeAndCancelAreObservedNativeAttempts() {
            var id = open(false), transfer = start(id,"actions","actions")
            tryVerify(function() { return transfer.entry.received > 0 },10000)
            var owner = transfer.owner, token = transfer.entry.downloadId
            compare(owner.requestDownloadAction(token,"pause-one","pause").status,"dispatched")
            tryCompare(transfer.entry,"paused",true,10000)
            compare(owner.requestDownloadAction(token,"pause-one","pause").status,"dispatched")
            compare(owner.requestDownloadAction(token,"pause-one","cancel").status,"invalidated")
            compare(owner.requestDownloadAction(token,"resume-one","resume").status,"dispatched")
            tryCompare(transfer.entry,"paused",false,10000)
            verify(!transfer.entry.completed)
            complete(transfer,"actions")
            compare(owner.requestDownloadAction(token,"late-cancel","cancel").status,"blocked")
            var cancelled = start(id,"cancelled","cancelled")
            compare(owner.requestDownloadAction(cancelled.entry.downloadId,"cancel-one","cancel").status,"dispatched")
            tryCompare(cancelled.entry,"state",WebEngineDownloadRequest.DownloadCancelled,10000)
            compare(cancelled.entry.completed,false)
            compare(cancelled.entry.snapshot().cleanupConfirmed,false)
        }
        function test_nativeInterruptionUsesRealRangeAndFinalChecksum() {
            var id = open(false), transfer = start(id,"range","range")
            tryVerify(function() { return transfer.entry.received > 0 },10000)
            verify(Fixture.interrupt("range") > 0)
            tryVerify(function() {
                return transfer.entry.state === WebEngineDownloadRequest.DownloadInterrupted
                    || Fixture.requestCount("range") > 1
            },20000)
            var handling
            if (transfer.entry.state === WebEngineDownloadRequest.DownloadInterrupted) {
                handling = "observed-interrupted"
                verify(transfer.entry.canResume, "The observed native interruption must permit a resume attempt for this fixture")
                compare(transfer.owner.requestDownloadAction(transfer.entry.downloadId,"range-resume","resume").status,"dispatched")
            } else handling = "engine-automatic-retry"
            complete(transfer,"range")
            var requests = Fixture.requestLog().filter(function(row) { return row.gate === "range" })
            console.log("FERIA_RANGE_EVIDENCE " + JSON.stringify({handling:handling,requests:requests,
                bytes:Fixture.pathSize(transfer.path),sha256:Fixture.checksum(transfer.path)}))
            verify(requests.some(function(row) { return row.rangeStart > 0 && row.status === 206 }),
                "A real nonzero HTTP Range request is required; replaying a full-body GET is not this assertion")
            compare(transfer.owner.requestDownloadAction(transfer.entry.downloadId,"terminal-resume","resume").status,"blocked")
        }
        function test_serverRejectsRangeWithoutInventedResumeSuccess() {
            var id=open(false), transfer=start(id,"no-range","no-range","&rejectRange=1")
            tryVerify(function() { return transfer.entry.received>0 },10000)
            verify(Fixture.interrupt("no-range")>0)
            tryVerify(function() { return transfer.entry.state===WebEngineDownloadRequest.DownloadInterrupted
                || Fixture.requestCount("no-range")>1 },20000)
            if (transfer.entry.state===WebEngineDownloadRequest.DownloadInterrupted && transfer.entry.canResume)
                compare(transfer.owner.requestDownloadAction(transfer.entry.downloadId,"no-range-attempt","resume").status,"dispatched")
            Fixture.release("no-range")
            tryVerify(function() { return Fixture.requestLog().some(function(row) { return row.gate==="no-range" && row.status===416 }) },20000)
            tryVerify(function() { return transfer.entry.finished || transfer.entry.state===WebEngineDownloadRequest.DownloadInterrupted },20000)
            console.log("FERIA_REJECT_RANGE_EVIDENCE "+JSON.stringify({state:transfer.entry.state,
                finished:transfer.entry.finished,resumeAttemptAvailable:transfer.entry.canResume,requests:Fixture.requestLog()}))
            if (transfer.entry.completed) {
                // The engine may legitimately fall back to a fresh full GET.
                // Count success only with its actual native terminal and bytes.
                complete(transfer,"")
            } else {
                compare(transfer.entry.completed,false)
                compare(transfer.entry.snapshot().canOpen,false)
                compare(transfer.entry.snapshot().resumeGuarantee,false)
                if (transfer.entry.canCancel) {
                    compare(transfer.owner.requestDownloadAction(transfer.entry.downloadId,"stop-no-range","cancel").status,"dispatched")
                    tryCompare(transfer.entry,"state",WebEngineDownloadRequest.DownloadCancelled,5000)
                }
                compare(transfer.entry.snapshot().cleanupConfirmed,false)
            }
        }
        function test_acceptedTransferSurvivesOriginalSpaceClose() {
            var id = open(false), transfer = start(id,"source-close","source-close")
            tryVerify(function() { return transfer.entry.received > 0 },10000)
            var originalId = transfer.entry.downloadId
            Sessions.close(id)
            tryVerify(function() { return !deck.entries[id] },10000)
            compare(transfer.entry.downloadId,originalId)
            verify(transfer.owner.hasTransferDependencies)
            complete(transfer,"source-close")
            compare(transfer.entry.metadataState,"blockedSourceIdentity")
        }
        function test_lastPrivateSpaceRetainsTransferThenSettledFileOnly() {
            var id = open(true), transfer = start(id,"private-close","private-close")
            tryVerify(function() { return transfer.entry.received > 0 },10000)
            var profile = deck.privateProfile, token = transfer.entry.downloadId
            transfer.owner.openPanel()
            Sessions.close(id)
            tryVerify(function() { return !deck.entries[id] },10000)
            compare(deck.privateProfile,profile)
            verify(transfer.owner.hasTransferDependencies)
            complete(transfer,"private-close")
            tryCompare(deck,"privateProfile",null,10000)
            compare(transfer.owner.rows.length,1)
            compare(transfer.entry.request,null)
            compare(transfer.entry.owningProfile,null)
            compare(transfer.owner.rows[0].downloadId,token)
            verify(transfer.owner.resolveDownloadFile(token).path !== undefined)
            compare(FeriaBrowserPolicy.downloadFileStatus(transfer.owner,token).status,"available")
            compare(FeriaAccount.browserDownloadHistory.length,0)
            verify(Fixture.fileExists(transfer.path))
        }
        function test_accountRetirementInvalidatesOldRowsAndFileActions() {
            var id = open(false), transfer = start(id,"account-a","account-a")
            complete(transfer,"account-a")
            var owner=transfer.owner, token=transfer.entry.downloadId
            Fixture.switchProfile("download-account-b-"+serial)
            tryCompare(deck,"quiescing",false,15000)
            compare(owner.rows.length,0)
            compare(FeriaBrowserPolicy.downloadFileStatus(owner,token).status,"invalidated")
            compare(FeriaAccount.browserDownloadHistory.length,0)
            verify(Fixture.fileExists(transfer.path))
        }
    }
}
