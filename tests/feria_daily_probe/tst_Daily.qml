import QtQuick
import QtQuick.Window
import QtTest
import QtWebEngine
import "../../qml/feria" as Feria

// Set 4 PDF contract: drive the production deck and real QtWebEngine offscreen.
Item {
    id: root
    width: 1060
    height: 760
    Item { id: embedded; x: 25; y: 125; width: 820; height: 580 }
    Feria.FeriaSessionDeck {
        id: deck
        parent: embedded
        anchors.fill: parent
        sessions: Sessions
        accountStore: FeriaAccount
        policy: FeriaBrowserPolicy
        displayed: true
    }
    TestCase {
        id: tests
        name: "FeriaDaily"
        when: windowShown
        property int serial: 0
        SignalSpy { id: privateDownloadSpy; target: deck.privateProfile; signalName: "downloadRequested" }
        SignalSpy { id: printMessageSpy; signalName: "message" }

        function init() {
            failOnWarning(/TypeError|ReferenceError|incompatible arguments|Could not convert argument/)
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 15000)
            Fixture.switchProfile("daily-assert-" + (++serial))
            tryCompare(deck, "quiescing", false, 15000)
            deck.displayed = true
            deck.suppressed = false
            privateDownloadSpy.clear()
            printMessageSpy.target = null
            printMessageSpy.clear()
        }
        function cleanup() {
            printMessageSpy.target = null
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 15000)
            compare(FeriaBrowserPolicy.runtimeViewCount, 0)
            compare(FeriaBrowserPolicy.runtimeProfileCount, 0)
        }
        function descriptor(path, privateMode) {
            return { providerId: "mangaplus", providerName: "Daily PDF fixture",
                url: FixtureUrl + path, startUrl: FixtureUrl + "/a",
                mode: "read", privateBrowsing: !!privateMode }
        }
        function loaded(page, path) {
            tryVerify(function() {
                return page && page.documentReady && !page.loading && !page.replacementLoadPending
                    && String(page.url) === FixtureUrl + path
            }, 15000, "Expected an engine-completed document at " + path)
        }
        function open(path, privateMode) {
            var id = deck.open(descriptor(path, privateMode), true, false, true)
            verify(id.length > 0, "production deck must allocate a browser session")
            tryVerify(function() {
                return privateMode ? deck.privateProfile !== null : deck.sharedProfile !== null
            }, 10000)
            // Profiles are owned by the deck and created lazily. Set the isolated
            // path before any test navigation that could possibly download.
            var profile = privateMode ? deck.privateProfile : deck.sharedProfile
            profile.downloadPath = Fixture.downloadRoot
            tryVerify(function() { return !!deck.browserForSession(id) }, 10000)
            loaded(deck.browserForSession(id), path)
            return id
        }
        function pdfUrl() { return FixtureUrl + "/daily-document.pdf" }
        function attachmentUrl() { return FixtureUrl + "/daily-attachment.pdf" }
        function go(id, path) {
            var page = deck.browserForSession(id)
            verify(deck.navigateSession(id, FixtureUrl + path, 0, null, null),
                "production navigation must accept " + path)
            loaded(page, path)
            return page
        }
        function inlinePdf(id) {
            var before = deck.downloads ? deck.downloads.items.length : 0
            var page = go(id, "/daily-document.pdf")
            wait(250)
            compare(deck.downloads.items.length, before, "inline PDF must not download")
            return page
        }
        function js(page, source) {
            var done = false, result
            verify(page.evaluateJavaScript(source, function(value) {
                result = value
                done = true
            }), "production wrapper must admit a script")
            tryVerify(function() { return done }, 5000)
            return result
        }
        function printBytes(path) {
            var bytes = new Uint8Array(Fixture.downloadedContent(path))
            var magic = bytes.length >= 4
                ? String.fromCharCode(bytes[0], bytes[1], bytes[2], bytes[3]) : ""
            return {length:bytes.length, magic:magic}
        }
        function countVisits(url) {
            return FeriaAccount.browserHistory.filter(function(entry) {
                return String(entry.url) === url
            }).length
        }

        function test_inlinePdfNavigation() {
            var id = open("/a", false)
            var page = inlinePdf(id)
            compare(String(page.url), pdfUrl())
            compare(page.documentReady, true)
            console.log("DAILY_ASSERT_INLINE url=" + String(page.url)
                + " documentReady=" + page.documentReady
                + " downloads=" + deck.downloads.items.length)
        }

        function test_backForwardAcrossInlinePdf() {
            var id = open("/a", false)
            var page = inlinePdf(id)
            tryVerify(function() { return page.navCanGoBack }, 5000)
            verify(deck.back(id), "Back must be accepted from the PDF")
            loaded(page, "/a")
            tryVerify(function() { return page.navCanGoForward }, 5000)
            verify(deck.forward(id), "Forward must be accepted back to the PDF")
            loaded(page, "/daily-document.pdf")
            compare(deck.downloads.items.length, 0)
            console.log("DAILY_ASSERT_PDF_HISTORY_NAV back=/a forward=/daily-document.pdf downloads=0")
        }

        function test_singlePdfHistoryVisit() {
            var id = open("/a", false)
            tryCompare(FeriaAccount.browserHistory, "length", 1)
            inlinePdf(id)
            tryCompare(FeriaAccount.browserHistory, "length", 2)
            compare(countVisits(pdfUrl()), 1, "a PDF is a single canonical history visit")
            compare(String(FeriaAccount.browserHistory[0].url), pdfUrl())
            console.log("DAILY_ASSERT_PDF_VISIT historyLength="
                + FeriaAccount.browserHistory.length + " pdfRows=" + countVisits(pdfUrl()))
        }

        function test_attachmentDownloadsWithoutNavigation() {
            var id = open("/a", false)
            var page = deck.browserForSession(id)
            var before = deck.downloads.items.length
            verify(deck.navigateSession(id, attachmentUrl(), 0, null, null))
            tryVerify(function() { return deck.downloads.items.length === before + 1 }, 10000)
            var item = deck.downloads.items[before]
            tryCompare(item, "completed", true, 15000)
            compare(item.fileName, "daily-attachment.pdf")
            var file = printBytes(item.path)
            verify(file.length > 300, "the attachment must be saved inside Fixture.downloadRoot")
            compare(file.magic, "%PDF")
            tryVerify(function() {
                return !page.loading && String(page.url) === FixtureUrl + "/a"
            }, 10000, "attachment must not replace the source page")
            compare(FeriaAccount.browserHistory.length, 1)
            console.log("DAILY_ASSERT_ATTACHMENT filename=" + item.fileName
                + " path=" + item.path + " bytes=" + file.length
                + " magic=" + file.magic + " page=" + String(page.url))
        }

        function test_privatePdfIsInlineAndUnrecorded() {
            var id = open("/a", true)
            var page = deck.browserForSession(id)
            compare(page.profile.offTheRecord, true)
            compare(FeriaAccount.browserHistory.length, 0)
            var before = privateDownloadSpy.count
            page = go(id, "/daily-document.pdf")
            wait(250)
            compare(privateDownloadSpy.count, before, "a private inline PDF must not download")
            compare(FeriaAccount.browserHistory.length, 0, "private PDF visits are not stored")
            console.log("DAILY_ASSERT_PRIVATE_PDF url=" + String(page.url)
                + " documentReady=" + page.documentReady + " history="
                + FeriaAccount.browserHistory.length + " downloadSignals="
                + (privateDownloadSpy.count - before))
        }

        function test_pluginNavigatorExposure() {
            var id = open("/a", false)
            var page = deck.browserForSession(id)
            var script = "JSON.stringify({count:navigator.plugins.length,"
                + "pdfViewerEnabled:typeof navigator.pdfViewerEnabled==='undefined'"
                + "?'undefined':navigator.pdfViewerEnabled})"
            var enabled = js(page, script)
            compare(page.settings.pluginsEnabled, true)
            page.settings.pluginsEnabled = false
            var generation = page.navigationGeneration
            verify(deck.reload(id))
            tryVerify(function() {
                return page.navigationGeneration > generation && page.documentReady
                    && !page.loading && !page.replacementLoadPending
                    && String(page.url) === FixtureUrl + "/a"
            }, 15000)
            var disabled = js(page, script)
            page.settings.pluginsEnabled = true
            generation = page.navigationGeneration
            verify(deck.reload(id))
            tryVerify(function() {
                return page.navigationGeneration > generation && page.documentReady
                    && !page.loading && !page.replacementLoadPending
                    && String(page.url) === FixtureUrl + "/a"
            }, 15000)
            var restored = js(page, script)
            console.log("DAILY_PLUGIN_NAVIGATOR enabled=" + enabled
                + " disabled=" + disabled + " restored=" + restored)
        }

        function test_pdfPrintObservation() {
            var id = open("/a", false)
            var page = inlinePdf(id)
            var requests = page.browserRequests
            printMessageSpy.target = requests
            printMessageSpy.clear()
            requests.printPage()
            var offered = requests.printTicket !== null
            var path = Fixture.downloadRoot + "/daily-viewer-print.pdf"
            var accepted = offered ? requests.savePrint("file:///" + path) : false
            for (var i = 0; i < 100 && accepted && printMessageSpy.count === 0; ++i)
                wait(200)
            var message = printMessageSpy.count
                ? String(printMessageSpy.signalArguments[printMessageSpy.count - 1][0]) : ""
            var file = printBytes(path)
            // Observation only: do not conflate engine PDF printing with the
            // accepted in-tab viewer/navigation contract.
            console.log("DAILY_PDF_PRINT offered=" + offered + " saveAccepted=" + accepted
                + " response=" + JSON.stringify(message)
                + " bytes=" + file.length + " magic=" + file.magic)
        }
    }
}
