import QtQuick
import QtCore
import QtTest
import "../../qml/feria" as Feria

Item {
    id: scene
    width: 1024; height: 768
    QtObject {
        id: sessions
        property string activeId: "tab-a"
        property var rows: [
            { id: "tab-a", title: "Manga Plus", target: { providerId: "mangaplus", url: "https://mangaplus.shueisha.co.jp/" } },
            { id: "tab-b", title: "Music", target: { providerId: "ytmusic", url: "https://music.youtube.com/", privateBrowsing: true } }
        ]
        property string switched: ""
        signal changed()
        signal activeChangedProp()
        function browsers() { return rows }
        function switchTo(id) { switched = id; activeId = id; activeChangedProp() }
    }
    QtObject {
        id: account
        property var browserBookmarks: []
        property var browserHistory: [{ url: "https://example.com/read", title: "Earlier reading" }]
        property var browserPreferences: ({ restoreTabs: true, freezeBackground: true, searchEngine: "duckduckgo" })
        property var browserTabs: [{ url: "https://example.com/previous" }]
        property var bookmarkAction: null
        property var preferenceAction: null
        property int cleared: 0
        signal changed()
        signal profileChanged()
        // A stand-in for the presentation cases in this file. The store's own
        // search, edit and delete rules are exercised against the real
        // FeriaAccountStore in tests/feria_history_probe.
        property int browserGeneration: 1
        function searchBrowserBookmarks(query) { return browserBookmarks }
        function searchBrowserHistory(query) { return browserHistory }
        function isBrowserBookmarked(url) {
            return browserBookmarks.some(function(row) { return row.url === url })
        }
        function setBrowserBookmark(url, title, saved) {
            bookmarkAction = { url: url, title: title, saved: saved }
            browserBookmarks = saved ? [{ url: url, title: title }] : []
            changed()
            return true
        }
        function removeBrowserBookmark(generation, url) { return setBrowserBookmark(url, "", false) }
        function clearBrowserHistory() { cleared++; browserHistory = []; changed(); return true }
        function setBrowserPreference(key, value) {
            preferenceAction = { key: key, value: value }
            var next = Object.assign({}, browserPreferences)
            next[key] = value
            browserPreferences = next
            changed()
            return true
        }
    }
    QtObject {
        id: deck
        property var sessions: sceneSessions
        property var activeEntry: ({ descriptor: { privateBrowsing: false } })
        property bool quiescing: false
        property var newAction: null
        property string closedId: ""
        property int restores: 0
        property string restoredProvider: ""
        property int restorableTabCount: 2
        property int dismissals: 0
        function newSpace(sourceId, privateBrowsing, activate) {
            newAction = { sourceId: sourceId, privateBrowsing: privateBrowsing, activate: activate }
        }
        function closeTab(id) { closedId = id }
        // The real deck routes activation through the native command door.
        function activate(id) { sessions.switchTo(id); return true }
        function restoreTabs(provider) { restores++; restoredProvider = provider || "" }
        function dismissRestoreTabs() { dismissals++; restorableTabCount = 0 }
    }
    property var sceneSessions: sessions
    Feria.FeriaBrowserMenu {
        id: menu
        parent: scene
        deck: deck
        accountStore: account
        providerId: "mangaplus"
        currentTitle: "Chapter seven"
        currentUrl: "https://mangaplus.shueisha.co.jp/viewer/7"
    }
    SignalSpy { id: navigateSpy; target: menu; signalName: "navigateRequested" }
    SignalSpy { id: addressSpy; target: menu; signalName: "focusAddressRequested" }
    SignalSpy { id: printSpy; target: menu; signalName: "printRequested" }
    SignalSpy { id: downloadSpy; target: menu; signalName: "downloadsRequested" }
    TestCase {
        name: "FeriaBrowserMenu"
        when: windowShown
        function control(name) {
            var result = findChild(menu, name)
            // ListView delegates have a visual parent but are not QObject children of the dialog.
            for (var listName of ["feriaBrowserTabsList", "feriaBrowserBookmarksList", "feriaBrowserHistoryList"]) {
                if (result) break
                var list = findChild(menu, listName)
                if (!list) continue
                for (var i = 0; i < list.count && !result; ++i) {
                    var row = list.itemAtIndex(i)
                    if (row) result = findChild(row, name)
                }
            }
            verify(result !== null, name + " exists")
            return result
        }
        function open(section) {
            menu.openSection(section || "tabs")
            tryCompare(menu, "opened", true)
            waitForRendering(menu.contentItem)
        }
        function init() {
            sessions.activeId = "tab-a"
            sessions.switched = ""
            deck.newAction = null
            deck.closedId = ""
            deck.quiescing = false
            deck.restores = 0
            deck.restoredProvider = ""
            deck.restorableTabCount = 2
            deck.dismissals = 0
            deck.activeEntry = ({ descriptor: { privateBrowsing: false } })
            account.browserBookmarks = []
            account.browserHistory = [{ url: "https://example.com/read", title: "Earlier reading" }]
            account.browserPreferences = ({ restoreTabs: true, freezeBackground: true, searchEngine: "duckduckgo" })
            account.bookmarkAction = null
            account.preferenceAction = null
            account.cleared = 0
            account.changed()
            navigateSpy.clear(); addressSpy.clear(); printSpy.clear(); downloadSpy.clear()
            menu.currentUrl = "https://mangaplus.shueisha.co.jp/viewer/7"
            menu.currentTitle = "Chapter seven"
        }
        function cleanup() { menu.close(); tryCompare(menu, "visible", false) }
        function test_new_private_and_normal_dispatch() {
            open()
            mouseClick(control("feriaBrowserNewPrivate"))
            compare(deck.newAction.privateBrowsing, true)
            compare(deck.newAction.sourceId, "tab-a")
            compare(deck.newAction.activate, true)
            tryCompare(menu, "visible", false)
            open()
            mouseClick(control("feriaBrowserNewTab"))
            compare(deck.newAction.privateBrowsing, false)
        }
        function test_existing_tabs_project_session_owner() {
            open()
            mouseClick(control("feriaBrowserSwitch_tab-b"))
            compare(sessions.switched, "tab-b")
            tryCompare(menu, "visible", false)
            open()
            mouseClick(control("feriaBrowserClose_tab-a"))
            compare(deck.closedId, "tab-a")
        }
        function test_bookmark_explicit_in_private_and_remove() {
            deck.activeEntry = ({ descriptor: { privateBrowsing: true } })
            open("bookmarks")
            verify(control("feriaBrowserPrivacy").text.indexOf("Private") >= 0)
            mouseClick(control("feriaBrowserBookmarkCurrent"))
            compare(account.bookmarkAction.url, menu.currentUrl)
            compare(account.bookmarkAction.title, menu.currentTitle)
            compare(account.bookmarkAction.saved, true)
            mouseClick(control("feriaBrowserBookmarkCurrent"))
            compare(account.bookmarkAction.saved, false)
        }
        function test_history_navigation_and_clear() {
            open("history")
            mouseClick(control("feriaBrowserHistory_0"))
            compare(navigateSpy.count, 1)
            compare(navigateSpy.signalArguments[0][0], "https://example.com/read")
            tryCompare(menu, "visible", false)
            open("history")
            mouseClick(control("feriaBrowserClearHistory"))
            compare(account.cleared, 1)
        }
        function test_page_titles_are_literal_text() {
            menu.currentTitle = "<b>Current page</b>"
            account.browserHistory = [{url: "https://example.com/read", title: "<b>Earlier reading</b>"}]
            account.changed()
            open("history")
            compare(control("feriaBrowserHistory_0").contentItem.textFormat, Text.PlainText,
                "page titles in action buttons must not interpret page-supplied markup")
            function findText(item, text) {
                if (item.text === text && item.textFormat !== undefined) return item
                for (var child of item.children || []) {
                    var found = findText(child, text)
                    if (found) return found
                }
                return null
            }
            var title = findText(menu.contentItem, menu.currentTitle)
            verify(title !== null)
            compare(title.textFormat, Text.PlainText,
                "current and tab title labels must not interpret page-supplied markup")
        }
        function test_restore_count_uses_previous_session_source() {
            open("settings")
            compare(menu.savedTabCount, 2, "use deck's previous source, not the one persisted current tab")
            deck.restorableTabCount = 0
            tryCompare(menu, "savedTabCount", 0)
            compare(control("feriaBrowserRestorePrevious").enabled, false)
            deck.restorableTabCount = 2
            mouseClick(control("feriaBrowserDismissPrevious"))
            compare(deck.dismissals, 1)
            compare(menu.savedTabCount, 0)
        }
        function test_keyboard_bookmark_and_search_engine() {
            open("bookmarks")
            control("feriaBrowserBookmarkCurrent").forceActiveFocus()
            keyClick(Qt.Key_Space)
            compare(account.bookmarkAction.saved, true)
            mouseClick(control("feriaBrowserBookmark_0"))
            compare(navigateSpy.count, 1)
            compare(navigateSpy.signalArguments[0][0], menu.currentUrl)
            open("bookmarks")
            mouseClick(control("feriaBrowserBookmarkRemove_0"))
            compare(account.bookmarkAction.saved, false)
            menu.section = "settings"
            var engine = control("feriaBrowserSearchEngine")
            engine.forceActiveFocus()
            keyClick(Qt.Key_Space)
            tryCompare(engine.popup, "opened", true)
            keyClick(Qt.Key_Home)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(account.preferenceAction.key, "searchEngine")
            compare(account.preferenceAction.value, "google")
            mouseClick(control("feriaBrowserFreezeBackgroundPreference"))
            compare(account.preferenceAction.key, "freezeBackground")
            compare(account.preferenceAction.value, false)
        }
        function test_settings_restore_and_native_tools() {
            open("settings")
            compare(findChild(menu, "feriaBrowserRestoreTabsPreference"), null,
                "Manual-only restoration must not expose an automatic startup switch")
            mouseClick(control("feriaBrowserRestorePrevious"))
            compare(deck.restores, 1)
            compare(deck.restoredProvider, "mangaplus")
            tryCompare(menu, "visible", false)
            open(); mouseClick(control("feriaBrowserAddress")); compare(addressSpy.count, 1)
            open(); mouseClick(control("feriaBrowserPrint")); compare(printSpy.count, 1)
            open(); mouseClick(control("feriaBrowserDownloads")); compare(downloadSpy.count, 1)
        }
        function test_sections_render_inside_dialog() {
            open()
            for (var row of [
                ["Tabs", "tabs", "feriaBrowserNewPrivate"],
                ["Bookmarks", "bookmarks", "feriaBrowserBookmarkCurrent"],
                ["History", "history", "feriaBrowserClearHistory"],
                ["Settings", "settings", "feriaBrowserSearchEngine"]
            ]) {
                mouseClick(control("feriaBrowser" + row[0] + "Section"))
                compare(menu.section, row[1])
                waitForRendering(menu.contentItem)
                var action = control(row[2])
                var position = action.mapToItem(menu.contentItem, 0, 0)
                verify(action.visible && action.width > 0 && action.height > 0)
                verify(position.x >= 0 && position.y >= 0)
                verify(position.x + action.width <= menu.contentItem.width)
                verify(position.y + action.height <= menu.contentItem.height)
                grabImage(menu.contentItem.parent).save(
                    decodeURIComponent(String(StandardPaths.writableLocation(StandardPaths.TempLocation)).replace(/^file:\/\/\//, ""))
                    + "/colosseum-feria-browser-menu-" + row[1] + ".png")
            }
        }
        function test_safe_dismissal_and_window_bounds() {
            open()
            verify(menu.x >= 0 && menu.y >= 0)
            verify(menu.x + menu.width <= scene.width)
            verify(menu.y + menu.height <= scene.height)
            account.profileChanged()
            tryCompare(menu, "visible", false)
            open(); deck.quiescing = true; tryCompare(menu, "visible", false)
            deck.quiescing = false
            open(); menu.currentUrl = "https://example.com/next"; tryCompare(menu, "visible", false)
            open(); menu.contentItem.forceActiveFocus(); keyClick(Qt.Key_Escape); tryCompare(menu, "visible", false)
        }
    }
}
