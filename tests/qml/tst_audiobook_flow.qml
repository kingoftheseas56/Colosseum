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

    Component {
        id: previousPageComponent
        Rectangle { color: "#ff00ff" }
    }

    function test_phoneBookPaintsBehindTheDockAndClearsTheLastShelf() {
        const width = 750
        const height = 450
        const dockInset = 53
        const previous = make(previousPageComponent, { width: width, height: height })
        const flow = make(flowComponent, {
            width: width, height: height, androidHost: true,
            catalog: make(catalogComponent), dockClearance: dockInset
        })

        flow.openWork("first", "edition-first")
        const book = flow.currentPage
        const scroll = findChild(book, "audiobookScroll")
        compare(book.height, height)
        compare(scroll.y + scroll.height, height)
        verify(waitForRendering(book))
        const pagePixel = grabImage(viewport.contentItem).pixel(width - 24, height - 2)
        previous.color = "#00ff00"
        verify(waitForRendering(previous))
        compare(grabImage(viewport.contentItem).pixel(width - 24, height - 2), pagePixel)

        const lastBook = visualChild(book, "audiobookSeries_second")
        verify(lastBook)
        scroll.contentY = scroll.contentHeight - scroll.height
        verify(lastBook.mapToItem(book, 0, lastBook.height).y <= height - dockInset)

        const withDock = scroll.contentHeight
        flow.dockClearance = 0
        compare(scroll.contentHeight, withDock - dockInset)

        book.backRequested()
        compare(flow.active, false)
        verify(waitForRendering(previous))
        compare(grabImage(viewport.contentItem).pixel(width - 24, height - 2), previous.color)
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
            property var failures: []
            property var replacements: []
            property var activeJob: null
            property bool simulateJobs: false
            signal activeCountChanged()
            function activeDownloads() { return activeJob ? [activeJob].concat(failures) : failures }
            function replaceStalledDownload(key, attempt, request, token) {
                replacements.push([key, attempt, request, token])
                if (simulateJobs) {
                    activeJob = jobFor(request, token)
                    failures = []
                    activeCountChanged()
                }
                return true
            }
            function recordingsForWork(key) { return [] }
            function statusOf(key) { return { state: activeJob && activeJob.id === key ? activeJob.state : "none" } }
            function jobFor(request, token) {
                return Object.assign({ id: request.recordingKey, state: "resolving", smartIntent: token }, request)
            }
            function downloadRecording(request, token) {
                captured.push(request)
                if (simulateJobs) {
                    activeJob = jobFor(request, token)
                    activeCountChanged()
                }
                return true
            }
            function fail(attempt, phase) {
                const failed = Object.assign({}, activeJob, { attempt: attempt, state: "failed", stallPhase: phase })
                if (!phase) delete failed.smartIntent
                activeJob = null
                failures = [failed]
                activeCountChanged()
            }
            function cancel() {
                activeJob = null
                activeCountChanged()
            }
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

    function test_replacement_picker_data() {
        return [{ tag: "catalogue", workId: "first" }, { tag: "title and author", workId: "" }]
    }

    function test_replacement_picker(data) {
        const view = setup()
        const flow = view.flow
        const original = "a".repeat(40)
        const alternative = "b".repeat(40)
        const wrongBook = "c".repeat(40)
        const pairKey = "first book|alex writer"
        const key = pairKey + (data.workId ? "#as:edition-first" : "#src:" + original)
        const failure = { state: "failed", id: key, attempt: "7", pairKey: pairKey, workId: data.workId,
            title: data.workId ? "First Book (Legacy Title)" : "First Book",
            author: data.workId ? "Alix Writer" : "Alex Writer", infoHash: original, sourceRef: "stalled",
            audiosiloRecordingId: data.workId ? "edition-first" : "" }
        const downloads = make(sourceDownloadsFixture, { failures: [failure] })
        flow.downloads = downloads
        flow.extensions = make(sourceExtensionsFixture, { sourceIds: ["colosseum.well.audiobookbay"] })

        const pending = make(stalledAbbFixture)
        flow.abb = pending
        verify(!flow.openReplacement(key, "stale"))
        verify(flow.openReplacement(key, "7"))
        verify(flow.replacingDownload)
        compare(pending.requests, 1)
        compare(flow.currentPage.providerStates.abb.state, "searching")
        flow.back()
        compare(pending.cancellations, 1)
        verify(!flow.replacingDownload)
        compare(downloads.replacements.length, 0)
        compare(downloads.failures.length, 1)

        const stubRows = [
            { slug: "stalled", title: "First Book", author: "Alex Writer" },
            { slug: "same-hash", title: "First Book", author: "Alex Writer" },
            { slug: "different-book", title: "Second Book", author: "Alex Writer" },
            { slug: "alternate", title: "First Book", author: "Alex Writer" },
            { slug: "alternate-mirror", title: "First Book", author: "Alex Writer" }
        ]
        let seen = []
        const torrentBySlug = { "same-hash": original, "different-book": wrongBook,
            "alternate": alternative, "alternate-mirror": alternative }
        flow.matcher = { matchSource: (source, editions) => {
            return source.title === "first book" && editions.length
                ? { verdict: "exact", recordingKey: editions[0].recordingKey } : {}
        } }
        flow.abb = {
            search: (term, page, done) => {
                compare(term, "first book")
                done({ rows: stubRows })
                return { cancel: () => {} }
            },
            detail: (slug, done) => {
                seen.push(slug)
                done({ infoHash: torrentBySlug[slug], author: "Alex Writer", narrator: ["Reader One"] })
                return { cancel: () => {} }
            }
        }
        verify(flow.openReplacement(key, "7"))
        const picker = flow.currentPage
        compare(picker.workTitle, "First Book")
        compare(picker.workAuthor, "Alex Writer")
        compare(picker.backTip, "Back to Downloads")
        compare(picker.providerStates.abb.rows.length, 1)
        verify(!seen.includes("stalled"))
        compare(downloads.captured.length, 0)
        compare(downloads.replacements.length, 0)
        const option = findChild(picker, "audiobookSourceOption_abb_0")
        verify(option.activate())
        compare(downloads.replacements.length, 1)
        compare(downloads.replacements[0][0], key)
        compare(downloads.replacements[0][1], "7")
        compare(downloads.replacements[0][2].infoHash, alternative)
        compare(downloads.replacements[0][2].workId || "", data.workId)
        compare(downloads.replacements[0][2].recordingKey, data.workId ? key : pairKey + "#src:" + alternative)
        verify(!flow.replacingDownload)

        stubRows.splice(1)
        verify(flow.openReplacement(key, "7"))
        compare(flow.currentPage.providerStates.abb.state, "empty")
        compare(flow.currentPage.providerStates.abb.message, "No other releases found for this audiobook.")
        flow.back()
        flow.abb = { search: (term, page, done) => { done(null, "timeout"); return { cancel: () => {} } } }
        verify(flow.openReplacement(key, "7"))
        compare(flow.currentPage.providerStates.abb.state, "error")
        compare(flow.currentPage.providerStates.abb.error, "timeout")
        downloads.failures = []
        compare(flow.currentPage.providerStates.abb.state, "empty")
        compare(flow.currentPage.providerStates.abb.message, "This stalled download is no longer in Downloads.")
    }

    function smartFixture() {
        const view = setup()
        const flow = view.flow
        flow.extensions = make(sourceExtensionsFixture, { sourceIds: ["colosseum.well.audiobookbay"] })
        const downloads = make(sourceDownloadsFixture, { simulateJobs: true })
        flow.downloads = downloads
        flow.matcher = { matchSource: (source, editions) => source.narrators.includes("Reader One")
            ? { verdict: "probable", recordingKey: editions[0].recordingKey } : { verdict: "none" } }
        const provider = { lookups: 0, choices: ["a"] }
        function resolveChoices(done) {
            done({ rows: provider.choices.map(choice => ({ slug: choice, title: "First Book", author: "Alex Writer" })) })
        }
        flow.abb = {
            search: (term, page, done) => {
                ++provider.lookups
                if (provider.hold) {
                    provider.reply = () => resolveChoices(done)
                    return { cancel: () => { provider.cancelled = true } }
                }
                resolveChoices(done)
                return { cancel: () => {} }
            },
            detail: (slug, done) => {
                const hash = slug === "first-alias" ? "a" : slug
                done({ infoHash: hash.repeat(40), author: "Alex Writer",
                    narrator: [slug === "d" && provider.different ? "Other Reader" : "Reader One"],
                    language: "English", abridgement: "unabridged", audioFileCount: 1 })
                return { cancel: () => {} }
            }
        }
        flow.openWork("first", "edition-first")
        const detail = flow.currentPage
        findChild(detail, "audiobookPrimaryAction").triggered()
        compare(downloads.captured.length, 1)
        verify(downloads.activeJob.smartIntent)
        return { flow: flow, downloads: downloads, provider: provider, detail: detail }
    }

    function test_smart_stall_retry_limit_and_no_navigation() {
        const { flow, downloads, provider } = smartFixture()
        flow.clear()
        provider.choices = ["first-alias", "b"]
        downloads.fail("1", "file-list")
        tryVerify(() => downloads.replacements.length === 1)
        compare(downloads.replacements[0][2].infoHash, "b".repeat(40))
        verify(!flow.active)
        flow.openWork("second")
        const other = flow.currentPage
        provider.choices = ["first-alias", "c"]
        downloads.fail("2", "audio-data")
        tryVerify(() => downloads.replacements.length === 2)
        compare(downloads.replacements[1][2].infoHash, "c".repeat(40))
        compare(flow.currentPage, other)
        provider.choices = ["d"]
        downloads.fail("3", "file-list")
        flow.openWork("first", "edition-first")
        tryVerify(() => flow.currentPage.primary.label === "Choose another release")
        compare(provider.lookups, 3)
        compare(downloads.replacements.length, 2)
        compare(downloads.activeJob, null)
    }

    function test_smart_stall_explicit_choice() {
        const { flow, detail, downloads, provider } = smartFixture()
        provider.choices = ["d"]
        provider.different = true
        downloads.fail("1", "audio-data")
        tryCompare(detail, "smartRecoveryRequired", true)
        compare(flow.currentPage, detail)
        compare(detail.primary.label, "Choose another release")
        compare(downloads.replacements.length, 0)
        findChild(detail, "audiobookPrimaryAction").triggered()
        const picker = flow.currentPage
        compare(picker.backTip, "Back to audiobook details")
        compare(downloads.replacements.length, 0)
        verify(findChild(picker, "audiobookSourceOption_abb_0").activate())
        compare(downloads.replacements.length, 1)
        verify(!downloads.activeJob.smartIntent)
        compare(flow.currentPage, detail)
        downloads.fail("2", "file-list")
        wait(25)
        compare(provider.lookups, 3)
        compare(downloads.replacements.length, 1)
    }

    function test_smart_stall_stops_data() {
        return [{ tag: "cancel" }, { tag: "dismiss" }, { tag: "replacement" },
                { tag: "provider-disabled" }, { tag: "non-stall" }]
    }

    function test_smart_stall_stops(data) {
        const { flow, downloads, provider } = smartFixture()
        provider.choices = ["b"]
        if (data.tag === "cancel") downloads.cancel()
        else if (data.tag === "non-stall") downloads.fail("1", "")
        else {
            provider.hold = true
            downloads.fail("1", "file-list")
            tryVerify(() => !!provider.reply)
            const lateReply = provider.reply
            if (data.tag === "dismiss") downloads.failures = []
            else if (data.tag === "replacement") {
                verify(flow.openReplacement(downloads.failures[0].id, "1"))
                flow.back()
            } else {
                flow.extensions.sourceIds = []
                ++flow.extensions.revision
            }
            tryVerify(() => provider.cancelled)
            lateReply()
        }
        wait(25)
        compare(downloads.replacements.length, 0)
        if (data.tag === "cancel" || data.tag === "non-stall") compare(provider.lookups, 1)
    }

    function test_indexers_do_not_enable_audiobook_downloads() {
        const view = setup()
        const extensions = make(sourceExtensionsFixture, { sourceIds: ["colosseum.well.indexers"] })
        view.flow.extensions = extensions
        view.flow.abb = make(stalledAbbFixture)
        view.flow.matcher = {}
        view.flow.downloads = make(sourceDownloadsFixture)
        view.flow.openWork("the-time-machine")
        const detail = view.flow.currentPage
        const action = findChild(detail, "audiobookPrimaryAction")
        compare(detail.primary.kind, "extensions")
        action.triggered()
        compare(view.flow.currentPage, detail)
        compare(view.flow.abb.requests, 0)

        extensions.sourceIds = ["colosseum.well.indexers", "colosseum.well.audiobookbay"]
        ++extensions.revision
        action.triggered()
        compare(detail.primary.kind, "lookup")
        compare(view.flow.abb.requests, 1)

        extensions.sourceIds = ["colosseum.well.indexers"]
        ++extensions.revision
        compare(view.flow.abb.cancellations, 1)
        compare(detail.primary.kind, "extensions")
        compare(view.flow.downloads.captured.length, 0)
    }

    function test_catalogue_edition_flag_has_a_readable_label() {
        const page = openFirst(setup())
        compare(page.shownRecording.facts.find(fact => fact.key === "edition").text, "Unabridged")
    }

    function test_desktop_detail_uses_space_beside_collapsed_dock() {
        const view = setup()
        const page = openFirst(view)
        compare(page.height, height - view.flow.dockClearance)
        view.flow.dockExpanded = false
        tryCompare(page, "height", height)
        view.flow.dockExpanded = true
        tryCompare(page, "height", height - view.flow.dockClearance)
        compare(findChild(page, "audiobookDetailBackdrop").height, height)
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
        verify(!grid.squareArtwork)
        verify(grid.fitArtwork)
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
