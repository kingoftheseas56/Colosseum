// ComicReaderPreview — a standalone first-render harness for the from-scratch Comic Reader.
//
// PURPOSE: let a brother LAUNCH the new Comic Reader and SEE it rendering pages with its HUD,
// WITHOUT cutting over production. This is a preview, not a product surface — it is never mounted
// by any caller; it is only ever loaded as the QML-path argument of the real app:
//
//     colosseum.exe qml/comicreader/ComicReaderPreview.qml [pagesFolder]
//
// The real app (native/main.cpp) registers `ComicReaderCore` + the `image://comicreader/` provider
// UNCONDITIONALLY (they don't depend on the argless user-lane), so loading THIS file as argv[1]
// still gets the full native backend. The shell's `core` seam resolves to the real ComicReaderCore;
// openEntry() decodes our demo files and the provider serves them — a genuine first render.
//
// It mounts ComicReaderShell over a tiny in-QML page store that hands back a generated demo chapter
// (tools/comicreader-preview/pages/, 18 pages incl. two landscape spreads so double-page pairing,
// the spread, and the gutter are all visible). Optional real-folder override: pass a folder as
// argv[2] and it lists that folder's images instead (so it can be pointed at real downloaded manga).

import QtQuick
import QtQuick.Window

Window {
    id: win
    width: 1400
    height: 900
    visible: true
    color: "#000000"
    title: "Comic Reader — Preview"

    // ---- demo chapter (committed PNGs), resolved to absolute file:// urls relative to THIS file ----
    // this file is qml/comicreader/ComicReaderPreview.qml → repo root is ../../ → tools/comicreader-preview/pages/
    readonly property int demoCount: 18
    function _pad3(i) { return ("000" + i).slice(-3) }
    function _demoPages() {
        var arr = []
        for (var i = 0; i < demoCount; ++i) {
            arr.push({
                index: i,
                url: Qt.resolvedUrl("../../tools/comicreader-preview/pages/page_" + _pad3(i) + ".png").toString(),
                group: 0
            })
        }
        return arr
    }

    // ---- optional real-folder override (argv[2]) ----
    readonly property string pagesArg: (Qt.application.arguments.length > 2)
        ? Qt.application.arguments[2] : ""
    readonly property bool overrideMode: pagesArg.length > 0
    property var folderPages: []          // filled async when a folder override is supplied
    property string currentChapterId: "ch-2"

    // ---- the injected page store: the exact contract ComicReaderShell needs ----
    // pageStore wins over the app's Comics/Downloads default (shell store resolution, contract §3).
    // ALL three fake chapters share these pages so prev/next crossing lands on a readable entry.
    QtObject {
        id: previewStore
        // download-line signals the shell's Connections listens for (ignoreUnknownSignals covers the rest)
        signal progress(string cid, int done, int total)
        signal finished(string cid)
        signal failed(string cid, string reason)

        function localPages(entryId) {
            return win.overrideMode ? win.folderPages : win._demoPages()
        }
        function statusOf(id) {
            var t = win.overrideMode ? win.folderPages.length : win.demoCount
            return { state: "ready", done: t, total: t }
        }
        // no-op acquisition seams — the preview never downloads anything
        function downloadChapter(cid, seriesId, seriesTitle, label) {}
        function downloadIssue(cid, url, seriesId, seriesTitle, label, bytes) {}
        function startDownload(cid) {}
    }

    // The Comic Reader itself. core/progress default to the app's real ComicReaderCore/Progress.
    ComicReaderShell {
        id: readerShell
        anchors.fill: parent
        focus: true

        seriesTitle: "Preview Series"
        seriesId: "preview-series"
        // newest-first; the MIDDLE entry is current, so BOTH prev and next crossings are enabled
        chapters: [
            { id: "ch-3", number: 3, name: "Chapter 3" },
            { id: "ch-2", number: 2, name: "Chapter 2" },
            { id: "ch-1", number: 1, name: "Chapter 1" }
        ]
        chapterId: win.currentChapterId
        chapterLabel: "Chapter 1"
        entryKind: "manga"
        western: false
        pageStore: previewStore
        // A preview must not write to Hemanth's real reading history — null the Progress sink so no
        // fake "Preview Series" Continue row lands in his live shelf. Every progress use is guarded
        // `if (progress) ...`, so the reader (pages, HUD, crossing) is otherwise unaffected.
        progress: null

        // window verbs → drive the preview Window
        onCloseRequested: Qt.quit()
        onBackRequested: Qt.quit()
        onMinimizeRequested: win.showMinimized()
        onFullscreenRequested:
            win.visibility = (win.visibility === Window.FullScreen) ? Window.Windowed : Window.FullScreen
    }

    // ---- folder override: build the page list from argv[2] once, then re-open the shell ----
    // Isolated behind Qt.createQmlObject + try/catch so a missing Qt.labs.folderlistmodel module can
    // NEVER break the demo path — the whole point is a clean first render. Only runs when a folder arg
    // is passed. FolderListModel is async, so we open on the demo (empty override → shell shows nothing)
    // and re-run load() the instant the folder is enumerated.
    Component.onCompleted: {
        if (!overrideMode) return
        var url = pagesArg
        if (!/^[a-z]+:\/\//i.test(url))
            url = "file:///" + String(url).replace(/\\/g, "/")
        try {
            var fm = Qt.createQmlObject(
                'import QtQuick; import Qt.labs.folderlistmodel; ' +
                'FolderListModel { showDirs: false; sortField: FolderListModel.Name; ' +
                'nameFilters: ["*.png","*.jpg","*.jpeg","*.webp","*.gif","*.bmp"] }',
                win, "previewFolderModel")
            // Enumeration is async and finishes with count == N. Guard on count (not the
            // FolderListModel.Ready enum, which isn't in this file's import scope) and only
            // rebuild once, when the real page count first lands.
            var rebuild = function () {
                if (fm.count <= 0 || win.folderPages.length === fm.count) return
                var arr = []
                for (var i = 0; i < fm.count; ++i)
                    arr.push({ index: i, url: fm.get(i, "fileURL").toString(), group: 0 })
                win.folderPages = arr
                console.info("[comicreader-preview] folder override:", arr.length, "images from", url)
                readerShell.load()   // re-open now that the real pages are known
            }
            fm.statusChanged.connect(rebuild)
            fm.countChanged.connect(rebuild)
            fm.folder = url
        } catch (e) {
            console.warn("[comicreader-preview] folder override unavailable, using demo chapter:", e)
        }
    }
}
