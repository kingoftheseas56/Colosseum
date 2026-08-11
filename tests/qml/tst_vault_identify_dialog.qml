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
