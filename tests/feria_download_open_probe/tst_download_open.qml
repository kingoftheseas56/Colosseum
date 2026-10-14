import QtQuick
import QtTest
import QtWebEngine
import "../../qml/feria" as Feria

Item {
    id: stage
    width: 900; height: 700
    Component {
        id: browserFactory
        Feria.FeriaQtWebEngine {
            width: 900; height: 700
            sourceUrl: FixtureUrl + "/hosting"
            profilePath: Fixture.downloadRoot + "/profile"
            appLayout: false; appKeyboard: false; observationEnabled: false
        }
    }
    Component { id: privateFactory; WebEngineProfile { offTheRecord: true } }
    Component {
        id: downloadsFactory
        Feria.FeriaBrowserDownloads { accountStore: FeriaAccount; runtime: FeriaBrowserPolicy.runtime }
    }
    TestCase {
        name: "FeriaDownloadOpening"
        when: windowShown
        property var browser: null
        property var downloads: null
        property var privateProfile: null
        function init() {
            failOnWarning(/TypeError|ReferenceError|Cannot anchor/)
            Fixture.switchProfile("download-opening")
            verify(FeriaAccount.clearBrowserDownloads())
        }
        function makeBrowser(isPrivate) {
            if (isPrivate) privateProfile = privateFactory.createObject(stage)
            browser = browserFactory.createObject(stage, {sharedProfile: privateProfile,
                accountSessionId: "download-opening", accountVisitToken: FeriaAccount.beginSessionVisit("download-opening", {pk:"youtube",privateBrowsing:!!isPrivate})})
            verify(browser !== null)
            browser.profile.downloadPath = Fixture.downloadRoot
            downloads = downloadsFactory.createObject(stage, {profile:browser.profile,privateBrowsing:!!isPrivate})
            verify(downloads !== null)
            verify(FeriaAccount.watchBrowserDownloads(browser.profile, FeriaBrowserPolicy.runtime))
            tryCompare(browser, "documentReady", true, 15000)
        }
        function download(name) {
            browser.evaluateJavaScript("var a=document.createElement('a');a.href='/download';a.download=" + JSON.stringify(name) + ";document.body.appendChild(a);a.click();", function(){})
            tryCompare(downloads, "items", downloads.items, 100) // property synchronization only
            tryVerify(function(){return downloads.items.length === 1}, 5000)
            const item = downloads.items[0]
            tryCompare(item, "completed", true, 10000)
            tryVerify(function(){return item.openingAuthority.length > 0}, 5000)
            compare(String(Fixture.downloadedContent(item.path)), "Colosseum browser download fixture\n")
            return item
        }
        function cleanup() {
            if (downloads) { downloads.retire(); downloads.destroy(); downloads = null }
            if (browser) {
                browser.prepareForClose(false)
                tryCompare(browser, "pendingJavaScript", 0, 10000)
                tryCompare(browser, "destructionNotified", true, 10000)
                browser.destroy(); browser = null
            }
            if (privateProfile) { privateProfile.destroy(); privateProfile = null }
            tryCompare(FeriaBrowserPolicy, "runtimeViewCount", 0, 10000)
            tryCompare(FeriaBrowserPolicy, "runtimeProfileCount", 0, 10000)
        }
        function test_engineCompletionAndInternetMarkingWithoutOpening() {
            makeBrowser(false)
            const item = download("harmless.txt")
            tryCompare(FeriaAccount, "browserDownloadHistory", FeriaAccount.browserDownloadHistory, 100)
            tryVerify(function(){return FeriaAccount.browserDownloadHistory.length === 1}, 5000)
            const row = FeriaAccount.browserDownloadHistory[0]
            compare(row.id, item.openingAuthority)
            verify(row.downloadSnapshot.path.length > 0)
            const prepared = FeriaAccount.prepareBrowserDownloadOpen(row.id, FeriaAccount.browserGeneration)
            verify(prepared.ok); compare(prepared.requiresConfirmation, false)
            compare(FeriaAccount.finishBrowserDownloadOpen(prepared.ticket, false).reason, "canceled")
            if (Qt.platform.os === "windows") {
                const zone = String(Fixture.downloadedContent(item.path + ":Zone.Identifier"))
                verify(/ZoneId=3/.test(zone), "Observe the actual engine's existing Internet-zone marking")
                console.log("DOWNLOAD_MARKING " + JSON.stringify({zoneId:3,source:FixtureUrl,recorded:true,opened:false}))
            }
        }
        function test_privateCompletionIsEphemeralAndRiskCancelDoesNotOpen() {
            makeBrowser(true)
            const item = download("invoice.pdf.exe") // harmless ASCII fixture bytes
            compare(FeriaAccount.browserDownloadHistory.length, 0)
            const prepared = FeriaAccount.prepareBrowserDownloadOpen(item.openingAuthority, FeriaAccount.browserGeneration, true)
            verify(prepared.ok); verify(prepared.requiresConfirmation)
            verify(prepared.warning.indexOf("program") >= 0)
            compare(FeriaAccount.finishBrowserDownloadOpen(prepared.ticket, false).reason, "canceled")
            downloads.openDownload(item.openingAuthority, true)
            verify(downloads.opening !== null)
            downloads.finishOpening(false)
            compare(downloads.opening, null)
            compare(FeriaAccount.browserDownloadHistory.length, 0)
        }
        function test_historicalRiskConfirmationCancelDoesNotOpen() {
            makeBrowser(false)
            const item = download("invoice.pdf.exe")
            tryVerify(function(){return FeriaAccount.browserDownloadHistory.length === 1}, 5000)
            const row = FeriaAccount.browserDownloadHistory[0]
            compare(row.id, item.openingAuthority)
            downloads.openDownload(row.id, false)
            verify(downloads.opening !== null)
            verify(downloads.opening.requiresConfirmation)
            downloads.finishOpening(false)
            compare(downloads.opening, null)
            compare(FeriaAccount.browserDownloadHistory.length, 1)
        }
    }
}
