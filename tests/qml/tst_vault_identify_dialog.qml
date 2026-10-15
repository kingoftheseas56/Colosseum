import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as Colosseum
import "../../qml/TheatreApi.js" as TheatreApi

TestCase {
    name: "VaultIdentifyDialog"
    when: windowShown

    Window { id: testWindow; width: 720; height: 520; visible: true }
    Component { id: dialogComp; Colosseum.VaultIdentifyDialog {} }
    property var dialog: null
    SignalSpy { id: chosenSpy; signalName: "identityChosen" }

    readonly property var provider: ({
        search: function(query, kind, done) {
            var q = String(query || "").toLowerCase()
            var rows = q.indexOf("beta") >= 0
                ? [{ source: "IMDB", sourceId: "imdb:ttB", title: "Beta", year: 2002,
                    synopsis: "B", coverUrl: "cover-b", world: "Theatre" }]
                : [{ source: "IMDB", sourceId: "imdb:ttA", title: "Alpha", year: 2001,
                    synopsis: "A", coverUrl: "cover-a", world: "Theatre" }]
            if (done) done(rows)
            return rows
        }
    })

    function init() {
        dialog = dialogComp.createObject(testWindow, {
            groupKey: "series-1", titleText: "Alpha", kind: "video", searchProvider: provider
        })
        verify(dialog !== null)
        chosenSpy.target = dialog
        dialog.open()
        wait(40)
    }
    function cleanup() {
        chosenSpy.target = null
        if (dialog) dialog.destroy()
        dialog = null
    }

    function test_typing_populates_results_and_use_this_emits_chosen_identity() {
        verify(dialog.visible)
        verify(findText(dialog.contentItem, "Identify this Vault folder") !== null)
        var field = findChild(dialog.contentItem, "vaultIdentifyQuery")
        verify(field !== null)
        field.text = "Beta"
        wait(40)
        var use = findChild(dialog.contentItem, "vaultIdentifyUse_0")
        verify(use !== null)
        mouseClick(use)
        compare(chosenSpy.count, 1)
        compare(chosenSpy.signalArguments[0][0], "series-1")
        compare(chosenSpy.signalArguments[0][1].sourceId, "imdb:ttB")
    }

    function test_cancel_closes_without_choice() {
        var cancel = findChild(dialog.contentItem, "vaultIdentifyCancel")
        verify(cancel !== null)
        mouseClick(cancel)
        verify(!dialog.visible)
        compare(chosenSpy.count, 0)
    }

    function test_video_fallback_is_fixture_injected_and_not_live() {
        var requested = ""
        TheatreApi.setRequestAdapter(function(url, done) {
            requested = url
            done({ metas: [{ id: "ttC", name: "Cinemeta Miss", releaseInfo: "2024",
                            poster: "https://live.metahub.space/poster/small/ttC/img" }] })
        })
        var got = null
        TheatreApi.searchTitle("Cinemeta Miss", function(rows) { got = rows })
        verify(got !== null)
        compare(got.length, 1)
        compare(got[0].id, "ttC")
        verify(requested.indexOf("search=Cinemeta%20Miss") >= 0)
        TheatreApi.resetRequestAdapter()
    }

    // The kind -> catalogue routing the browse face's identify gesture depends on. VaultPage's
    // identifyBrowseRow() used to hard-code kind = "" (browseAt()'s rows carried no kind), and ""
    // is exactly the value searchNow() falls through to ComicsCatalog/MalCatalog on — so
    // identifying a MOVIE from the browse grid searched comic and manga catalogues and never
    // reached IMDb. Now that the C++ projection carries the stored kind, this pins the dialog end
    // of that contract in both directions.
    //
    // Deliberately WITHOUT searchProvider: the injected provider seam short-circuits searchNow()'s
    // own branching, which is the thing under test. ImdbCatalog/ComicsCatalog/MalCatalog are app
    // context properties that do not exist in a Quick Test, so the video branch falls through to
    // TheatreApi (request-adapter injected, never live) and the kindless branch reaches neither.
    function test_kind_routes_video_to_imdb_and_empty_kind_never_reaches_it() {
        var urls = []
        TheatreApi.setRequestAdapter(function(url, done) {
            urls.push(url)
            done({ metas: [{ id: "ttV", name: "Seeded Film", releaseInfo: "1999" }] })
        })

        var video = dialogComp.createObject(testWindow, {
            groupKey: "browse-row-1", titleText: "Seeded Film", kind: "video"
        })
        verify(video !== null)
        video.open()
        wait(40)
        verify(urls.length > 0)
        verify(String(urls[0]).indexOf("search=Seeded%20Film") >= 0)
        compare(video.results.length, 1)
        compare(video.results[0].source, "IMDB")
        compare(video.results[0].sourceId, "imdb:ttV")
        video.destroy()

        // Falsifiability: the SAME row title with no kind takes the comic/manga fall-through
        // instead — no IMDb ask at all, and no candidates, since neither catalogue exists here.
        urls = []
        var kindless = dialogComp.createObject(testWindow, {
            groupKey: "browse-row-1", titleText: "Seeded Film", kind: ""
        })
        verify(kindless !== null)
        kindless.open()
        wait(40)
        compare(urls.length, 0)
        compare(kindless.results.length, 0)
        kindless.destroy()

        TheatreApi.resetRequestAdapter()
    }

    function findText(root, wanted) {
        if (!root) return null
        if (root.text === wanted) return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; i++) {
            var found = findText(kids[i], wanted)
            if (found) return found
        }
        return null
    }
    function findChild(root, wanted) {
        if (!root) return null
        if (root.objectName === wanted) return root
        var kids = root.children || []
        for (var i = 0; i < kids.length; i++) {
            var found = findChild(kids[i], wanted)
            if (found) return found
        }
        return null
    }
}
