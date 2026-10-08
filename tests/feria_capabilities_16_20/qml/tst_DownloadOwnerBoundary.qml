import QtQuick
import QtTest
import QtWebEngine
import "../../../qml/feria" as Feria

// MODELED ENGINE boundary, genuine QObject lifetime/signals and actual owner.
// This does not establish native request source provenance or engine behavior.
Item {
    id: surface
    width: 800; height: 600
    property var owner: null
    property var profile: null
    QtObject {
        id: metadata
        property var browserDownloadHistory: []
        property string outcome: "recorded"
        property int attempts: 0
        property int releases: 0
        property var saved: null
        function authorizeBrowserDownload(session,visit,binding) { return "modeled-grant" }
        function cancelBrowserDownload(token) { releases++; return true }
        function recordBrowserDownloadResult(row) {
            attempts++
            if (outcome === "recorded") saved = row
            return {status:outcome}
        }
    }
    QtObject {
        id: modeledPolicy
        property bool rejectDestination: false
        property string storageRoot: "/modeled"
        function registerDownloadOwner(owner,store) { return true }
        function unregisterDownloadOwner(owner) { return true }
        function downloadOwnerScope(owner) { return {accountGeneration:"1",storageGeneration:"1"} }
        function requestBelongsToProfile(request,profile) { return true }
        function admitDownloadDestination(directory,name) { return rejectDestination ? {status:"rejected"}
            : {status:"admitted",directory:directory,fileName:name,path:directory+"/"+name} }
        function completedDownloadFile(admission) { return {state:"completed",path:admission.path} }
    }
    Component {
        id: browserFactory
        QtObject {
            property var profile: surface.profile
            property int navigationGeneration: 1
            property bool rendererLost: false
            property bool closingSession: false
            property string accountSessionId: "modeled-space"
            property string accountVisitToken: "modeled-visit"
            property var saveBinding: ({accountEpoch:1,profileId:"model-profile",spaceId:"modeled-space",
                pageGeneration:1,documentGeneration:1,hostId:"embedded",hostGeneration:1})
        }
    }
    Component {
        id: profileFactory
        QtObject { property bool offTheRecord: false; signal downloadRequested(var request) }
    }
    Component {
        id: ownerFactory
        Feria.FeriaBrowserDownloads { profile: null; accountStore: metadata; requestLifetimeObserver: FeriaBrowserPolicy }
    }
    Component {
        id: requestFactory
        QtObject {
            property var sourceSnapshot: ({sessionId:"modeled-space",visitToken:"modeled-visit"})
            property string downloadFileName: "modeled.bin"
            property string downloadDirectory: "/modeled"
            property string url: "https://example.test/file"
            property string mimeType: "application/octet-stream"
            property int state: WebEngineDownloadRequest.DownloadRequested
            property bool isFinished: false
            property bool isPaused: false
            property bool isSavePageDownload: false
            property int savePageFormat: WebEngineDownloadRequest.UnknownSaveFormat
            property string interruptReasonString: ""
            property int interruptReason: 0
            property real receivedBytes: 16
            property real totalBytes: 100
            function accept() { state = WebEngineDownloadRequest.DownloadInProgress }
            function cancel() {
                if (state === WebEngineDownloadRequest.DownloadCompleted) return
                state = WebEngineDownloadRequest.DownloadCancelled
                isFinished = true
            }
            function pause() { isPaused = true }
            function resume() { if (!isFinished) { state = WebEngineDownloadRequest.DownloadInProgress; isPaused = false } }
        }
    }
    TestCase {
        name: "FeriaDownloadOwnerModeledQObjectBoundary"
        when: windowShown
        function init() {
            metadata.outcome="recorded";metadata.attempts=0;metadata.releases=0;metadata.saved=null;modeledPolicy.rejectDestination=false
            profile=profileFactory.createObject(surface)
            owner=ownerFactory.createObject(surface,{profile:profile,
                sourceResolver:function(request) { return request.sourceSnapshot }})
            verify(owner !== null)
        }
        function cleanup() {
            if (owner) { owner.retire("test-retirement");owner.destroy();owner=null }
            if (profile) { profile.destroy();profile=null }
            wait(0)
        }
        function offer(properties) {
            var request=requestFactory.createObject(profile,properties || {})
            verify(request !== null)
            profile.downloadRequested(request)
            compare(owner.items.length,1)
            tryCompare(owner.items[0],"settlePending",false,1000)
            return request
        }
        function pageIntent(browser) {
            owner.pageSaveIngressExclusive=true
            return owner.beginNativeSave(browser,{kind:"page",format:0,operationId:"modeled-page-save",binding:browser.saveBinding})
        }
        function test_unknownFormatResourceCannotStealPagePackageSlot() {
            var browser=browserFactory.createObject(surface), token=pageIntent(browser)
            verify(token.length>0)
            var resource=requestFactory.createObject(profile,{isSavePageDownload:true,savePageFormat:-1})
            profile.downloadRequested(resource)
            compare(resource.state,WebEngineDownloadRequest.DownloadCancelled)
            compare(owner.items.length,0)
            compare(owner.pageSaveIntent.token,token)
            compare(owner.pageSaveIntent.downloadId,"")
            var pageRequest=requestFactory.createObject(profile,{isSavePageDownload:true,savePageFormat:0})
            profile.downloadRequested(pageRequest)
            compare(owner.items.length,1)
            compare(owner.items[0].source.sourceStatus,"exclusive-app-page-save")
            pageRequest.state=WebEngineDownloadRequest.DownloadCompleted;pageRequest.isFinished=true
            tryCompare(owner,"pageSaveIntent",null,1000)
            browser.destroy()
        }
        function test_unknownInterruptionReasonUsesSafeFallbackWithoutNativePolicy() {
            var request = offer({interruptReason:38,interruptReasonString:"Unsafe native getter text"})
            tryCompare(owner.items[0],"interruption",qsTr("Download interrupted (reason %1).").arg(38),1000)
            request.interruptReason = 0
            tryCompare(owner.items[0],"interruption","",1000)
        }
        function test_postDispatchRehostKeepsOriginalDocumentSource() {
            var browser=browserFactory.createObject(surface), token=pageIntent(browser)
            verify(token.length>0)
            browser.saveBinding=Object.assign({},browser.saveBinding,{hostId:"window",hostGeneration:2})
            var request=requestFactory.createObject(profile,{isSavePageDownload:true,savePageFormat:0})
            profile.downloadRequested(request)
            compare(owner.items.length,1)
            compare(owner.items[0].source.hostId,"embedded")
            request.state=WebEngineDownloadRequest.DownloadCompleted;request.isFinished=true
            tryCompare(owner,"pageSaveIntent",null,1000)
            browser.destroy()
        }
        function test_rejectedDestinationReleasesOnlyAfterObservedWrapperTerminal() {
            owner.policy=modeledPolicy
            var browser=browserFactory.createObject(surface), token=pageIntent(browser)
            verify(token.length>0)
            modeledPolicy.rejectDestination=true
            var request=requestFactory.createObject(profile,{isSavePageDownload:true,savePageFormat:0})
            profile.downloadRequested(request)
            compare(owner.items.length,0)
            verify(owner.pageSaveIntent!==null)
            compare(pageIntent(browser),"")
            tryCompare(owner,"pageSaveIntent",null,1000)
            compare(owner.nativeSaveOutcome.status,"rejected")
            compare(owner.nativeSaveOutcome.nativeState,WebEngineDownloadRequest.DownloadCancelled)
            modeledPolicy.rejectDestination=false
            verify(pageIntent(browser).length>0)
            owner.retire("test-retirement")
            browser.destroy()
        }
        function test_missingLifetimeObserverRejectsWithoutRetainingRequest() {
            owner.requestLifetimeObserver=null
            var request=requestFactory.createObject(profile)
            profile.downloadRequested(request)
            compare(owner.items.length,0)
            compare(request.state,WebEngineDownloadRequest.DownloadCancelled)
            compare(owner.lastAdmissionFailure,"lifetime-observer-unavailable")
        }
        function test_genuineQObjectDisappearanceFailsWithoutInventingNativeCancel() {
            var request=offer({}), id=owner.items[0].downloadId
            compare(owner.rows[0].terminal,"")
            request.destroy()
            tryVerify(function() { return owner.rows.length===1 && owner.rows[0].requestLost },1000)
            compare(owner.rows[0].downloadId,id)
            compare(owner.rows[0].terminal,"failed")
            compare(owner.rows[0].terminalOutcome,"failed")
            compare(owner.rows[0].indeterminate,false)
            compare(owner.rows[0].nativeState,WebEngineDownloadRequest.DownloadInProgress)
            compare(owner.rows[0].cleanupConfirmed,false)
            compare(owner.rows[0].canPause,false)
            compare(owner.rows[0].canResume,false)
            compare(owner.rows[0].canOpen,false)
            compare(owner.hasTransferDependencies,false)
        }
        function test_completedIntentionalDetachDoesNotBecomeLost() {
            owner.privateBrowsing=true
            var request=offer({}), entry=owner.items[0]
            request.state=WebEngineDownloadRequest.DownloadCompleted
            request.isFinished=true
            tryCompare(entry,"recorded",true,1000)
            entry.fileSnapshot={state:"completed",path:"/modeled/modeled.bin"}
            verify(owner.detachCompletedProfile())
            owner.profile=null
            request.destroy();wait(0)
            compare(owner.rows[0].terminal,"succeeded")
            compare(owner.rows[0].requestLost,false)
            compare(owner.rows[0].nativeAvailable,false)
            verify(owner.resolveDownloadFile(entry.downloadId).path.length>0)
        }
        function test_cancelledWrapperThenLateNativeCompleteCannotRegainAuthority() {
            var request=offer({}), entry=owner.items[0]
            request.state=WebEngineDownloadRequest.DownloadInterrupted
            compare(owner.requestDownloadAction(entry.downloadId,"cancel","cancel").status,"dispatched")
            // Deliberately model Qt's documented interrupted wrapper cancellation
            // followed by a later underlying engine update. No fake cleanup ACK.
            request.state=WebEngineDownloadRequest.DownloadCompleted
            request.receivedBytes=100
            request.totalBytes=100
            tryCompare(entry,"settlePending",false,1000)
            compare(owner.rows[0].terminal,"cancelled")
            compare(owner.rows[0].state,WebEngineDownloadRequest.DownloadCancelled)
            compare(owner.rows[0].nativeState,WebEngineDownloadRequest.DownloadCompleted)
            compare(owner.rows[0].completed,false)
            compare(owner.rows[0].canOpen,false)
            compare(owner.rows[0].cleanupConfirmed,false)
            compare(metadata.attempts,0)
        }
        function test_completionCounterSignalsSettleBeforeTypedCommit() {
            var request=offer({}), entry=owner.items[0]
            request.state=WebEngineDownloadRequest.DownloadCompleted
            compare(metadata.attempts,0)
            request.receivedBytes=100;request.totalBytes=100;request.isFinished=true
            tryCompare(metadata,"attempts",1,1000)
            compare(metadata.saved.receivedBytes,100)
            compare(metadata.saved.totalBytes,100)
            entry.update();compare(metadata.attempts,1)
        }
        function test_settledDestinationChangeRejectsFileAndMetadataAuthority() {
            owner.policy=modeledPolicy
            var request=offer({}), entry=owner.items[0]
            request.downloadFileName="unadmitted.bin"
            request.state=WebEngineDownloadRequest.DownloadCompleted;request.isFinished=true
            tryCompare(entry,"settlePending",false,1000)
            compare(entry.completed,true)
            compare(entry.metadataState,"rejected")
            compare(entry.metadataReason,"native-destination-changed")
            compare(metadata.attempts,0)
            compare(entry.snapshot().canOpen,false)
            compare(Object.keys(owner.resolveDownloadFile(entry.downloadId)).length,0)
        }
        function test_typedRetryRetainsGrantButRejectionDoesNot() {
            var request=offer({}), entry=owner.items[0]
            metadata.outcome="retryable"
            request.state=WebEngineDownloadRequest.DownloadCompleted;request.isFinished=true
            tryCompare(entry,"metadataState","retryable",1000)
            compare(entry.downloadToken,"modeled-grant")
            metadata.outcome="recorded"
            compare(owner.retryMetadata(entry.downloadId).status,"recorded")
            compare(metadata.attempts,2)
            compare(entry.downloadToken,"")
        }
    }
}
