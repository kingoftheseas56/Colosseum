import QtQuick
import QtTest
import "../../qml" as Colosseum

TestCase {
    id: testCase
    name: "AudiobookFlow"
    when: windowShown
    width: 1280
    height: 720

    Window {
        id: viewport
        width: testCase.width
        height: testCase.height
        visible: true
    }

    function item(id, title) {
        return { workId: id, recordingId: "edition-" + id, title: title, authors: ["Alex Writer"] }
    }

    Component {
        id: catalogComponent
        QtObject {
            property bool remote: false
            property int revision: 0

            function remotePending() { return remote && revision < 2 }
            function shelves() {
                return remote && revision < 1 ? [] : [{ key: "new-releases", title: "New releases" }]
            }
            function shelf(key, page) {
                if (key !== "new-releases" || (remote && revision < 2)) return {}
                return { items: [page === 0 ? item("first", "First Book") : item("second", "Second Book")],
                         page: page, hasMore: page === 0 }
            }
            function genres() {
                return remote && revision < 2 ? [] : [{ genre: "fantasy", count: 2, covers: [] }]
            }
            function genre(name, page) {
                if (name !== "fantasy" || (remote && revision < 2)) return {}
                return { items: [page === 0 ? item("second", "Second Book") : item("first", "First Book")],
                         page: page, hasMore: page === 0 }
            }
            function work(id) {
                if (remote && revision < 2) return {}
                if (id === "mercy-book-1" || id === "the-time-machine")
                    return { id: id, title: id === "mercy-book-1" ? "Mercy" : "The Time Machine",
                        authors: [{ name: "Alex Writer" }],
                        series: id === "mercy-book-1" ? [{ id: "series", name: "The Mercy Series", numberText: "1" }] : [],
                        recordings: [{ id: "edition-" + id, language: "en" }] }
                if (id !== "first" && id !== "second") return {}
                return { id: id, title: id === "first" ? "First Book" : "Second Book",
                         authors: [{ id: "writer", name: "Alex Writer" }],
                         series: [{ id: "series", name: "The Series" }],
                         recordings: [{ id: "edition-" + id, abridgement: 0, language: "en",
                                        narrators: [{ id: "n", name: "Reader One" }] }] }
            }
            function series(id) {
                return id === "series" ? { id: id, name: "The Series", works: [
                    { id: "first", title: "First Book", positionSort: 1 },
                    { id: "second", title: "Second Book", positionSort: 2 }
                ] } : {}
            }
            function byPerson(id, page) {
                return id === "writer" && page === 0 ? [{ id: "second", title: "Second Book" }] : []
            }
        }
    }

    Component {
        id: standaloneTab
        Colosseum.BiblioAudiobooksTab { width: testCase.width; active: true }
    }
    Component {
        id: worldComponent
        Colosseum.BiblioWorld { activeTab: "audiobooks" }
    }
    Component {
        id: flowComponent
        Colosseum.AudiobookFlow {}
    }
    Component {
        id: collectionComponent
        QtObject {
            property int revision: 0
            property var entry: null
            function has(world, id) { return world === "biblio" && entry !== null && entry.id === id }
            function add(world, item) { if (world === "biblio") { entry = item; ++revision } }
            function remove(world, id) { if (has(world, id)) { entry = null; ++revision } }
        }
    }

    function make(component, properties) {
        const object = createTemporaryObject(component, viewport.contentItem, properties || {})
        verify(object !== null)
        return object
    }

    function setup(remote) {
        const catalog = make(catalogComponent, { remote: !!remote })
        const world = make(worldComponent, { width: width, height: height, lifecycleActive: false })
        const tab = findChild(world, "biblioAudiobooksTab")
        tab.catalog = catalog
        world.lifecycleActive = true
        tryCompare(tab, "visible", true)
        compare(tab.shelfRows.length, remote ? 0 : 1)
        const collection = make(collectionComponent)
        return { world: world, tab: tab, catalog: catalog, collection: collection, flow: make(flowComponent, {
            width: width, height: height, world: world, catalog: catalog,
            collectionStore: collection, dockClearance: 80
        }) }
    }

    function openFirst(view) {
        const row = visualChild(view.tab, "audiobooksRow_new_releases")
        verify(row !== null)
        row.itemRequested(row.items[0])
        tryCompare(view.flow, "active", true)
        const page = view.flow.currentPage
        compare(findChild(page, "audiobookTitle").text, "First Book")
        return page
    }

    function visualChild(root, name) {
        if (root.objectName === name) return root
        for (const child of root.children) {
            const match = visualChild(child, name)
            if (match) return match
        }
        return null
    }

    function test_tab_refetches_shelves_and_genres_at_each_revision() {
        const catalog = make(catalogComponent, { remote: true })
        const tab = make(standaloneTab, { catalog: catalog })
        compare(tab.shelfRows.length, 0)
        compare(tab.genreTiles.length, 0)
        catalog.revision = 1
        compare(tab.shelfRows.length, 0)
        catalog.revision = 2
        tryVerify(() => tab.shelfRows.length === 1 && tab.genreTiles.length === 1)
        compare(tab.shelfRows[0].items[0].title, "First Book")
        compare(tab.genreTiles[0].name, "Fantasy")
    }

    function test_tab_item_opens_work() {
        const view = setup()
        const page = openFirst(view)
        compare(page.work.id, "first")
        compare(findChild(page, "audiobookNarrator").text, "Reader One")
        compare(page.shownRecording.facts.find(fact => fact.key === "language").text, "English")
        const heart = findChild(page, "audiobookSave")
        verify(heart.visible)
        heart.triggered()
        compare(view.collection.entry.payload.workId, "first")
        compare(view.collection.entry.type, "audiobook")
        verify(page.saved)
        heart.triggered()
        compare(view.collection.entry, null)
    }

    Component {
        id: sourceFixture
        QtObject {
            property var rows: []
            property var calls: []
            property var cancellations: []
            property bool autoReply: true
            signal searchFinished(string token, var rows, string error)
            function isEligible(work) { return !!work.id && Array.isArray(work.series) && work.series.length === 0 }
            function search(token, first, second, third) {
                calls.push({ token: token, first: first, second: second, third: third })
                if (autoReply) searchFinished(token, rows, "")
            }
            function cancel(token) { cancellations.push(token) }
        }
    }

    Component {
        id: sourceExtensionsFixture
        QtObject {
            property var sourceIds: []
            property int revision: 0
            function installed() { return sourceIds.map(id => ({ id: id, enabled: true })) }
        }
    }

    Component {
        id: sourceDownloadsFixture
        QtObject {
            property var captured: []
            function activeDownloads() { return [] }
            function recordingsForWork(key) { return [] }
            function statusOf(key) { return { state: "none" } }
            function downloadRecording(request) { captured.push(request) }
        }
    }

    Component {
        id: stalledAbbFixture
        QtObject {
            property int requests: 0
            property int cancellations: 0
            function search(term, page, done) {
                ++requests
                return { cancel: () => ++cancellations }
            }
        }
    }

    function test_audiobookbay_search_runs_with_tankorent_disabled() {
        const view = setup()
        view.flow.extensions = make(sourceExtensionsFixture, { sourceIds: ["colosseum.well.audiobookbay"] })
        view.flow.abb = make(stalledAbbFixture)
        view.flow.matcher = {}
        view.flow.downloads = make(sourceDownloadsFixture)
        view.flow.tankorent = make(sourceFixture)
        view.flow.openWork("the-time-machine")
        findChild(view.flow.currentPage, "audiobookPrimaryAction").triggered()
        const sources = view.flow.currentPage
        compare(sources.objectName, "audiobookSourcesPage")
        verify(sources.showTankorent)
        tryCompare(view.flow.abb, "requests", 1)
        compare(sources.providerStates.abb.state, "searching")
        compare(sources.providerStates.tankorent.state, "disabled")
        compare(view.flow.tankorent.calls.length, 0)
        view.flow.back()
        compare(view.flow.abb.cancellations, 1)
    }

    function test_tankorent_is_selectable_while_abb_is_pending() {
        const view = setup()
        view.flow.extensions = make(sourceExtensionsFixture, {
            sourceIds: ["colosseum.well.audiobookbay", "colosseum.well.indexers"]
        })
        view.flow.abb = make(stalledAbbFixture)
        view.flow.matcher = {}
        view.flow.downloads = make(sourceDownloadsFixture)
        const key = "the time machine|alex writer#src:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        view.flow.tankorent = make(sourceFixture, { rows: [{
            title: "The Time Machine upload", narrators: ["Reader Two"],
            request: { sourceKind: "tankorent", recordingKey: key }
        }] })
        view.flow.openWork("the-time-machine")
        const detail = view.flow.currentPage
        compare(detail.primary.kind, "download")
        findChild(detail, "audiobookPrimaryAction").triggered()
        const sources = view.flow.currentPage
        compare(sources.objectName, "audiobookSourcesPage")
        tryVerify(() => sources.providerStates.tankorent.state === "results")
        compare(Object.keys(sources.providerStates).sort(), ["abb", "tankorent"])
        compare(sources.providerStates.abb.state, "searching")
        compare(view.flow.abb.requests, 1)
        compare(sources.providerStates.tankorent.rows[0].narrators, ["Reader Two"])
        sources.sourcePicked({ request: { sourceKind: "retired", recordingKey: key } })
        compare(view.flow.downloads.captured.length, 0)
        const choice = visualChild(sources, "audiobookSourceOption_tankorent_0")
        verify(choice !== null)
        choice.triggered()
        compare(view.flow.downloads.captured.length, 1)
        compare(view.flow.downloads.captured[0].recordingKey, key)
        compare(view.flow.abb.cancellations, 1)
        compare(view.flow.currentPage, detail)
    }

    function test_tankorent_excludes_mercy_book_one_and_shows_for_a_standalone() {
        const view = setup()
        view.flow.extensions = make(sourceExtensionsFixture, { sourceIds: ["colosseum.well.indexers"] })
        view.flow.downloads = make(sourceDownloadsFixture)
        const key = "the time machine|alex writer#src:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        view.flow.tankorent = make(sourceFixture, { rows: [{
            title: "The Time Machine upload", narrators: [], reason: "Edition unconfirmed",
            request: { sourceKind: "tankorent", recordingKey: key }
        }] })

        view.flow.openWork("mercy-book-1")
        const mercy = view.flow.currentPage
        compare(mercy.built.title, "Mercy")
        findChild(mercy, "audiobookPrimaryAction").triggered()
        const mercySources = view.flow.currentPage
        compare(mercySources.objectName, "audiobookSourcesPage")
        verify(!mercySources.showTankorent)
        verify(!visualChild(mercySources, "audiobookSources_tankorent").visible)
        compare(mercySources.providerStates.abb.state, "disabled")
        compare(Object.keys(mercySources.providerStates), ["abb"])
        compare(view.flow.tankorent.calls.length, 0)
        view.flow.back()
        compare(view.flow.currentPage, mercy)
        view.flow.back()

        view.flow.openWork("the-time-machine")
        const standalone = view.flow.currentPage
        findChild(standalone, "audiobookPrimaryAction").triggered()
        const sources = view.flow.currentPage
        verify(sources.showTankorent)
        tryVerify(() => sources.providerStates.tankorent.state === "results")
        compare(view.flow.tankorent.calls[0].first.id, "the-time-machine")
        compare(view.flow.tankorent.calls[0].third, "the time machine|alex writer")
        compare(view.flow.downloads.captured.length, 0)
        compare(sources.providerStates.tankorent.rows[0].narrators, [])
        const choice = visualChild(sources, "audiobookSourceOption_tankorent_0")
        verify(choice !== null)
        choice.triggered()
        compare(view.flow.downloads.captured[0].recordingKey, key)
    }

    function test_disabling_tankorent_cancels_and_rejects_its_late_results() {
        const view = setup()
        const extensions = make(sourceExtensionsFixture, { sourceIds: ["colosseum.well.indexers"] })
        view.flow.extensions = extensions
        view.flow.downloads = make(sourceDownloadsFixture)
        const source = make(sourceFixture, { autoReply: false })
        view.flow.tankorent = source
        view.flow.openWork("the-time-machine")
        const detail = view.flow.currentPage
        findChild(detail, "audiobookPrimaryAction").triggered()
        const sources = view.flow.currentPage
        tryVerify(() => source.calls.length === 1)
        const token = source.calls[0].token
        extensions.sourceIds = []
        ++extensions.revision
        tryVerify(() => sources.providerStates.tankorent.state === "disabled")
        compare(source.cancellations, [token])
        source.searchFinished(token, [{ title: "An old result", request: { sourceKind: "tankorent" } }], "")
        compare(sources.providerStates.tankorent.state, "disabled")
        compare(sources.providerStates.tankorent.rows.length, 0)
        compare(view.flow.downloads.captured.length, 0)
        findChild(sources, "audiobookSourcesBack").triggered()
        compare(view.flow.currentPage, detail)
    }

    function test_catalogue_edition_flag_has_a_readable_label() {
        const page = openFirst(setup())
        compare(page.shownRecording.facts.find(fact => fact.key === "edition").text, "Unabridged")
    }

    function test_series_cover_swaps_work() {
        const view = setup()
        const page = openFirst(view)
        const cover = visualChild(page, "audiobookSeries_second")
        verify(cover !== null)
        cover.triggered()
        compare(view.flow.currentPage, page)
        tryCompare(findChild(page, "audiobookTitle"), "text", "Second Book")
    }

    function test_back_returns_to_retained_tab_and_scroll() {
        const view = setup()
        const flick = view.world.pageFlickable
        tryVerify(() => flick.contentHeight - flick.height >= 190)
        flick.contentY = 190
        const page = openFirst(view)
        findChild(page, "audiobookBack").triggered()
        tryCompare(view.flow, "active", false)
        compare(view.world.activeTab, "audiobooks")
        compare(flick.contentY, 190)
        view.world.audiobookRequested({
            recordingKey: "legacy-download", title: "Local Audiobook", author: "Alex Writer",
            downloaded: true
        })
        const localPage = view.flow.currentPage
        compare(localPage.built.title, "Local Audiobook")
        compare(localPage.built.workId, "")
        compare(localPage.primary.kind, "listen")
    }

    function test_catalogue_browse_data() {
        return [
            { tag: "shelf", title: "New releases", first: "First Book", second: "Second Book" },
            { tag: "genre", title: "Fantasy", first: "Second Book", second: "First Book" }
        ]
    }

    function test_catalogue_browse(data) {
        const view = setup()
        if (data.tag === "shelf") {
            const row = visualChild(view.tab, "audiobooksRow_new_releases")
            row.seeAllRequested(row.seeAllPin)
        } else {
            findChild(view.tab, "audiobooksGenreMosaic").genreClicked(0)
        }
        const browser = view.flow.currentPage
        compare(browser.titleText, data.title)
        compare(browser.height, height - view.flow.dockClearance)
        compare(browser.items.map(card => card.title), [data.first])
        verify(browser.hasMore)
        browser.requestMore()
        tryVerify(() => browser.items.length === 2)
        compare(browser.items.map(card => card.title), [data.first, data.second])
        compare(browser.hasMore, false)
        const grid = findChild(browser, "theatreSeeAllPosterWall")
        verify(grid.squareArtwork)
        grid.itemRequested(grid.items[1])
        tryCompare(findChild(view.flow.currentPage, "audiobookTitle"), "text", data.second)
        view.flow.back()
        compare(view.flow.currentPage, browser)
        compare(browser.items.length, 2)
    }

    function test_author_opens_works_and_back_returns_to_book() {
        const view = setup()
        const page = openFirst(view)
        const author = findChild(page, "audiobookAuthor")
        const y = Math.round(author.height / 2)
        let x = 0
        while (x < author.width && author.linkAt(x, y) !== "writer") ++x
        verify(x < author.width, "The credited author must have a clickable link")
        verify(waitForRendering(author), "The audiobook page must have rendered before clicking")
        mouseClick(author, x + 2, y)
        tryVerify(() => view.flow.currentPage && view.flow.currentPage.titleText === "Alex Writer")
        compare(view.flow.currentPage.items.map(card => card.title), ["Second Book"])
        view.flow.back()
        compare(view.flow.currentPage, page)
    }

    function test_pending_genre_and_book_wait_for_catalogue_revision() {
        const view = setup(true)
        view.world.audiobookGenreRequested("fantasy")
        const browser = view.flow.currentPage
        compare(browser.loading, true)
        compare(browser.items.length, 0)
        view.world.audiobookRequested(item("first", "First Book"))
        const page = view.flow.currentPage
        compare(page.catalogueStatus, "Loading audiobook…")
        view.catalog.revision = 2
        tryCompare(findChild(page, "audiobookTitle"), "text", "First Book")
        compare(browser.loading, false)
        compare(browser.items[0].title, "Second Book")
    }

    function test_missing_cover_uses_house_placeholder() {
        const view = setup()
        const page = openFirst(view)
        const cover = findChild(page, "audiobookCover").children[0]
        verify(cover.placeholderVisible)
        compare(cover.activeSource.toString(), "")
        const placeholder = findChild(cover, "biblioCoverPlaceholder")
        verify(placeholder && placeholder.visible)
        compare(placeholder.title, "First Book")
    }

    function test_local_editorial_bake() {
        if (typeof AudiobookCatalog === "undefined")
            skip("Set COLOSSEUM_AUDIOBOOK_CATALOG_PATH to run against the editorial bake")
        verify(AudiobookCatalog.ready(), "The configured editorial bake did not open")
        const tab = make(standaloneTab)
        compare(tab.shelfRows.length, 5)
        compare(tab.shelfRows[0].title, "New releases")
        compare(tab.shelfRows[1].title, "Fully dramatized")
        for (let i = 0; i < tab.shelfRows.length; ++i) {
            const row = tab.shelfRows[i]
            if (i >= 2) verify(row.title.startsWith("Narrated by "))
            verify(row.items.length > 0 && row.items[0].title.length > 0)
        }
        verify(tab.genreTiles.length > 0)
    }
}
