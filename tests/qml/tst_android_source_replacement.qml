import QtQuick
import QtTest

// BR-SOURCE-REPLACEMENT: the production Main Sessions Connections (onTargetReplaced /
// onSourceReplaced) and activateMovieSession are extracted and run against a recording
// fake player and a fake Sessions store (renamed sessionStore in the extract) that replays SessionStore's signal order
// (target installed -> sourceReplaced -> setActive). Native ordering/identity is proven by
// tests/session_store_harness.cpp; this covers the QML warm/active dispatch only.
TestCase {
    id: testCase
    name: "AndroidSourceReplacement"
    property string mainSource: ""
    property var host: null

    function blockAt(source, start) {
        verify(start >= 0, "Required production source block exists")
        const opening = source.indexOf("{", start)
        let depth = 1
        for (let end = opening + 1; end < source.length; ++end) {
            if (source[end] === "{") ++depth
            else if (source[end] === "}" && --depth === 0) return source.slice(start, end + 1)
        }
        fail("Unterminated production source block")
        return ""
    }
    function initTestCase() {
        const request = new XMLHttpRequest()
        request.open("GET", Qt.resolvedUrl("../../qml/Main.qml"), false)
        request.send()
        mainSource = request.responseText
        verify(mainSource.length > 0, "Read production Main; enable QML_XHR_ALLOW_FILE_READ")
    }
    function init() {
        const marker = mainSource.indexOf("function onSourceReplaced(id)")
        const connections = blockAt(mainSource, mainSource.lastIndexOf("Connections {", marker))
        const activate = blockAt(mainSource, mainSource.indexOf("function activateMovieSession(rec)"))
        const source = [
            'import QtQuick',
            'Item { id: win',
            'property string warmPlayerSessionId: ""',
            'property var calls: []',
            'function claimAudio(w) {}',
            'function requestTheatreAudio() {}',
            'function downloadedVideoPath(id) { return "" }',
            // Seam: the real activateSession routes movie records to activateMovieSession.
            'function activateSession(rec) { if (rec && rec.contentKind === "movie") activateMovieSession(rec) }',
            activate,
            'QtObject { id: sessionStore',
            '  signal targetReplaced(string id); signal sourceReplaced(string id)',
            '  property string activeId: ""; property var records: ({})',
            '  function get(id) { return records[id] || ({}) }',
            '  function saveState(id, st) { var r = records[id]; r.savedState = st; records[id] = r }',
            '}',
            'Item { id: playerLayer; property alias item: player',
            '  QtObject { id: player; property real position: 0',
            '    function resumeFromMinimize() { win.calls.push("resume") }',
            '    function playTorrent(h, f) { win.calls.push("torrent:" + h + ":" + f) }',
            '    function playLocalFile(t) { win.calls.push("local:" + t.localPath) }',
            '    function playRemoteUrl(t) { win.calls.push("url:" + t.streamUrl) }',
            '    function restoreState(st) { win.calls.push("restore:" + Number(st.position || 0)) }',
            '    function captureState() { return { "position": player.position } }',
            '  }',
            '}',
            connections,
            '}'
        ].join("\n").replace(/\bSessions\b/g, "sessionStore")
        host = Qt.createQmlObject(source, testCase, Qt.resolvedUrl("MainSourceReplacementExtract.qml"))
        verify(host !== null)
    }
    function cleanup() { if (host) { host.destroy(); host = null } }

    function sessions() {
        for (let i = 0; i < host.data.length; ++i)
            if (host.data[i].records !== undefined) return host.data[i]
        return null
    }
    function player() {
        for (let i = 0; i < host.data.length; ++i)
            if (host.data[i].item !== undefined) return host.data[i].item
        return null
    }
    function record(hash, file, position) {
        return { id: "s1", contentKind: "movie",
                 target: { infoHash: hash, fileIdx: file, subId: "tt1:1:2", position: 0 },
                 savedState: position > 0 ? { position: position } : ({}) }
    }
    // Replays SessionStore.openOrSwitch's same-content new-source branch.
    function reopenWithSource(store, hash, file) {
        const r = store.records["s1"]
        r.target = { infoHash: hash, fileIdx: file, subId: "tt1:1:2", position: 0 }
        store.records["s1"] = r
        store.sourceReplaced("s1")
        if (store.activeId !== "s1") {          // setActive -> activeChanged -> activateSession
            store.activeId = "s1"
            host.activateSession(store.get("s1"))
        }
    }

    function test_parked_session_loads_new_source_with_saved_position() {
        const store = sessions()
        store.records = { "s1": record("hashA", 0, 600) }   // minimized: state captured at park
        host.warmPlayerSessionId = "s1"
        store.activeId = ""
        reopenWithSource(store, "hashB", 3)
        compare(host.calls.join(","), "torrent:hashB:3,restore:600")
        compare(host.warmPlayerSessionId, "s1")
    }

    function test_active_session_switches_with_latest_live_position() {
        const store = sessions()
        store.records = { "s1": record("hashA", 0, 120) }   // stale saved position
        store.activeId = "s1"
        host.warmPlayerSessionId = "s1"
        player().position = 905                             // live position of the old route
        reopenWithSource(store, "hashB", 1)
        compare(host.calls.join(","), "torrent:hashB:1,restore:905")
        compare(store.get("s1").savedState.position, 905)
    }

    function test_unrelated_warm_session_is_untouched() {
        const store = sessions()
        store.records = { "s1": record("hashA", 0, 0), "s2": { id: "s2", contentKind: "movie", target: {} } }
        host.warmPlayerSessionId = "s2"
        store.activeId = "s2"
        store.sourceReplaced("s1")
        compare(host.warmPlayerSessionId, "s2")
        compare(host.calls.length, 0)
    }

    function test_warm_same_source_reopen_still_resumes() {
        // No sourceReplaced (metadata-only refresh): warm resume path is preserved.
        const store = sessions()
        store.records = { "s1": record("hashA", 0, 300) }
        host.warmPlayerSessionId = "s1"
        store.activeId = "s1"
        host.activateSession(store.get("s1"))
        compare(host.calls.join(","), "resume")
    }
}
