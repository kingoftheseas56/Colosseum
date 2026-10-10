import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtTest
import QtWebEngine
import "../../qml/feria" as Feria

// Set 4 discovery only: all page actions pass through production FeriaSessionDeck
// and FeriaQtWebEngine. Missing facilities are logged, never asserted as working.
Item {
    id: root
    width: 1080
    height: 760
    property int contextSerial: 0
    property var lastContext: ({})
    property int optionSignals: 0

    Item { id: embedded; x: 25; y: 125; width: 800; height: 580 }
    Feria.FeriaSessionDeck {
        id: deck
        parent: embedded
        anchors.fill: parent
        sessions: Sessions
        accountStore: FeriaAccount
        policy: FeriaBrowserPolicy
        displayed: true
    }
    // Deliberately separate: whether an actual inspector works is observable
    // independently of whether the product currently offers an entry point.
    WebEngineView {
        id: inspector
        x: 840; y: 125; width: 220; height: 580
        visible: false
    }
    Connections {
        target: deck.activeBrowser
        ignoreUnknownSignals: true
        function onContextMenuRequested(request) {
            root.contextSerial += 1
            root.lastContext = {
                accepted: Boolean(request.accepted),
                linkUrl: String(request.linkUrl),
                linkText: String(request.linkText),
                mediaUrl: String(request.mediaUrl),
                mediaType: Number(request.mediaType),
                selectedText: String(request.selectedText),
                editable: Boolean(request.isContentEditable),
                misspelledWord: String(request.misspelledWord),
                suggestions: Array.from(request.spellCheckerSuggestions || []),
                editFlags: Number(request.editFlags)
            }
            console.log("DAILY_CONTEXT_REQUEST " + JSON.stringify(root.lastContext))
            // The wrapper has already accepted it: inspect only Feria's menu.
        }
        function onOptionsRequested() { root.optionSignals += 1 }
    }

    TestCase {
        id: tests
        name: "FeriaDailyDiscovery"
        when: windowShown
        property int serial: 0

        function init() {
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 15000)
            Fixture.switchProfile("daily-discovery-" + (++serial))
            tryCompare(deck, "quiescing", false, 15000)
            deck.displayed = true
            deck.suppressed = false
            root.contextSerial = 0
            root.lastContext = ({})
            root.optionSignals = 0
        }
        function cleanup() {
            inspector.inspectedView = null
            inspector.visible = false
            deck.startQuiescence(false)
            tryCompare(deck, "quiescing", false, 15000)
            console.log("DAILY_CLEANUP views=" + FeriaBrowserPolicy.runtimeViewCount
                + " profiles=" + FeriaBrowserPolicy.runtimeProfileCount)
        }
        function descriptor(path) {
            return { providerId: "mangaplus", providerName: "Daily fixture",
                url: FixtureUrl + path, startUrl: FixtureUrl + "/daily", mode: "read" }
        }
        function open(path) {
            var id = deck.open(descriptor(path), true, false, true)
            verify(id.length > 0, "production deck must allocate a session")
            tryVerify(function() { return !!deck.browserForSession(id) }, 10000)
            var page = deck.browserForSession(id)
            tryVerify(function() {
                return page.documentReady && !page.loading && !page.replacementLoadPending
                    && String(page.url) === FixtureUrl + path
            }, 15000, "daily loopback HTML page did not load")
            return page
        }
        function js(page, source) {
            var done = false
            var result = null
            verify(page.evaluateJavaScript(source, function(value) {
                result = value
                done = true
            }), "production wrapper must admit a script")
            tryVerify(function() { return done }, 5000)
            return result
        }
        function log(name, details) {
            console.log("DAILY_" + name + " " + JSON.stringify(details))
        }
        function menuSnapshot() {
            var found = []
            try {
                function walk(node, depth) {
                    if (!node || depth > 4 || found.length > 60) return
                    if (node.visible && (node.objectName || node.text !== undefined)) {
                        found.push({object: String(node.objectName || ""),
                            text: node.text === undefined ? "" : String(node.text)})
                    }
                    var children = node.children || []
                    for (var i = 0; i < children.length; ++i) walk(children[i], depth + 1)
                }
                walk(Overlay.overlay, 0)
            } catch (e) {
                found.push({error: String(e)})
            }
            return found
        }
        function rightClick(page, label, x, y) {
            var before = root.contextSerial
            mouseClick(page, x, y, Qt.RightButton)
            wait(350)
            log("CONTEXT_CASE", {name:label, signalled:root.contextSerial > before,
                request: root.contextSerial > before ? root.lastContext : null,
                feriaOpen:page.pageMenu.visible,
                feriaRows:page.pageMenu.visibleEntries(), visibleOverlay:menuSnapshot()})
            keyClick(Qt.Key_Escape)
            wait(120)
        }

        function test_01_addressPolicyThreeEngines() {
            var examples = ["https://example.com/path", "example.com", "localhost:8123",
                "one piece manga arc", "javascript:alert(1)", "file:///C:/secret.txt", ""]
            var engines = ["duckduckgo", "google", "bing"]
            for (var e = 0; e < engines.length; ++e)
                for (var i = 0; i < examples.length; ++i) {
                    var value = String(FeriaBrowserPolicy.omniboxUrl(examples[i], engines[e]))
                    log("ADDRESS", {engine:engines[e], input:examples[i], result:value})
                }
            log("ADDRESS_NAVIGATION_API", {
                bare:String(FeriaBrowserPolicy.navigationUrl("example.com")),
                local:String(FeriaBrowserPolicy.navigationUrl("localhost:8123")),
                javascript:String(FeriaBrowserPolicy.navigationUrl("javascript:alert(1)")),
                file:String(FeriaBrowserPolicy.navigationUrl("file:///C:/secret.txt")),
                empty:String(FeriaBrowserPolicy.navigationUrl(""))})
        }

        function test_02_historyBookmarkSearchBackends() {
            var page = open("/daily")
            var url = FixtureUrl + "/daily"
            var saved = FeriaAccount.setBrowserBookmark(
                url, "Daily fixture bookmark", true, FeriaAccount.browserGeneration)
            var bookmarks = FeriaAccount.searchBrowserBookmarks("daily")
            var history = FeriaAccount.searchBrowserHistory("daily")
            log("SUGGESTION_BACKENDS", {saved:saved, bookmarks:bookmarks.length,
                history:history.length, pageTitle:String(page.title),
                note:"Production toolbar only has address TextField, no typed suggestion UI."})
        }

        function test_03_pageFocusedShortcuts() {
            var page = open("/daily")
            mouseClick(page, 70, 270)
            var focus = js(page, "var e=document.getElementById('dailyEdit'); e.focus(); ({hasFocus:document.hasFocus(),tag:document.activeElement.tagName,id:document.activeElement.id})")
            log("KEY_FOCUS", {dom:focus, qtActiveFocus:page.activeFocus, toolsActive:page.pageTools.active})
            keyClick(Qt.Key_F, Qt.ControlModifier)
            wait(300)
            log("KEY", {shortcut:"Ctrl+F", findOpen:page.pageTools.findOpen})
            page.pageTools.closeFind(false)
            mouseClick(page, 70, 270)
            var original = page.zoomFactor
            keyClick(Qt.Key_Equal, Qt.ControlModifier)
            wait(300)
            log("KEY", {shortcut:"Ctrl+=", before:original, after:page.zoomFactor})
            keyClick(Qt.Key_Minus, Qt.ControlModifier)
            wait(300)
            log("KEY", {shortcut:"Ctrl+-", after:page.zoomFactor})
            page.zoomFactor = 1.5
            keyClick(Qt.Key_0, Qt.ControlModifier)
            wait(300)
            log("KEY", {shortcut:"Ctrl+0", after:page.zoomFactor})
            mouseClick(page, 70, 270)
            keyClick(Qt.Key_F10)
            wait(250)
            log("KEY", {shortcut:"F10", optionsSignals:root.optionSignals})
            log("KEY_LIMIT", {note:"Host-level shortcuts live in PorticoDiscoveryHostView, not in this production deck fixture; their page-focused delivery is not qualified by this case."})
        }

        function test_04_contextMenuAcrossElements() {
            var page = open("/daily")
            rightClick(page, "blank page", 430, 45)
            rightClick(page, "link", 70, 95)
            rightClick(page, "image", 65, 175)
            js(page, "var s=window.getSelection(),r=document.createRange();r.selectNodeContents(document.getElementById('dailyText'));s.removeAllRanges();s.addRange(r);true")
            log("CONTEXT_SELECTION_PRE", {selected:js(page,"window.getSelection().toString()")})
            rightClick(page, "selected text", 85, 350)
            rightClick(page, "editable textarea", 65, 270)
        }

        function visibleMenuItem(label) {
            var found = null
            function walk(node, depth) {
                if (!node || depth > 5 || found) return
                if (node.visible && node.text === label) { found = node; return }
                var children = node.children || []
                for (var i = 0; i < children.length; ++i) walk(children[i], depth + 1)
            }
            try { walk(Overlay.overlay, 0) } catch (e) {}
            return found
        }
        function test_05a_feriaMenuOpenLinkInTab() {
            var page = open("/daily")
            var before = Sessions.browsers().length
            mouseClick(page, 70, 95, Qt.RightButton)
            wait(300)
            var entry = page.pageMenu.entry("feriaPageMenuOpenLink")
            if (entry) {
                mouseClick(entry)
                wait(1500)
            }
            var records = Sessions.browsers()
            log("CONTEXT_ACTION", {name:"open link in new tab",
                source:"Feria", accepted:root.lastContext.accepted,
                offered:!!entry && entry.visible, tabsBefore:before, tabsAfter:records.length,
                urls:records.map(function(r) { return String(r.target ? r.target.url : r.url) })})
        }
        function test_05b_feriaMenuBack() {
            var page = open("/daily")
            var id = deck.sessions.activeId
            verify(deck.navigateSession(id, FixtureUrl + "/b", 0, null, null))
            tryVerify(function() {
                return !page.loading && page.documentReady && String(page.url) === FixtureUrl + "/b"
            }, 15000)
            mouseClick(page, 420, 45, Qt.RightButton)
            wait(350)
            var entry = page.pageMenu.entry("feriaPageMenuBack")
            var enabled = entry ? entry.enabled : false
            if (entry && enabled) {
                mouseClick(entry)
                wait(1000)
            }
            log("CONTEXT_ACTION", {name:"back", source:"Feria",
                accepted:root.lastContext.accepted, offered:!!entry && entry.visible,
                enabled:enabled,
                engineCanGoBack:Boolean(page.canGoBack), wrapperCanGoBack:Boolean(page.navCanGoBack),
                backItemCount:page.navigation.backCount, resultingUrl:String(page.url),
                expectedUrl:FixtureUrl + "/daily"})
        }

        function test_05_spellCheckStateAndTextarea() {
            var page = open("/daily")
            log("SPELL_PROFILE", {enabled:Boolean(page.profile.spellCheckEnabled),
                languages:page.profile.spellCheckLanguages,
                textareaSpellcheck:js(page,"document.getElementById('dailyEdit').spellcheck")})
            mouseClick(page, 70, 270)
            rightClick(page, "misspelling in textarea", 70, 270)
            log("SPELL_LIMIT", {note:"An offscreen context request alone cannot prove a red underline was painted; the profile switch, dictionary availability and suggestions must be considered separately."})
        }

        function pdfAttempt(page, label) {
            // Tests must never let QtWebEngine fall back to the user's Downloads.
            verify(deck.sharedProfile !== null, "the disposable profile is available")
            deck.sharedProfile.downloadPath = Fixture.downloadRoot
            var before = deck.downloads ? deck.downloads.items.length : -1
            var began = deck.navigateSession(deck.sessions.activeId,
                FixtureUrl + "/daily-document.pdf", 0, null, null)
            for (var i = 0; i < 70; ++i) {
                if (!page.loadActive && (String(page.url).indexOf("daily-document.pdf") >= 0
                        || (deck.downloads && deck.downloads.items.length > before))) break
                wait(100)
            }
            wait(1300)
            var after = deck.downloads ? deck.downloads.items.length : -1
            var item = after > before ? deck.downloads.items[after - 1] : null
            var magic = ""
            if (item && item.path && item.completed) {
                try {
                    var data = new Uint8Array(Fixture.downloadedContent(item.path))
                    if (data.length >= 4)
                        magic = String.fromCharCode(data[0],data[1],data[2],data[3])
                } catch (e) {
                    magic = "unreadable:" + String(e)
                }
            }
            log("PDF", {mode:label, navigateAccepted:began,
                pdfViewerEnabled:Boolean(page.settings.pdfViewerEnabled),
                pluginsEnabled:Boolean(page.settings.pluginsEnabled),
                url:String(page.url), title:String(page.title),
                documentReady:page.documentReady, loading:page.loadActive,
                downloadsBefore:before, downloadsAfter:after, fileName:item ? item.name : "",
                fileCompleted:item ? item.completed : null, downloadedMagic:magic,
                rendererPid:Number(page.renderProcessPid)})
        }
        function test_06_pdfBuiltInViewer() {
            var page = open("/daily")
            pdfAttempt(page, "production defaults (inline PDF viewer)")
            var alternate = open("/daily")
            alternate.settings.pluginsEnabled = false
            pdfAttempt(alternate, "test-only pluginsEnabled false (download control)")
            log("PDF_LIMIT", {note:"Viewer toolbar rendering, in-viewer zoom/page/download/print buttons cannot be proved from the URL or title in offscreen mode."})
        }

        function test_07_inspectorProbe() {
            var page = open("/daily")
            log("DEVTOOLS_BEFORE", {pageHasInspector:!!page.devToolsView,
                inspectorUrl:String(inspector.url), settingPresent:inspector.inspectedView === null})
            inspector.visible = true
            inspector.inspectedView = page
            wait(2500)
            log("DEVTOOLS_LINKED", {inspectedViewMatches:inspector.inspectedView === page,
                reverseMatches:page.devToolsView === inspector, inspectorUrl:String(inspector.url),
                inspectorTitle:String(inspector.title), loading:inspector.loading,
                pid:Number(inspector.renderProcessPid)})
            inspector.inspectedView = null
            inspector.visible = false
            log("DEVTOOLS_DETACHED", {inspectedViewNull:inspector.inspectedView === null})
        }
    }
}
