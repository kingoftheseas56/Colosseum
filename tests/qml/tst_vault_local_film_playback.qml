import QtQuick
import QtTest

// Main and PlayerPage need native services the shared runner does not load.
// Exercise their production routing functions and HUD binding with recording stubs.
TestCase {
    id: suite
    name: "VaultLocalFilmPlayback"
    readonly property string filmPath: "/vault/About.a.Boy.2002.720p.BluRay.x264.YIFY.mp4"
    readonly property string poster: "file:///vault/about-a-boy.jpg"

    function source(file) {
        const request = new XMLHttpRequest()
        request.open("GET", Qt.resolvedUrl("../../qml/" + file + ".qml"), false)
        request.send()
        verify(request.responseText.length > 0, "Read production " + file)
        return request.responseText
    }
    function extract(text, header) {
        const start = text.indexOf(header)
        verify(start >= 0, header)
        const open = text.indexOf("{", start)
        let depth = 1
        for (let i = open + 1; i < text.length; ++i) {
            if (text[i] === "{") ++depth
            else if (text[i] === "}" && --depth === 0) return text.slice(start, i + 1)
        }
        fail("Unterminated block: " + header)
    }

    function test_identifiedFilmKeepsFileIdentity_data() {
        return [{ tag: "theatre-copy", theatre: true }, { tag: "vault-resume", theatre: false }]
    }
    function test_identifiedFilmKeepsFileIdentity(data) {
        const main = source("Main")
        const pickCopy = extract(main, "item.localMediaRequested.connect(function").slice("item.localMediaRequested.connect(".length)
        const host = Qt.createQmlObject(`import QtQuick
            import "${Qt.resolvedUrl("../../qml/EpisodeBrowser.js")}" as EpisodeBrowser
            Item {
                id: win
                property bool localMediaAvailable: true
                property var pendingLocalVideoPresentation: null
                property var pendingIdentityRoute: null
                property var opened: null
                property var requestedPaths: []
                property var localLaunchState: ({openCount: 0})
                function setGuiStallContext(action, surface) {}
                function resolvedDownloadPath(path) { return path }
                function openMediaRequested(path, presentation) { openLocalMedia([path], presentation) }
                function pickCopy(path) { (${pickCopy})(path) }
                QtObject { id: item
                    property string mediaType: "movie"
                    property string title: "About a Boy"
                    property string cover: suite.poster
                }
                QtObject { id: launch; function openAsync(paths) { win.requestedPaths = paths } }
                QtObject { id: sessions; function openOrSwitch(desc) { win.opened = desc } }
                QtObject { id: progress
                    function get(kind, id) { return id === "vault:copy" ? { resume: { position: 78 } } : null }
                }
                ${["openLocalMedia", "dispatchLocalRoute", "videoTargetFor", "openLocalVideoSession"]
                    .map(name => extract(main, "function " + name + "(")).join("\n")
                    .replace(/\bLocalLaunch\b/g, "launch").replace(/\bSessions\b/g, "sessions").replace(/\bProgress\b/g, "progress")}
                ${extract(source("VaultLibraryPage"), "function resumeRow(")}
            }`, suite)
        verify(host)
        const route = { path: filmPath, vaultId: "vault:copy", family: "video", accepted: true,
                        title: "About a Boy 2002 720p BluRay x264 YIFY" }
        if (data.theatre) host.pickCopy(filmPath)
        else host.resumeRow({ kind: "movie", identified: true, title: "About a Boy", cover: poster,
                              path: "/vault/other-copy.mp4", resumePath: filmPath })
        compare(host.requestedPaths, [filmPath])
        host.dispatchLocalRoute(route)
        compare(host.opened.title, "About a Boy")
        compare(host.opened.target.title, "About a Boy")
        compare(host.opened.target.art, poster)
        compare(host.opened.target.id, "vault:copy")
        compare(host.opened.target.position, 78)
        compare(host.opened.target.localPath, filmPath)

        host.openLocalMedia(["/vault/unidentified.mp4"])
        host.dispatchLocalRoute({ path: "/vault/unidentified.mp4", family: "video", accepted: true,
                                  title: "Unidentified", vaultId: "vault:other" })
        compare(host.opened.target.title, "Unidentified")
        compare(host.opened.target.art, "")
        host.destroy()
    }

    function test_cachePercentageOnlyForNetworkDependentMedia() {
        const host = Qt.createQmlObject(`import QtQuick
            QtObject {
                id: root
                property bool starting: false
                property bool errored: false
                property bool seeking: false
                property string chapterTransient: ""
                property string mediaLocalPath: ""
                property string arrivingStreamUrl: ""
                property QtObject backend: QtObject {
                    id: mpv
                    property real cacheBufferingState: 1
                    property bool pause: false
                    property real speed: 1
                }
                ${extract(source("PlayerPage"), "readonly property string stateLineText:")}
            }`, suite)
        verify(host)
        compare(host.stateLineText, "Buffering 1%")
        host.mediaLocalPath = filmPath
        compare(host.stateLineText, "")
        host.backend.pause = true
        compare(host.stateLineText, "Paused")
        host.backend.pause = false
        host.arrivingStreamUrl = "https://example.invalid/film.mp4"
        compare(host.stateLineText, "Buffering 1%")
        host.destroy()
    }
}
