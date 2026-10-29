// Real-input + geometry tests for the DownloadsPage Play / Retry / Pause / Resume action
// targets. The production page renders from an injected downloadsApi fixture (activeJobs /
// series / items / availableElsewhere plus the mutation entry points), so the checks run
// against real delegates: every active text action must expose a non-overlapping ≥48dp hit
// host carrying a useful accessible name, stay inside the page, and route the exact
// production callback (playArrivingRequested / retry / pause / resume) for both grouped
// season rows and folded single rows, at desktop and narrow-phone widths, with the
// KeyboardAction keyboard path still intact.
import QtQuick
import QtQuick.Window
import QtTest 1.3
import "../../qml" as App

TestCase {
    id: testCase
    name: "PhoneDownloadTargets"
    when: windowShown

    Window {
        id: testWindow
        width: 1280
        height: 1000
        property bool androidHost: false
        visible: true
    }

    Component { id: downloadsComponent; App.DownloadsPage { anchors.fill: parent } }

    Component {
        id: dlApiComponent
        QtObject {
            property var calls: []
            property var jobList: []
            property var totals: ({ "items": 5, "bytes": 0, "tankoban": 0, "biblio": 0, "theatre": 5 })
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

    function findChild(root, name) {
        if (!root)
            return null
        if (root.objectName === name)
            return root
        const children = root.children || []
        for (let i = 0; i < children.length; ++i) {
            const found = findChild(children[i], name)
            if (found)
                return found
        }
        return null
    }

    function rectIn(item, ancestor) {
        const point = item.mapToItem(ancestor, 0, 0)
        return { "x": point.x, "y": point.y, "width": item.width, "height": item.height }
    }

    function rectsOverlap(a, b) {
        return a.x < b.x + b.width - 0.5 && b.x < a.x + a.width - 0.5
               && a.y < b.y + b.height - 0.5 && b.y < a.y + a.height - 0.5
    }

    function ops(api, op) {
        const out = []
        for (let i = 0; i < api.calls.length; ++i)
            if (api.calls[i].op === op)
                out.push(api.calls[i])
        return out
    }

    function job(id, groupKey, state, opts) {
        const extra = opts ||({})
        return {
            "id": id, "world": "theatre", "groupKey": groupKey, "state": state,
            "season": extra.season || 1,
            "seriesTitle": extra.seriesTitle || (extra.title || "Fixture Series"),
            "title": extra.title || ("Episode " + (extra.episode || 1)),
            "episode": extra.episode || 1,
            "received": extra.received || 0,
            "total": extra.total || 0,
            "speed": extra.speed || 0,
            "etaSec": extra.etaSec === undefined ? -1 : extra.etaSec,
            "ratio": extra.ratio || 0,
            "error": extra.error || "",
            "canPlay": extra.canPlay === true,
            "canPause": extra.canPause === true,
            "canResume": extra.canResume === true,
            "canRetry": extra.canRetry === true,
            "canCancel": extra.canCancel === true,
            "canDismiss": extra.canDismiss === true
        }
    }

    function fixtureJobs() {
        return [
            job("dl-play", "g:play", "downloading", {
                "title": "Playable Feature", "seriesTitle": "Playable Feature",
                "canPlay": true, "canPause": true,
                "received": 9437184, "total": 18874368, "speed": 1048576,
                "etaSec": 60, "ratio": 0.5
            }),
            job("dl-retry", "g:retry", "failed", {
                "title": "Failed Feature", "seriesTitle": "Failed Feature",
                "canRetry": true, "error": "source gone"
            }),
            job("dl-resume", "g:resume", "paused", {
                "title": "Paused Feature", "seriesTitle": "Paused Feature",
                "canResume": true, "received": 2097152, "total": 18874368
            }),
            job("dl-ep1", "g:season", "downloading", {
                "seriesTitle": "Season Show", "season": 2, "episode": 1,
                "canPlay": true, "canPause": true, "canCancel": true,
                "received": 5242880, "total": 10485760, "speed": 524288,
                "etaSec": 120, "ratio": 0.5
            }),
            job("dl-ep2", "g:season", "failed", {
                "seriesTitle": "Season Show", "season": 2, "episode": 2,
                "canRetry": true, "error": "host closed"
            })
        ]
    }

    function buildPage(width, height) {
        testWindow.width = width
        testWindow.height = height
        const page = createTemporaryObject(downloadsComponent, testWindow.contentItem)
        verify(page !== null, "production DownloadsPage should instantiate")
        const api = dlApiComponent.createObject(page)
        api.jobList = fixtureJobs()
        page.downloadsApi = api
        page.refresh()
        wait(150)
        return { page: page, api: api }
    }

    function keyboardActionIn(host) {
        if (!host)
            return null
        const children = host.children || []
        for (let i = 0; i < children.length; ++i)
            if (children[i].accessibleName !== undefined
                    && typeof children[i].triggered === "function")
                return children[i]
        return null
    }

    function checkHost(page, name, label) {
        const host = findChild(page, name)
        verify(host !== null, label + " should expose its addressed hit host")
        if (!host)
            return null
        verify(host.visible, label + " hit host should be visible")
        verify(host.width >= 48 && host.height >= 48,
               label + " should provide a ≥48dp hit host; size=" + host.width + "x" + host.height)
        const pageRect = rectIn(host, page)
        verify(pageRect.x >= -0.5 && pageRect.y >= -0.5
               && pageRect.x + pageRect.width <= page.width + 0.5
               && pageRect.y + pageRect.height <= page.height + 0.5,
               label + " host should stay inside the page; rect=" + JSON.stringify(pageRect))
        const action = keyboardActionIn(host)
        verify(action !== null, label + " host should carry its production KeyboardAction")
        if (action) {
            compare(action.width, host.width, label + " KeyboardAction should fill its host")
            compare(action.height, host.height, label + " KeyboardAction should fill its host")
            verify(String(action.accessibleName).length > 0,
                   label + " needs a useful accessible name")
            verify(action.enabled && action.pointerEnabled,
                   label + " should keep input enabled")
        }
        console.log("DOWNLOAD_TARGET " + label + " rect=" + JSON.stringify(pageRect)
                    + " accessible=" + (action ? action.accessibleName : "?"))
        return host
    }

    function checkRowTargetsDoNotOverlap(page, row, label) {
        const rects = []
        const children = row.children || []
        for (let i = 0; i < children.length; ++i) {
            const child = children[i]
            if (!child.visible || child.width <= 0 || child.height <= 0)
                continue
            rects.push({ "name": child.objectName || (child.text !== undefined ? String(child.text) : "?"),
                         "rect": rectIn(child, page) })
        }
        verify(rects.length >= 2, label + " should expose at least two action targets")
        for (let a = 0; a < rects.length; ++a) {
            for (let b = a + 1; b < rects.length; ++b) {
                verify(!rectsOverlap(rects[a].rect, rects[b].rect),
                       label + " action targets must not overlap: " + rects[a].name
                       + " " + JSON.stringify(rects[a].rect) + " vs " + rects[b].name
                       + " " + JSON.stringify(rects[b].rect))
            }
        }
    }

    function touchCenter(item) {
        const root = testWindow.contentItem
        const point = item.mapToItem(root, item.width / 2, item.height / 2)
        touchEvent(root).press(0, root, point.x, point.y).commit()
        wait(40)
        touchEvent(root).release(0, root, point.x, point.y).commit()
        wait(100)
    }

    // Names of the hosts the production page addresses (Play keeps its historic
    // downloadsPlayArriving_ seam; Retry/Pause gain the same per-row stems).
    function headerHostNames() {
        return ["downloadsPlayArriving_dl-play", "downloadsPauseAction_dl-play",
                "downloadsRetryAction_dl-retry", "downloadsPauseAction_dl-resume"]
    }

    function test_grouped_and_single_download_hosts_geometry_and_routing() {
        const env = buildPage(1280, 1000)
        const page = env.page
        const api = env.api
        playSpy.target = page
        playSpy.clear()

        const playHost = checkHost(page, "downloadsPlayArriving_dl-play", "single Play")
        const pauseHost = checkHost(page, "downloadsPauseAction_dl-play", "single Pause")
        const retryHost = checkHost(page, "downloadsRetryAction_dl-retry", "single Retry")
        const resumeHost = checkHost(page, "downloadsPauseAction_dl-resume", "single Resume")
        checkRowTargetsDoNotOverlap(page, playHost.parent, "single playable group header row")

        // Exact action routing through committed touches on the hosts.
        touchCenter(playHost)
        compare(playSpy.count, 1, "Play host touch should fire playArrivingRequested once")
        compare(String(playSpy.signalArguments[0][0].id), "dl-play",
                "Play host should route its own job")
        touchCenter(pauseHost)
        compare(ops(api, "pause").length, 1, "Pause host touch should call pause once")
        compare(ops(api, "pause")[0].world, "theatre", "Pause should route the job's world")
        compare(ops(api, "pause")[0].id, "dl-play", "Pause should route the job's id")
        touchCenter(retryHost)
        compare(ops(api, "retry").length, 1, "Retry host touch should call retry once")
        compare(ops(api, "retry")[0].id, "dl-retry", "Retry should route its own job")
        touchCenter(resumeHost)
        compare(ops(api, "resume").length, 1, "Resume host touch should call resume once")
        compare(ops(api, "resume")[0].id, "dl-resume", "Resume should route its own job")

        // Grouped season row: fold closed first, then expanded episode rows.
        verify(findChild(page, "downloadsPlayArriving_dl-ep1") === null
               || !findChild(page, "downloadsPlayArriving_dl-ep1").visible,
               "episode actions must stay hidden while the season fold is closed")
        const seasonPause = checkHost(page, "downloadsPauseAction_dl-ep1", "season Pause")
        compare(keyboardActionIn(seasonPause).accessibleName, "Pause season",
                "season Pause host should keep its exact accessible name")
        page.toggleGroup("theatre|g:season")
        wait(150)
        const epPlay = checkHost(page, "downloadsPlayArriving_dl-ep1", "episode Play")
        const epPause = checkHost(page, "downloadsPauseEpisode_dl-ep1", "episode Pause")
        const epRetry = checkHost(page, "downloadsRetryEpisode_dl-ep2", "episode Retry")
        checkRowTargetsDoNotOverlap(page, epPlay.parent, "expanded episode row targets")
        checkRowTargetsDoNotOverlap(page, seasonPause.parent, "season header row targets")

        touchCenter(epPlay)
        compare(playSpy.count, 2, "episode Play host should fire playArrivingRequested")
        compare(String(playSpy.signalArguments[1][0].id), "dl-ep1",
                "episode Play should route its own job")
        touchCenter(epPause)
        compare(ops(api, "pause").length, 2, "episode Pause host should call pause once")
        compare(ops(api, "pause")[1].id, "dl-ep1", "episode Pause should route its own row")
        touchCenter(epRetry)
        compare(ops(api, "retry").length, 2, "episode Retry host should call retry once")
        compare(ops(api, "retry")[1].id, "dl-ep2", "episode Retry should route its own row")

        // Keyboard path stays intact: a focused host still activates on Enter. The
        // owning window must be active for key delivery in this harness.
        testWindow.requestActivate()
        tryVerify(function() { return testWindow.active }, 2000,
                  "downloads window should activate for keyboard delivery")
        const retryAction = keyboardActionIn(retryHost)
        verify(retryAction !== null, "Retry host should carry its action")
        retryAction.forceActiveFocus(Qt.OtherFocusReason)
        tryVerify(function() { return retryAction.activeFocus }, 1500,
                  "download host should accept keyboard focus")
        const retriesBefore = ops(api, "retry").length
        keyClick(Qt.Key_Return)
        tryVerify(function() { return ops(api, "retry").length === retriesBefore + 1 }, 1500,
                  "keyboard Enter should still trigger the retry action")
        compare(ops(api, "retry")[retriesBefore].id, "dl-retry",
                "keyboard trigger should route the same job")
    }

    function test_narrow_download_hosts_stay_separated_and_inside() {
        const env = buildPage(412, 1000)
        const page = env.page
        const playHost = checkHost(page, "downloadsPlayArriving_dl-play", "narrow single Play")
        const pauseHost = checkHost(page, "downloadsPauseAction_dl-play", "narrow single Pause")
        const retryHost = checkHost(page, "downloadsRetryAction_dl-retry", "narrow single Retry")
        checkRowTargetsDoNotOverlap(page, playHost.parent, "narrow single group header row")
        page.toggleGroup("theatre|g:season")
        wait(150)
        const epPlay = checkHost(page, "downloadsPlayArriving_dl-ep1", "narrow episode Play")
        checkHost(page, "downloadsPauseEpisode_dl-ep1", "narrow episode Pause")
        checkRowTargetsDoNotOverlap(page, epPlay.parent, "narrow episode row targets")
        // One committed routing check at the narrow width too.
        touchCenter(retryHost)
        compare(ops(env.api, "retry").length, 1,
                "narrow Retry host should still route its exact callback")
        compare(ops(env.api, "retry")[0].id, "dl-retry",
                "narrow Retry should route its own job")
    }
}
