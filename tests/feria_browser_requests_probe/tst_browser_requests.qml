import QtQuick
import QtTest
import QtWebEngine
import "../../qml/feria" as Feria

Item {
    id: stage
    width: 900; height: 700
    Item {
        id: browser
        property bool closingSession: false
        property url url: "https://page.example/start"
        property string printed: ""
        property string accountSessionId: "session-a"
        property string accountVisitToken: "visit-a"
        property var profile: profile
        signal permissionRequested(var permission)
        signal desktopMediaRequested(var request)
        signal certificateError(var error)
        signal selectClientCertificate(var selection)
        signal webAuthUxRequested(var request)
        signal authenticationDialogRequested(var request)
        signal javaScriptDialogRequested(var request)
        signal fileDialogRequested(var request)
        signal navigationRequested(var request)
        signal windowCloseRequested()
        signal printRequested()
        signal pdfPrintingFinished(string path, bool success)
        function printToPdf(path) { printed = path }
    }
    QtObject {
        id: profile
        property bool offTheRecord: false
        property string queriedOrigin: ""
        property var originPermissions: []
        function listPermissionsForOrigin(url) { queriedOrigin = String(url); return originPermissions }
        signal downloadRequested(var request)
    }
    QtObject {
        id: store
        property var browserDownloadHistory: []
        property var grants: ({})
        property int nextGrant: 0
        property int accountEpoch: 0
        property bool visitLive: true
        function authorizeBrowserDownload(sessionId, visitToken) {
            if (!visitLive || sessionId !== "session-a" || visitToken !== "visit-a") return ""
            var token = "grant-" + (++nextGrant)
            var next = Object.assign({}, grants)
            next[token] = {epoch: accountEpoch, sessionId: sessionId, visitToken: visitToken}
            grants = next
            return token
        }
        function cancelBrowserDownload(token) {
            if (!grants[token]) return false
            var next = Object.assign({}, grants)
            delete next[token]
            grants = next
            return true
        }
        function recordBrowserDownload(record) {
            var grant = grants[record.downloadToken]
            if (!grant || grant.epoch !== accountEpoch) return false
            browserDownloadHistory = browserDownloadHistory.concat([record])
            cancelBrowserDownload(record.downloadToken)
            return true
        }
    }
    Feria.FeriaBrowserRequests { id: requests; browser: browser }
    Feria.FeriaBrowserDownloads { id: downloads; profile: profile; accountStore: store }
    SignalSpy { id: notices; target: requests; signalName: "message" }
    ListModel {
        id: screenSources
        ListElement { display: "Display 1" }
        ListElement { display: "Display 2" }
    }
    ListModel {
        id: windowSources
        ListElement { display: "Document window" }
        ListElement { display: "Meeting window" }
    }
    Component {
        id: downloadMock
        QtObject {
            property string downloadDirectory: "C:/Downloads"
            property string downloadFileName: "report (1).pdf"
            property string url: "https://files.example/report.pdf"
            property string mimeType: "application/pdf"
            property real receivedBytes: 0
            property real totalBytes: 120
            property int state: WebEngineDownloadRequest.DownloadRequested
            property bool isPaused: false
            property var view: browser
            readonly property bool isFinished: state === WebEngineDownloadRequest.DownloadCompleted || state === WebEngineDownloadRequest.DownloadCancelled
            property int accepted: 0
            property int canceled: 0
            function accept() { accepted++; state = WebEngineDownloadRequest.DownloadInProgress }
            function cancel() { canceled++; state = WebEngineDownloadRequest.DownloadCancelled }
            function pause() { isPaused = true }
            function resume() { isPaused = false; state = WebEngineDownloadRequest.DownloadInProgress }
        }
    }
    Component {
        id: webAuthMock
        QtObject {
            property int state: WebEngineWebAuthUxRequest.WebAuthUxState.SelectAccount
            property string relyingPartyId: "login.example"
            property var userNames: ["first@example", "second@example"]
            property var pinRequest: ({reason: WebEngineWebAuthUxRequest.PinEntryReason.Challenge,
                minPinLength: 4, error: WebEngineWebAuthUxRequest.PinEntryError.NoError, remainingAttempts: 3})
            property int requestFailureReason: WebEngineWebAuthUxRequest.RequestFailureReason.Timeout
            property string selectedAccount: ""
            property string selectedPin: ""
            property int canceled: 0
            property int retried: 0
            function setSelectedAccount(name) { selectedAccount = name }
            function setPin(pin) { selectedPin = pin }
            function cancel() { canceled++; state = WebEngineWebAuthUxRequest.WebAuthUxState.Cancelled }
            function retry() { retried++; state = WebEngineWebAuthUxRequest.WebAuthUxState.NotStarted }
        }
    }
    TestCase {
        name: "FeriaBrowserRequests"
        when: windowShown
        function permission(origin) {
            return {origin: origin, permissionType: WebEnginePermission.PermissionType.Geolocation,
                isValid: true, granted: 0, denied: 0,
                grant: function() { this.granted++ }, deny: function() { this.denied++ }}
        }
        function init() {
            browser.visible = true; browser.closingSession = false
            browser.url = "https://page.example/start"
            requests.cancelPending(); notices.clear()
            downloads.retire(); downloads.profile = profile; downloads.accountStore = store
            downloads.active = true; downloads.privateBrowsing = false
            profile.offTheRecord = false; store.browserDownloadHistory = []
            store.grants = ({}); store.nextGrant = 0; store.accountEpoch = 0; store.visitLive = true
            profile.queriedOrigin = ""; profile.originPermissions = []
            browser.accountSessionId = "session-a"; browser.accountVisitToken = "visit-a"
        }
        function cleanup() { requests.cancelPending(); downloads.retire() }
        function test_permission_uses_request_origin_and_owner() {
            var first = permission("https://frame.example/"), second = permission("https://other.example/")
            browser.permissionRequested(first); browser.permissionRequested(second)
            compare(requests.current.origin, "https://frame.example")
            requests.answer(requests.current.id, true)
            compare(first.granted, 1); compare(second.granted, 0)
            tryVerify(function() { return requests.current !== null })
            compare(requests.current.origin, "https://other.example")
            requests.answer(requests.current.id, false)
            compare(second.denied, 1)
        }
        function test_origin_labels_hide_credentials_and_keep_host_scheme_port() {
            compare(requests.originLabel("HTTPS://trusted.example:secret@actual.example:8443/path?q=1#part"), "https://actual.example:8443")
            compare(requests.originLabel("https://[::1]:8443?secret=yes"), "https://[::1]:8443")
            compare(requests.certificateHostLabel("user:secret@actual.example:443"), "actual.example:443")
            var request = permission("https://user:secret@actual.example:8443/")
            browser.permissionRequested(request)
            compare(requests.current.origin, "https://actual.example:8443")
        }
        function test_navigation_cancels_queue_and_fences_old_answer() {
            var old = permission("https://old.example"), queued = permission("https://queued.example")
            browser.permissionRequested(old); browser.permissionRequested(queued)
            var oldId = requests.current.id
            browser.navigationRequested({isMainFrame: false})
            compare(old.denied, 0)
            browser.navigationRequested({isMainFrame: true})
            compare(old.denied, 1); compare(queued.denied, 1)
            var fresh = permission("https://fresh.example")
            browser.permissionRequested(fresh)
            compare(requests.answer(oldId, true), false)
            compare(fresh.granted, 0); compare(old.granted, 0)
        }
        function test_close_denies_prompt_and_future_requests() {
            var current = permission("https://current.example")
            browser.permissionRequested(current)
            browser.closingSession = true
            compare(current.denied, 1); compare(requests.current, null)
            var next = permission("https://next.example")
            browser.permissionRequested(next)
            compare(next.denied, 1); compare(next.granted, 0)
        }
        function test_invalid_permission_cannot_be_granted() {
            var request = permission("https://site.example")
            browser.permissionRequested(request); request.isValid = false
            requests.answer(requests.current.id, true)
            compare(request.granted, 0)
        }
        function test_reset_site_permissions_uses_full_native_origin_lookup() {
            browser.url = "https://page.example:8443/path?q=1"
            var row = {isValid: true, resets: 0, reset: function() { this.resets++ }}
            var invalid = {isValid: false, resets: 0, reset: function() { this.resets++ }}
            profile.originPermissions = [row, invalid]
            compare(requests.currentSitePermissions().length, 2)
            compare(profile.queriedOrigin, "https://page.example:8443/path?q=1")
            compare(requests.resetSitePermissions(), 1)
            compare(row.resets, 1); compare(invalid.resets, 0)
            verify(notices.signalArguments[0][0].indexOf("https://page.example:8443") >= 0)
        }
        function test_desktop_requires_explicit_source_then_navigation_cancels() {
            var request = {screensModel: screenSources, windowsModel: windowSources, selected: -1, canceled: 0,
                selectScreen: function(index) { this.selected = index.row },
                selectWindow: function(index) { this.selected = 10 + index.row },
                cancel: function() { this.canceled++ }}
            browser.desktopMediaRequested(request)
            compare(request.selected, -1)
            requests.answer(requests.current.id, true, "window", 1)
            compare(request.selected, 11)
            browser.desktopMediaRequested(request)
            browser.navigationRequested({isMainFrame: true})
            compare(request.canceled, 1)
        }
        function test_certificate_is_rejected_with_site_notice() {
            var error = {url: "https://unsafe.example/page", rejected: 0, rejectCertificate: function() { this.rejected++ }}
            browser.certificateError(error)
            compare(error.rejected, 1); compare(notices.count, 1)
            verify(notices.signalArguments[0][0].indexOf("https://unsafe.example") >= 0)
        }
        // Page dialogs and sign-in are owned by the request queue: Qt's stock
        // delegate never opens, navigation refuses what is pending, and a
        // hidden page is refused outright.
        function test_page_dialogs_and_sign_in_are_owned_fenced_and_refused_when_hidden() {
            function dialog(type) { return {accepted: false, rejected: 0, answer: null, type: type,
                message: "Sure?", defaultText: "", securityOrigin: "https://app.example/page",
                dialogAccept: function(text) { this.answer = text === undefined ? "" : text },
                dialogReject: function() { this.rejected++ }} }
            var confirm = dialog(1)
            browser.javaScriptDialogRequested(confirm)
            compare(confirm.accepted, true, "the stock delegate must not open")
            compare(requests.current.kind, "dialog")
            compare(requests.current.origin, "https://app.example")
            var stale = requests.current.id
            browser.navigationRequested({isMainFrame: true})
            compare(confirm.rejected, 1, "leaving the page refuses its dialog")
            compare(requests.answer(stale, true), false, "an answer for the old page goes nowhere")
            compare(confirm.answer, null)

            var signIn = {accepted: false, rejected: 0, user: null, type: 0, url: "https://user:pw@app.example/private",
                realm: "Members", proxyHost: "",
                dialogAccept: function(user, password) { this.user = user; this.password = password },
                dialogReject: function() { this.rejected++ }}
            browser.authenticationDialogRequested(signIn)
            compare(signIn.accepted, true)
            compare(requests.current.kind, "signin")
            compare(requests.current.origin, "https://app.example", "credentials in the address are not shown")
            compare(requests.answer(requests.current.id, false), true)
            compare(signIn.rejected, 1); compare(signIn.user, null)

            browser.visible = false
            var hidden = dialog(0)
            browser.javaScriptDialogRequested(hidden)
            browser.authenticationDialogRequested(signIn)
            compare(hidden.accepted, true); compare(hidden.rejected, 1); compare(signIn.rejected, 2)
            compare(requests.current, null)
            browser.visible = true
        }
        function test_file_chooser_is_owned_and_withdrawn_on_navigation() {
            var chooser = {accepted: false, rejected: 0, files: null, mode: 0, acceptedMimeTypes: [".png", "image/*", ".JPG"],
                dialogAccept: function(files) { this.files = files }, dialogReject: function() { this.rejected++ }}
            compare(requests.fileFilters(chooser.acceptedMimeTypes)[0], "Suggested files (*.png *.jpg)")
            compare(requests.fileFilters([]).length, 1)
            browser.fileDialogRequested(chooser)
            compare(chooser.accepted, true)
            verify(requests.fileTicket !== null)
            var second = {accepted: false, rejected: 0, mode: 0, acceptedMimeTypes: [],
                dialogAccept: function() {}, dialogReject: function() { this.rejected++ }}
            browser.fileDialogRequested(second)
            compare(second.rejected, 1, "one chooser at a time")
            browser.navigationRequested({isMainFrame: true})
            compare(chooser.rejected, 1)
            compare(requests.finishFiles(["file:///C:/late.png"]), false)
            compare(chooser.files, null)
            browser.fileDialogRequested(chooser)
            compare(requests.finishFiles(["file:///C:/Users/Reader/My%20Art/page.png"]), true)
            compare(chooser.files[0], "C:/Users/Reader/My Art/page.png")
        }
        function test_print_destination_is_fenced_after_navigation() {
            browser.printed = ""
            requests.printTicket = {generation: requests.generation, browser: browser}
            browser.navigationRequested({isMainFrame: true})
            compare(requests.savePrint("file:///C:/saved.pdf"), false)
            compare(browser.printed, "")
            requests.printTicket = {generation: requests.generation, browser: browser}
            verify(requests.savePrint("file:///C:/A%20folder/saved.pdf"))
            compare(browser.printed, "C:/A folder/saved.pdf")
            browser.pdfPrintingFinished(browser.printed, true)
            compare(requests.printingPath, "")
        }
        function test_client_certificate_waits_for_choice_and_cancel_selects_none() {
            var selection = {host: "https://cert.example", certificates: [ {subject:"Person A",issuer:"Issuer"}, {subject:"Person B",issuer:"Issuer"} ],
                selected: -1, none: 0, select: function(index) { this.selected = index }, selectNone: function() { this.none++ }}
            browser.selectClientCertificate(selection)
            compare(selection.selected, -1); compare(requests.current.origin, "https://cert.example")
            requests.answer(requests.current.id, true, "certificate", 1)
            compare(selection.selected, 1); compare(selection.none, 0)
            browser.selectClientCertificate(selection)
            browser.navigationRequested({isMainFrame: true})
            compare(selection.none, 1)
        }
        function test_passkey_account_requires_explicit_valid_selection() {
            var request = createTemporaryObject(webAuthMock, stage)
            browser.webAuthUxRequested(request)
            compare(request.selectedAccount, ""); compare(requests.current.origin, "login.example")
            verify(!requests.submitAuth(requests.current.id, "account", "missing@example"))
            verify(requests.submitAuth(requests.current.id, "account", "second@example"))
            compare(request.selectedAccount, "second@example")
            request.state = WebEngineWebAuthUxRequest.WebAuthUxState.Completed
            compare(requests.current, null); compare(request.canceled, 0)
        }
        function test_passkey_pin_confirmation_and_state_refresh() {
            var request = createTemporaryObject(webAuthMock, stage, {state: WebEngineWebAuthUxRequest.WebAuthUxState.CollectPin})
            request.pinRequest = {reason: WebEngineWebAuthUxRequest.PinEntryReason.Set, minPinLength: 4,
                error: WebEngineWebAuthUxRequest.PinEntryError.NoError, remainingAttempts: 3}
            browser.webAuthUxRequested(request)
            var id = requests.current.id
            verify(!requests.submitAuth(id, "pin", "12", "12"))
            verify(!requests.submitAuth(id, "pin", "1234", "9876")); compare(request.selectedPin, "")
            verify(requests.submitAuth(id, "pin", "1234", "1234")); compare(request.selectedPin, "1234")
            request.state = WebEngineWebAuthUxRequest.WebAuthUxState.FinishTokenCollection
            verify(!requests.submitAuth(id, "pin", "9999", "9999"))
            request.state = WebEngineWebAuthUxRequest.WebAuthUxState.RequestFailed
            verify(requests.submitAuth(id, "retry")); compare(request.retried, 1)
        }
        function test_passkey_navigation_cancels_and_fences_stale_reply() {
            var request = createTemporaryObject(webAuthMock, stage)
            browser.webAuthUxRequested(request)
            var oldId = requests.current.id
            browser.navigationRequested({isMainFrame: true})
            compare(request.canceled, 1)
            var next = createTemporaryObject(webAuthMock, stage)
            browser.webAuthUxRequested(next)
            verify(!requests.submitAuth(oldId, "account", "first@example"))
            compare(next.selectedAccount, "")
            browser.closingSession = true
            compare(next.canceled, 1)
        }
        function test_download_acceptance_is_synchronous_and_preserves_destination() {
            var first = createTemporaryObject(downloadMock, stage)
            var second = createTemporaryObject(downloadMock, stage, {downloadFileName: "second.pdf"})
            profile.downloadRequested(first); profile.downloadRequested(second)
            compare(first.accepted, 1); compare(second.accepted, 1)
            compare(first.downloadDirectory, "C:/Downloads"); compare(first.downloadFileName, "report (1).pdf")
            compare(downloads.items.length, 2)
            browser.navigationRequested({isMainFrame: true})
            compare(first.canceled, 0); compare(second.canceled, 0)
            downloads.items[0].togglePause(); compare(first.isPaused, true)
            downloads.items[0].togglePause(); compare(first.isPaused, false)
        }
        function test_completed_download_records_once_and_private_never_records() {
            var first = createTemporaryObject(downloadMock, stage)
            profile.downloadRequested(first)
            first.receivedBytes = 120; first.state = WebEngineDownloadRequest.DownloadCompleted
            compare(store.browserDownloadHistory.length, 1)
            downloads.items[0].update(); compare(store.browserDownloadHistory.length, 1)
            compare(store.browserDownloadHistory[0].path, "C:/Downloads/report (1).pdf")
            compare(store.browserDownloadHistory[0].sessionId, "session-a")
            compare(store.browserDownloadHistory[0].visitToken, "visit-a")
            compare(store.browserDownloadHistory[0].downloadToken, "grant-1")
            compare(Object.keys(store.grants).length, 0)
            downloads.privateBrowsing = true
            var second = createTemporaryObject(downloadMock, stage)
            profile.downloadRequested(second); second.state = WebEngineDownloadRequest.DownloadCompleted
            compare(store.browserDownloadHistory.length, 1)
            compare(store.nextGrant, 1)
        }
        function test_download_visit_identity_is_captured_before_navigation() {
            var request = createTemporaryObject(downloadMock, stage)
            profile.downloadRequested(request)
            browser.accountSessionId = "session-b"; browser.accountVisitToken = "visit-b"
            store.visitLive = false // Native source Visit has ended when the tab closes.
            request.view = null
            request.state = WebEngineDownloadRequest.DownloadCompleted
            compare(store.browserDownloadHistory.length, 1)
            compare(store.browserDownloadHistory[0].sessionId, "session-a")
            compare(store.browserDownloadHistory[0].visitToken, "visit-a")
        }
        function test_download_resumable_interruption_retains_authorization() {
            var request = createTemporaryObject(downloadMock, stage)
            profile.downloadRequested(request)
            request.state = WebEngineDownloadRequest.DownloadInterrupted
            compare(Object.keys(store.grants).length, 1)
            store.visitLive = false; request.view = null
            downloads.items[0].togglePause()
            compare(request.state, WebEngineDownloadRequest.DownloadInProgress)
            request.state = WebEngineDownloadRequest.DownloadCompleted
            compare(store.browserDownloadHistory.length, 1)
            compare(Object.keys(store.grants).length, 0)
        }
        function test_download_old_account_authorization_cannot_record_in_new_account() {
            var request = createTemporaryObject(downloadMock, stage)
            profile.downloadRequested(request)
            ++store.accountEpoch
            store.grants = ({}) // Native setStoragePath invalidates previous authorizations.
            request.state = WebEngineDownloadRequest.DownloadCompleted
            compare(store.browserDownloadHistory.length, 0)
            compare(downloads.items[0].downloadToken, "")
        }
        function test_download_cancel_releases_authorization() {
            var request = createTemporaryObject(downloadMock, stage)
            profile.downloadRequested(request)
            compare(Object.keys(store.grants).length, 1)
            downloads.items[0].cancel()
            compare(Object.keys(store.grants).length, 0)
            compare(store.browserDownloadHistory.length, 0)
        }
        function test_download_profile_or_store_replacement_cancels_and_fences_completion() {
            var request = createTemporaryObject(downloadMock, stage)
            profile.downloadRequested(request)
            downloads.profile = null
            compare(request.canceled, 1); compare(Object.keys(store.grants).length, 0)
            request.state = WebEngineDownloadRequest.DownloadCompleted
            compare(store.browserDownloadHistory.length, 0)
            downloads.profile = profile
            var next = createTemporaryObject(downloadMock, stage)
            profile.downloadRequested(next)
            downloads.accountStore = null
            compare(next.canceled, 1); compare(Object.keys(store.grants).length, 0)
            next.state = WebEngineDownloadRequest.DownloadCompleted
            compare(store.browserDownloadHistory.length, 0)
        }
        function test_download_without_originating_visit_skips_history() {
            var request = createTemporaryObject(downloadMock, stage, {view: null})
            profile.downloadRequested(request)
            request.state = WebEngineDownloadRequest.DownloadCompleted
            compare(store.browserDownloadHistory.length, 0)
            browser.accountVisitToken = ""
            var noToken = createTemporaryObject(downloadMock, stage)
            profile.downloadRequested(noToken)
            noToken.state = WebEngineDownloadRequest.DownloadCompleted
            compare(store.browserDownloadHistory.length, 0)
        }
        function test_profile_retirement_cancels_all_unfinished_downloads() {
            var first = createTemporaryObject(downloadMock, stage), second = createTemporaryObject(downloadMock, stage)
            profile.downloadRequested(first); profile.downloadRequested(second)
            downloads.retire()
            compare(first.canceled, 1); compare(second.canceled, 1); compare(downloads.items.length, 0)
            compare(Object.keys(store.grants).length, 0)
        }
        function test_retired_profile_rejects_without_retaining_request() {
            downloads.active = false
            var request = createTemporaryObject(downloadMock, stage)
            profile.downloadRequested(request)
            compare(request.accepted, 0); compare(request.canceled, 1); compare(downloads.items.length, 0)
        }
    }
}
