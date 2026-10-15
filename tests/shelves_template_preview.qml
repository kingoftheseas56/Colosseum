import QtQuick
import QtQuick.Window
import "../qml" as App

Window {
    id: host
    visible: true
    width: Number(argument("--width", "1440"))
    height: Number(argument("--height", "720"))
    title: "Shelves template — " + page.universeName
    // A capture run must not take the keyboard or the mouse from whoever is at the machine:
    // the window pops up over their work, and a stray click or Enter would land in it.
    flags: argument("--capture", "").length > 0
           ? Qt.Window | Qt.WindowDoesNotAcceptFocus | Qt.WindowTransparentForInput : Qt.Window
    function argument(key, fallback) {
        const i = Qt.application.arguments.indexOf(key)
        return i >= 0 ? Qt.application.arguments[i + 1] : fallback
    }
    function findItem(item, name) {
        if (item.objectName === name) return item
        for (let i = 0; i < item.children.length; ++i) {
            const result = findItem(item.children[i], name)
            if (result) return result
        }
        return null
    }
    App.ShelvesUniversePage {
        id: page
        universeName: host.argument("--universe", "Dragon Ball")
        onBackRequested: host.close()
        onCloseRequested: host.close()
        onMinimizeRequested: host.showMinimized()
        onFullscreenRequested: host.visibility === Window.FullScreen ? host.showNormal() : host.showFullScreen()
        onSeriesRequested: (title) => console.log("[shelves-preview] MANGA", title)
        onWatchRequested: (item) => console.log("[shelves-preview] WATCH", item.title)
        onUniverseRequested: (name) => page.universeName = name
        onContinueResumeRequested: (entry) => console.log("[shelves-preview] RESUME", entry.title)
        onContinueDetailRequested: (entry) => console.log("[shelves-preview] DETAIL", entry.title)
    }
    // --state returning: seed a returning visitor once the works have resolved, so Continue,
    // progress marks and the resume labels can be judged without the app's Progress store.
    // Video rides the pinned imdb id; a manga rides its MAL pin where curation carries one
    // (Naruto and Attack on Titan do; One Piece and Dragon Ball stay honestly unmatched).
    Timer {
        interval: 12000
        running: host.argument("--state", "") === "returning"
        onTriggered: {
            const all = []
            ;(page.uni.shelves || []).forEach(function(sh) { sh.runs.forEach(function(r) { r.items.forEach(function(w) { all.push(w) }) }) })
            const m = all.filter(function(w) { return w.medium === "manga" })
            const s = all.filter(function(w) { return w.medium === "series" || w.medium === "movie" }), out = []
            function add(id, kind, title, progress, sub, watched) {
                if (id) out.push({ id: String(id), kind: kind, title: title, sub: sub,
                                   cover: "", progress: progress, watched: watched === true })
            }
            if (m.length) add(m[0].mal ? "mal:" + m[0].mal : "", "manga", m[0].title, 0.38, "Chapter 195")
            if (s.length > 1) { add(s[0].id, "video", s[0].title, 1, "", true); add(s[1].id, "video", s[1].title, 0.62, "Episode 36") }
            page.previewProgress = out.reverse()
        }
    }
    // --scroll N: capture the page scrolled to N pixels
    Timer {
        interval: host.waitMs - 2000
        running: Number(host.argument("--scroll", "0")) > 0
        onTriggered: findItem(page, "shelvesScroll").contentY = Number(host.argument("--scroll", "0"))
    }
    // --wait N: seconds before the capture (default 20). Outside the app nothing pins the image
    // hosts to IPv4, so covers can take ~21s each on a machine with dead IPv6.
    readonly property int waitMs: Number(argument("--wait", "20")) * 1000
    Timer {
        interval: host.waitMs
        running: host.argument("--capture", "").length > 0
        onTriggered: {
            const section = findItem(page, host.argument("--section", ""))
            if (section && section !== page) page.revealSection(section)
            page.grabToImage(function(result) {
            const saved = result.saveToFile(host.argument("--capture", ""))
            console.log("[shelves-preview] focus", host.activeFocusItem ? (host.activeFocusItem.objectName || host.activeFocusItem) : "none",
                        "scroll", findItem(page, "shelvesScroll").contentY)
            console.log("[shelves-preview] capture", saved, (page.uni.shelves || []).map(function(sh) {
                return sh.title + ":" + sh.runs.map(function(r) { return r.items.length }).join("+") }).join(" | "))
            Qt.exit(saved ? 0 : 1)
            })
        }
    }
}
