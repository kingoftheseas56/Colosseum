import QtQuick
import QtQuick.Window
import QtTest 1.3
import "../../qml" as App

// PF-02b: the Comic (GetComics alternate sources) and Manga (Tankoban volume) Sources pages
// keep at least one complete 150-unit result row inside the page at the Android desktop-
// presentation launch heights (640/zoom: 556 at 1.15, 533 at 1.20); desktop keeps its 300
// banner. Calculated logical viewports, not measured device rectangles.
TestCase {
    id: testCase
    name: "AndroidComicMangaSourcesBudget"
    when: windowShown

    Window {
        id: hostWindow
        width: 1280; height: 900; visible: true
        property bool androidHost: false
        property bool desktopPresentation: true
        Item { id: surface; anchors.fill: parent }
    }
    Component { id: comicComponent; App.ComicTorrentSourcesPage { anchors.fill: parent; comicsApi: null } }
    Component { id: mangaComponent; App.MangaTankobanSourcesPage { anchors.fill: parent } }

    function find(root, name) {
        if (!root) return null
        if (root.objectName === name) return root
        const kids = root.children || []
        for (let i = 0; i < kids.length; ++i) { const f = find(kids[i], name); if (f) return f }
        return null
    }
    function rows(n) {
        const out = []
        for (let i = 0; i < n; ++i)
            out.push({ infoHash: "h" + i, title: "Release " + i, confidence: "strong", magnetUri: "magnet:" + i,
                       evidence: ["TITLE"], sizeText: "100 MB", seeders: 5, coverage: true, uploader: "", trustTier: 1,
                       kind: "nyaa", volumeId: "v1", sizeBytes: 1, label: "Release " + i })
        return out
    }
    function build(component, w, h, android) {
        hostWindow.width = w; hostWindow.height = h; hostWindow.androidHost = android
        const page = createTemporaryObject(component, surface)
        verify(page !== null)
        page.context = { issueId: "i1", volumeId: "v1", seriesTitle: "Saga", editionTitle: "Saga: Book One", cover: "" }
        page.rows = rows(8)
        page.open = true
        waitForRendering(page)
        return page
    }
    function check(page, prefix, bannerName, listName, android, h) {
        const banner = find(page, bannerName)
        verify(banner !== null, bannerName)
        if (!android) { compare(banner.height, 300); return }
        const list = find(page, listName)
        verify(list !== null, listName)
        const r = list.mapToItem(page, 0, 0)
        verify(list.height >= 150, prefix + " list height " + list.height + " at h=" + h)
        verify(r.y + list.height <= page.height + 0.5, prefix + " list inside page")
        const title = find(page, prefix === "comic" ? "comicSourcesTitleBlock" : "tankobanSourcesTitleBlock")
        verify(title.y + title.height <= banner.height + 0.5, prefix + " title inside banner")
    }

    function test_budget_data() {
        return [
            { tag: "comic-zoom1.15", comp: "comic", w: 1237, h: 556, android: true },
            { tag: "comic-zoom1.20", comp: "comic", w: 1185, h: 533, android: true },
            { tag: "comic-desktop-short", comp: "comic", w: 1280, h: 600, android: false },
            { tag: "manga-zoom1.15", comp: "manga", w: 1237, h: 556, android: true },
            { tag: "manga-zoom1.20", comp: "manga", w: 1185, h: 533, android: true },
            { tag: "manga-desktop-short", comp: "manga", w: 1280, h: 600, android: false }
        ]
    }
    function test_budget(data) {
        if (data.comp === "comic") {
            const page = build(comicComponent, data.w, data.h, data.android)
            check(page, "comic", "comicSourcesBanner", "comicSourcesList", data.android, data.h)
        } else {
            const page = build(mangaComponent, data.w, data.h, data.android)
            check(page, "manga", "tankobanSourcesBanner", "tankobanSourcesList", data.android, data.h)
        }
    }
}
