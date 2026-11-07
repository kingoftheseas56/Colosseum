// PF-01: production DownloadsPage text actions and TopBar system icons expose >=48-unit
// hit hosts on Android hosts (independent of desktop presentation), keep neighbouring
// hosts disjoint, route the exact production callback on touch, and keep desktop sizes.
// Adapted from Alpha tests/qml/tst_phone_download_targets.qml / tst_phone_touch_chrome.qml.
import QtQuick
import QtQuick.Window
import QtTest 1.3
import "../../qml" as App

TestCase {
    id: testCase
    name: "AndroidDownloadTargets"
    when: windowShown

    Window {
        id: testWindow
        width: 1185
        height: 900
        property bool androidHost: true
        property bool desktopPresentation: true
        visible: true
    }

    Component { id: downloadsComponent; App.DownloadsPage { anchors.fill: parent } }
    Item { id: fakeBackdrop; width: 10; height: 10 }
    Component { id: topBarComponent; App.TopBar { width: 1185; height: 56; backdrop: fakeBackdrop } }

    Component {
        id: dlApiComponent
        QtObject {
            property var calls: []
            property var jobList: []
            property var totals: ({ "items": 3, "bytes": 0, "tankoban": 0, "biblio": 0, "theatre": 3 })
            function activeJobs() { return jobList }
            function series(world) { return [] }
            function items(world, key) { return [] }
            function availableElsewhere() { return [] }
            function retry(world, id) { calls.push({ "op": "retry", "world": world, "id": id }) }
            function pause(world, id) { calls.push({ "op": "pause", "world": world, "id": id }) }
            function resume(world, id) { calls.push({ "op": "resume", "world": world, "id": id }) }
            function cancel(world, id) { calls.push({ "op": "cancel", "world": world, "id": id }) }
            function dismissFailure(world, id) { calls.push({ "op": "dismissFailure", "world": world, "id": id }) }
        }
    }

    SignalSpy { id: playSpy; signalName: "playArrivingRequested" }

    function job(id, groupKey, state, extra) {
        return {
            "id": id, "world": "theatre", "groupKey": groupKey, "state": state,
            "season": 1, "seriesTitle": extra.title, "title": extra.title, "episode": 1,
            "received": extra.received || 0, "total": extra.total || 0, "speed": 0,
            "etaSec": -1, "ratio": 0, "error": "",
            "canPlay": extra.canPlay === true, "canPause": extra.canPause === true,
            "canResume": extra.canResume === true, "canRetry": extra.canRetry === true,
            "canCancel": extra.canCancel === true, "canDismiss": false
        }
    }

    function buildPage(android) {
        testWindow.androidHost = android
        var page = createTemporaryObject(downloadsComponent, testWindow.contentItem)
        verify(page !== null)
        var api = dlApiComponent.createObject(page)
        api.jobList = [
            job("dl-play", "g:play", "downloading", { title: "Playable Feature", canPlay: true, canPause: true,
                                                      canCancel: true, received: 9437184, total: 18874368 }),
            job("dl-retry", "g:retry", "failed", { title: "Failed Feature", canRetry: true })
        ]
        page.downloadsApi = api
        page.refresh()
        wait(150)
        return { page: page, api: api }
    }

    function collectActions(root, out) {
        if (!root)
            return out
        var kids = root.children || []
        for (var i = 0; i < kids.length; ++i) {
            var c = kids[i]
            if (c.accessibleName !== undefined && typeof c.triggered === "function" && c.parent
                    && c.parent.text !== undefined && c.parent.visible)
                out.push(c)
            collectActions(c, out)
        }
        return out
    }

    function actionNamed(page, name, startsWithText) {
        var all = collectActions(page, [])
        for (var i = 0; i < all.length; ++i)
            if (String(all[i].accessibleName) === name
                    && (startsWithText === undefined || String(all[i].parent.text) === startsWithText))
                return all[i]
        return null
    }

    function rectIn(item, ancestor) {
        var p = item.mapToItem(ancestor, 0, 0)
        return { x: p.x, y: p.y, width: item.width, height: item.height }
    }

    function overlap(a, b) {
        return a.x < b.x + b.width - 0.5 && b.x < a.x + a.width - 0.5
            && a.y < b.y + b.height - 0.5 && b.y < a.y + a.height - 0.5
    }

    function touchCenter(item) {
        var root = testWindow.contentItem
        var p = item.mapToItem(root, item.width / 2, item.height / 2)
        touchEvent(root).press(0, root, p.x, p.y).commit()
        wait(30)
        touchEvent(root).release(0, root, p.x, p.y).commit()
        wait(60)
    }

    function touchAt(item, x, y) {
        var root = testWindow.contentItem
        var p = item.mapToItem(root, x, y)
        touchEvent(root).press(0, root, p.x, p.y).commit()
        wait(30)
        touchEvent(root).release(0, root, p.x, p.y).commit()
        wait(60)
    }

    function test_android_download_actions_have_disjoint_48_hosts_and_route() {
        var env = buildPage(true)
        var page = env.page
        playSpy.target = page
        playSpy.clear()
        var play = actionNamed(page, "Play arriving download")
        var pause = actionNamed(page, "Pause")
        var retry = actionNamed(page, "Retry download")
        var cancel = actionNamed(page, "Cancel")
        var list = [play, pause, retry, cancel]
        for (var i = 0; i < list.length; ++i) {
            verify(list[i] !== null, "action " + i + " present")
            verify(list[i].parent.width >= 48 && list[i].parent.height >= 48,
                   list[i].accessibleName + " host " + list[i].parent.width + "x" + list[i].parent.height)
            compare(list[i].width, list[i].parent.width)
            compare(list[i].height, list[i].parent.height)
        }
        // Same-row neighbours stay disjoint.
        var rowItems = [play, pause, cancel]
        for (var a = 0; a < rowItems.length; ++a)
            for (var b = a + 1; b < rowItems.length; ++b)
                verify(!overlap(rectIn(rowItems[a].parent, page), rectIn(rowItems[b].parent, page)),
                       rowItems[a].accessibleName + " vs " + rowItems[b].accessibleName)

        // Edge-of-host touch (outside the 12px glyph) routes exactly one production callback.
        touchAt(play.parent, 2, 2)
        compare(playSpy.count, 1)
        compare(String(playSpy.signalArguments[0][0].id), "dl-play")
        touchAt(pause.parent, pause.parent.width - 2, pause.parent.height - 2)
        compare(env.api.calls.length, 1)
        compare(env.api.calls[0].op, "pause")
        compare(env.api.calls[0].id, "dl-play")
        touchCenter(retry.parent)
        compare(env.api.calls.length, 2)
        compare(env.api.calls[1].op, "retry")
        compare(env.api.calls[1].id, "dl-retry")
    }

    function test_desktop_download_actions_keep_text_size() {
        var env = buildPage(false)
        var play = actionNamed(env.page, "Play arriving download")
        verify(play !== null)
        compare(play.parent.width, play.parent.implicitWidth)
        compare(play.parent.height, play.parent.implicitHeight)
        verify(play.parent.height < 48)
    }

    function sysIcons(bar) {
        var row = null
        var stack = [bar]
        while (stack.length) {
            var it = stack.pop()
            if (it.objectName === "topBarSystemChrome") { row = it; break }
            var k = it.children || []
            for (var i = 0; i < k.length; ++i) stack.push(k[i])
        }
        verify(row !== null)
        var out = []
        for (var j = 0; j < row.children.length; ++j) {
            var c = row.children[j]
            if (c.visible && c.source !== undefined && c.accessibleName !== undefined)
                out.push(c)
        }
        return { row: row, icons: out }
    }

    function inputOf(icon) {
        var k = icon.children
        for (var i = 0; i < k.length; ++i)
            if (k[i].accessibleName !== undefined && typeof k[i].triggered === "function")
                return k[i]
        return null
    }

    function test_android_topbar_system_icons_touch_hosts() {
        var bar = createTemporaryObject(topBarComponent, testWindow.contentItem,
                                        { androidHost: true, activeMedium: "Theatre", lifecycleActive: false })
        verify(bar !== null)
        wait(50)
        var s = sysIcons(bar)
        verify(s.icons.length >= 3)
        var prev = null
        for (var i = 0; i < s.icons.length; ++i) {
            var input = inputOf(s.icons[i])
            verify(input !== null)
            verify(input.height >= 48, s.icons[i].accessibleName + " height " + input.height)
            // Width grows by the gap share without changing the Row layout.
            verify(input.width >= 22 + Math.min(26, 2 * Math.floor(s.row.spacing / 2)) - 0.5)
            var r = rectIn(input, bar)
            if (prev)
                verify(!overlap(prev, r), "neighbouring system hit hosts stay disjoint")
            prev = r
        }
        // Edge touch inside the extended host routes that icon only.
        var search = null
        for (var j = 0; j < s.icons.length; ++j) if (s.icons[j].accessibleName === "Search") search = s.icons[j]
        verify(search !== null)
        var spy = Qt.createQmlObject('import QtTest; SignalSpy { signalName: "searchClicked" }', testCase)
        spy.target = bar
        var si = inputOf(search)
        touchAt(si, 1, 1)
        compare(spy.count, 1)
        spy.destroy()
    }

    function test_desktop_topbar_icons_unchanged() {
        var bar = createTemporaryObject(topBarComponent, testWindow.contentItem,
                                        { androidHost: false, activeMedium: "Theatre", lifecycleActive: false })
        var s = sysIcons(bar)
        for (var i = 0; i < s.icons.length; ++i) {
            compare(s.icons[i].height, 22)
            verify(s.icons[i].width === 22 || s.icons[i].width === 82)
            compare(inputOf(s.icons[i]).width, s.icons[i].width)
        }
    }
}
