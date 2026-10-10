import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtTest
import QtWebEngine
import "../../qml/feria" as Feria

// Temporary TASK-0004 experimental probe; never touches the clipboard.
// Page JavaScript below is for setup/observation, not for invoking edit actions.
Item {
    id: root
    width: 1060; height: 760
    Item { id: embedded; x: 25; y: 125; width: 820; height: 580 }
    Feria.FeriaSessionDeck {
        id: deck; parent: embedded; anchors.fill: parent
        sessions: Sessions; accountStore: FeriaAccount
        policy: FeriaBrowserPolicy; displayed: true
    }
    TestCase {
        name: "FeriaEditFocusExperiment"
        when: windowShown
        function init() {
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 15000)
            Fixture.switchProfile("daily-edit-experiment")
            tryCompare(deck, "quiescing", false, 15000)
            deck.displayed = true
            deck.suppressed = false
        }
        function cleanup() {
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 15000)
        }
        function js(page, source) {
            var done = false, result = null
            verify(page.evaluateJavaScript(source, function(value) {
                result = value; done = true
            }), "script admitted only for setup/observation")
            tryVerify(function() { return done }, 5000)
            return result
        }
        function state(page) {
            return js(page, "(function(){var e=document.getElementById('dailyEdit');"
                + "return {hasFocus:document.hasFocus(),"
                + "tag:document.activeElement&&document.activeElement.tagName,"
                + "activeId:document.activeElement&&document.activeElement.id,"
                + "start:e.selectionStart,end:e.selectionEnd,len:e.value.length};})()")
        }
        function fresh(page, id) {
            var prev = page.navigationGeneration
            verify(deck.reload(id))
            tryVerify(function() {
                return page.navigationGeneration > prev && page.documentReady
                    && !page.loading && !page.replacementLoadPending
                    && String(page.url) === FixtureUrl + "/daily"
            }, 15000)
            page.forceActiveFocus()
            return js(page, "(function(){var e=document.getElementById('dailyEdit');"
                + "e.focus();e.setSelectionRange(6,6);"
                + "return {hasFocus:document.hasFocus(),tag:document.activeElement.tagName,"
                + "start:e.selectionStart,end:e.selectionEnd,len:e.value.length};})()")
        }
        function variant(page, id, name) {
            var wins = 0
            for (var i = 1; i <= 10; ++i) {
                var setup = fresh(page, id)
                var pre, post, menu = page.pageMenu
                if (name === "A") {
                    page.forceActiveFocus()
                    pre = state(page)
                    page.triggerWebAction(WebEngineView.SelectAll)
                } else {
                    menu.editDispatchMode = name
                    mouseClick(page, 65, 270, Qt.RightButton)
                    tryCompare(menu, "visible", true, 4000)
                    var entry = menu.entry("feriaPageMenuSelectAll")
                    pre = state(page)
                    if (entry && entry.enabled && entry.visible) mouseClick(entry)
                    else console.log("FERIA_EDIT_MISSING_ENTRY variant=" + name + " iteration=" + i)
                }
                wait(150)
                post = state(page)
                var good = post.start === 0 && post.end === post.len
                if (good) wins++
                console.log("FERIA_EDIT_SAMPLE variant=" + name + " iteration=" + i
                    + " setup=" + JSON.stringify(setup)
                    + " before=" + JSON.stringify(pre)
                    + " after=" + JSON.stringify(post)
                    + " qtFocus=" + page.activeFocus
                    + " menuVisible=" + menu.visible
                    + " success=" + good)
                if (menu.visible) menu.cancel()
            }
            console.log("FERIA_EDIT_COUNT variant=" + name + " success=" + wins + "/10")
            return wins
        }
        function test_measureABCThenConditionalD() {
            var id = deck.open({
                providerId: "mangaplus", providerName: "Daily fixture",
                url: FixtureUrl + "/daily", startUrl: FixtureUrl + "/a",
                mode: "read"
            }, true, false, true)
            verify(id.length > 0)
            tryVerify(function() { return !!deck.sharedProfile }, 10000)
            deck.sharedProfile.downloadPath = Fixture.downloadRoot
            tryVerify(function() { return !!deck.browserForSession(id) }, 10000)
            var page = deck.browserForSession(id)
            tryVerify(function() { return page.documentReady && !page.loading
                && String(page.url) === FixtureUrl + "/daily" }, 15000)
            var a = variant(page, id, "A")
            var b = variant(page, id, "B")
            var c = variant(page, id, "C")
            var d = -1
            if (b !== 10 && c !== 10) d = variant(page, id, "D")
            console.log("FERIA_EDIT_SUMMARY A=" + a + "/10 B=" + b
                + "/10 C=" + c + "/10 D=" + (d < 0 ? "not-run" : d + "/10"))
        }
    }
}
