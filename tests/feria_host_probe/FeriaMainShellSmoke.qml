// Run with the assembled colosseum executable and a fresh COLOSSEUM_APPDATA_TAG.
// Uses real Main Loaders and FeriaAccount; never use a user's normal profile.
import QtQuick
import QtQuick.Window
import QtTest
import QtCore
import "../../qml" as App

App.Main {
    id: shell
    property bool smokeCompleted: false
    property int smokeFailures: 0
    property int smokeChecks: 0
    readonly property string smokeTitleId: "film:feria-abc-shell"

    function find(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        var children = item.children || []
        for (var i = 0; i < children.length; ++i) {
            var found = find(children[i], name)
            if (found) return found
        }
        return null
    }
    function report(ok, detail) {
        smokeChecks++
        if (!ok) smokeFailures++
        console.log("FERIA_MAIN_SMOKE " + JSON.stringify({success:!!ok,detail:detail}))
    }
    TestCase {
        id: probe
        when: windowShown
        function check(ok, detail) { shell.report(ok, detail); verify(ok, detail) }
        function until(predicate, detail) {
            for (var i = 0; i < 100 && !predicate(); ++i) wait(50)
            check(predicate(), detail)
        }
        function feriaLoader() { return shell.find(shell.contentItem, "feriaLayer") }
        function feria() { return feriaLoader() ? feriaLoader().item : null }
        function home() { return shell.find(feria(), "home-focus-variant") }
        function open() {
            shell.openFeriaPage()
            until(function() { return feria() && feria().lifecycleActive && home() }, "Main creates active Feria")
            mouseMove(shell.contentItem, 1, 1)
            until(function() { return shell.feriaReturnSnapshot === null }, "Main consumes any parked snapshot")
            if (feria().viewState === "home")
                until(function() { return !home().pendingNavigationPosition && !home().pendingLensPosition },
                      "Main Feria Home restoration settles")
        }
        function run() {
            var profileRoot = String(StandardPaths.writableLocation(StandardPaths.AppLocalDataLocation))
            check(profileRoot.indexOf("Colosseum-dltest-feria-abc-") >= 0,
                  "Main smoke has a disposable AppData identity")
            // Activate the real isolated local profile through the normal
            // onboarding API; browser storage is native-owned and read-only.
            AccountController.continueWithoutAccount()
            until(function() { return FeriaBrowserPolicy.storageRoot.length > 0 },
                  "Native local profile enables its own browser storage")
            open()
            // Feed the actual native content store without depending on a live
            // catalogue response or confusing demo ids with canonical ids.
            porticoContent.ingestShelf("feria-abc-shell", {items:[{
                canonicalKey:shell.smokeTitleId,kind:"film",title:"Shell fixture",creator:"",
                year:2026,genres:[],sourceId:"fixture",sourceIds:[],externalIds:{},
                imageUrl:"",canonicalUrl:"",rank:1,extra:{description:"Shell route fixture"}
            }]})
            check(!!feria().titleObj(shell.smokeTitleId), "Native content store supplies a canonical title fixture")
            feria().selectLens("watch")
            until(function() { return !home().pendingLensPosition }, "Watch lens settles")
            feria().openTitle(shell.smokeTitleId)
            feria().openSearch("Dune")
            check(feria().viewState === "search" && feria().searchReturnState === "title",
                  "Main Feria Search retains its title origin")
            var saved = feria().navigationSnapshot()
            shell.openWorld("Biblio")
            until(function() { return !feriaLoader().open && !feriaLoader().item }, "World switch destroys Feria Loader")
            check(shell.feriaReturnSnapshot && shell.feriaReturnSnapshot.query === "Dune",
                  "World switch parks Feria Search")
            open()
            check(feria().viewState === "search" && feria().query === "Dune"
                  && feria().selectedTitle === saved.selectedTitle && feria().lens === "watch",
                  "World return restores Feria lens, title and Search")
            check(shell.feriaReturnSnapshot === null, "Consumed return snapshot is cleared")

            feria().back()
            check(feria().viewState === "title", "Contextual Back returns inside Feria in Main")
            shell.openDownloadsPage()
            until(function() { return !feriaLoader().item }, "Utility departure destroys Feria Loader")
            check(shell.feriaReturnSnapshot && shell.feriaReturnSnapshot.viewState === "title",
                  "Utility departure parks the title route")
            open()
            check(feria().viewState === "title" && feria().selectedTitle === saved.selectedTitle,
                  "Utility return restores its Feria title")

            // A real browser opens a harmless local page, with no provider login.
            feria().openHost("youtube", shell.smokeTitleId, "title", "about:blank")
            var deck = shell.find(shell.contentItem, "feriaSessionDeck")
            until(function() { return deck && deck.activeBrowser !== null },
                  "Provider host creates an actual retained Qt browser")
            var browserId = Sessions.activeId
            var browser = deck.activeBrowser
            shell.openDownloadsPage()
            until(function() { return !feriaLoader().open && Sessions.activeId === "" },
                  "Provider departure parks the browser activity")
            check(deck.entries[browserId] && deck.entries[browserId].browser === browser,
                  "World departure preserves the same live browser instance")
            check(shell.feriaReturnSnapshot.viewState === "title", "Provider departure parks the owning title route")
            open()
            check(feria().viewState === "title" && deck.activeBrowser === null,
                  "Return restores title without activating the parked browser")
            Sessions.switchTo(browserId)
            until(function() { return feria().viewState === "host" && deck.activeBrowser === browser },
                  "Taskbar activation resumes the existing browser")
            feria().browserParkRequested(true)
            check(feria().viewState === "title", "Browser minimize returns to the owning Feria route")
            shell.closeSession(browserId)
            until(function() { return !deck.entries[browserId] }, "Close removes only this browser activity")

            shell.closeFeriaPage()
            until(function() { return !feriaLoader().item }, "Explicit Home disposes Feria")
            check(shell.currentSurface === "Home" && shell.feriaReturnSnapshot === null,
                  "Explicit Home clears parked state")
            open()
            check(feria().viewState === "home", "Explicit Home re-entry starts on Feria Home")

            feria().openSearch("profile-bound query")
            shell.openDownloadsPage()
            until(function() { return !feriaLoader().item }, "Profile-bound route is parked")
            check(shell.feriaReturnSnapshot !== null, "Profile test has a parked snapshot")
            // Exercise Main's real native-signal connection. This does not
            // simulate an authenticated account switch or its storage rebind.
            FeriaAccount.profileChanged()
            check(shell.feriaReturnSnapshot === null, "Native profile signal invalidates parked state")
            open()
            check(feria().viewState === "home", "Profile invalidation starts with Feria Home")
            feria().selectLens("watch")
            home().restoreNavigationSnapshot({lensPositions:{watch:{y:400,rails:{}}}})
            check(home().pendingNavigationPosition !== null, "Active profile test has pending restoration")
            FeriaAccount.profileChanged()
            check(home().pendingNavigationPosition === null && feria().viewState === "home",
                  "Native profile signal cancels active restoration")
            shell.smokeCompleted = true
        }
    }
    Timer {
        interval: 1500; running: true
        onTriggered: {
            try { probe.run() }
            finally {
                shell.report(shell.smokeCompleted, "Main shell smoke reached every acceptance assertion")
                console.log("FERIA_MAIN_SUMMARY " + JSON.stringify({checks:shell.smokeChecks,failures:shell.smokeFailures}))
                Qt.exit(shell.smokeFailures ? 1 : 0)
            }
        }
    }
    Timer {
        interval: 60000; running: true
        onTriggered: { shell.report(false, "Main shell smoke timed out"); Qt.exit(1) }
    }
}
