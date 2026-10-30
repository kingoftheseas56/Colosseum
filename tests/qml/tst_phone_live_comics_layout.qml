import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as App

TestCase {
    id: testCase
    name: "PhoneLiveComicSeriesLayout"
    when: windowShown

    readonly property string seriesTitleFixture: "Crisis on Infinite Earths — The Multiverse at War"
    readonly property url posterFixture: Qt.resolvedUrl("../../assets/wallpaper/sagas/dune.jpg")
    readonly property var releaseFixtures: [
        { id: "fixture-collected-deluxe", name: "Collected Edition — Infinite Crisis Deluxe", cover: posterFixture, year: 2024, sizeMB: 182, collection: true, url: "" },
        { id: "fixture-issue-3", name: "Issue #3 — Crisis in Gotham", cover: posterFixture, year: 2023, sizeMB: 44, collection: false, url: "" },
        { id: "fixture-collected-origins", name: "Collected Edition — Gotham Origins", cover: posterFixture, year: 2022, sizeMB: 156, collection: true, url: "" },
        { id: "fixture-issue-1", name: "Issue #1 — The First Signal", cover: posterFixture, year: 2018, sizeMB: 38, collection: false, url: "" },
        { id: "fixture-issue-2", name: "Issue #2 — Another Dawn", cover: posterFixture, year: 2020, sizeMB: 41, collection: false, url: "" },
        { id: "fixture-issue-4", name: "Issue #4 — The Final Hour", cover: posterFixture, year: 2025, sizeMB: 49, collection: false, url: "" }
    ]
    readonly property var dbLedgerSeriesFixture: ({
        title: "Crisis on Infinite Earths",
        publisher: "DC Comics",
        cover: posterFixture,
        synopsis: "Across a collapsing multiverse, heroes from countless worlds gather for one final defense. This deliberately long local synopsis exercises wrapped hero copy without requesting any remote metadata.",
        rank: 7,
        coverage: ({ availabilityState: "partial" }),
        editions: [
            {
                locg_comic_id: "db-fixture-deluxe",
                title: "Crisis on Infinite Earths: The Long Fall of the Silver City",
                display_title: "Crisis on Infinite Earths: The Long Fall of the Silver City",
                format: "Deluxe",
                pages: 768,
                published: "October 2024",
                description: "A long, local edition description with enough words to wrap across the narrow phone text column while remaining bounded by its card.",
                creators: "Marv Wolfman",
                cover: posterFixture,
                sources: [{ fanMade: true, available: false, format: "Community Archive",
                            title: "Long community mirror record kept for catalogue context only" }],
                available: false
            },
            {
                locg_comic_id: "db-fixture-omnibus",
                title: "Crisis on Infinite Earths: Complete Omnibus Edition",
                display_title: "Crisis on Infinite Earths: Complete Omnibus Edition",
                format: "Omnibus",
                pages: 1296,
                published: "March 2025",
                description: "A second long description exercises the other edition card with local presentation data.",
                creators: "George Pérez",
                cover: posterFixture,
                sources: [],
                available: false
            }
        ]
    })

    Window {
        id: host
        width: 354
        height: 792
        visible: true
        property bool androidHost: true
    }

    Component {
        id: comicSeriesComponent
        App.ComicSeries {
            anchors.fill: parent
            androidHost: true
            seriesTitle: ""
            poster: testCase.posterFixture
            bakedReleases: testCase.releaseFixtures
            gcdId: 7421
            comicsRef: null
            progressRef: null
        }
    }

    Component {
        id: locgComicSeriesComponent
        App.ComicSeriesPage {
            anchors.fill: parent
            androidHost: true
            seriesTitle: testCase.seriesTitleFixture
            cover: testCase.posterFixture
            locgId: ""
            locgMeta: ({ publisher: "DC Comics", startYear: 1985 })
            loading: false
        }
    }

    App.Theme { id: ledgerThemeFixture }

    Component {
        id: dbLedgerComponent
        App.ComicDbLedger {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            y: 0
            androidHost: true
            theme: ledgerThemeFixture
            dbSeries: testCase.dbLedgerSeriesFixture
            seriesTitle: testCase.dbLedgerSeriesFixture.title
            gcTag: "gc:fixture"
            libraryEntry: null
        }
    }

    property var comic: null
    property var locgComic: null
    property var dbLedger: null

    function init() {
        host.width = 354
        host.height = 792
        host.androidHost = true
        comic = comicSeriesComponent.createObject(host.contentItem)
        verify(comic !== null, "the actual live App.ComicSeries component instantiates")
        wait(0)
        compare(comic.androidHost, true, "the live component exposes an Android-host override")
        comic.seriesTitle = seriesTitleFixture
        tryCompare(comic, "loading", false, 2000)
        compare(comic.comicsRef, null, "the fixture uses no Comics backend")
        compare(comic.progressRef, null, "the fixture uses no Progress backend")
        verify(comic.poster.length > 0, "a local existing poster fixture prevents poster lookup")
        wait(30)
    }

    function cleanup() {
        if (dbLedger)
            dbLedger.destroy()
        dbLedger = null
        if (locgComic)
            locgComic.destroy()
        locgComic = null
        if (comic)
            comic.destroy()
        comic = null
        wait(0)
    }

    function findChild(parent, objectName) {
        if (!parent)
            return null
        if (parent.objectName === objectName)
            return parent
        const children = parent.children || []
        for (let i = 0; i < children.length; ++i) {
            const found = findChild(children[i], objectName)
            if (found)
                return found
        }
        return null
    }

    function findDescendant(parent, predicate) {
        if (!parent)
            return null
        if (predicate(parent))
            return parent
        const children = parent.children || []
        for (let i = 0; i < children.length; ++i) {
            const found = findDescendant(children[i], predicate)
            if (found)
                return found
        }
        return null
    }

    function rectIn(item, target) {
        const point = item.mapToItem(target, 0, 0)
        return { x: point.x, y: point.y, width: item.width, height: item.height }
    }

    function overlaps(first, second) {
        return first.x < second.x + second.width && second.x < first.x + first.width
            && first.y < second.y + second.height && second.y < first.y + first.height
    }

    function textIn(parent, text) {
        return findDescendant(parent, function(item) { return item.text === text })
    }

    function touchItem(item) {
        const root = host.contentItem
        const point = item.mapToItem(root, item.width / 2, item.height / 2)
        touchEvent(root).press(0, root, point.x, point.y).commit()
        wait(20)
        touchEvent(root).release(0, root, point.x, point.y).commit()
        wait(30)
    }

    function setViewport(width, height, androidHost) {
        host.width = width
        host.height = height
        host.androidHost = androidHost
        comic.androidHost = androidHost
        wait(40)
        compare(comic.width, width)
        compare(comic.height, height)
        compare(comic.androidHost, androidHost)
        compare(comic.seriesTitle, seriesTitleFixture)
        compare(comic.loading, false)
    }

    function assertPhoneHeroAndControls(width, height) {
        setViewport(width, height, true)

        const scroll = findChild(comic, "comicSeriesScroll")
        const hero = findChild(comic, "comicSeriesHeroContent")
        const title = findChild(comic, "comicSeriesHeroTitle")
        const metadata = findChild(comic, "comicSeriesHeroMetadata")
        const search = findChild(comic, "comicSeriesSearch")
        const sortStrip = findChild(comic, "comicSeriesSortStrip")
        const desktopControls = findChild(comic, "comicSeriesDesktopWindowControls")
        verify(scroll && hero && title && metadata && search && sortStrip && desktopControls,
               "the actual live ComicSeries exposes its named phone layout regions")
        verify(scroll.clip, "the page scroll viewport clips its content")
        compare(desktopControls.visible, false,
                "Android hides desktop minimize/fullscreen/close controls")
        compare(comic.phoneLayout, true, "the Android host activates the phone layout gate")
        compare(comic.phoneNarrow, width < 840 || height < 500,
                "the compact detail follows the shared usable-page class")
        compare(hero.columns, 1,
                "compact phone and tablet details stack cover and copy")

        scroll.contentY = 0
        wait(0)
        const heroRect = rectIn(hero, scroll.contentItem)
        const titleRect = rectIn(title, hero)
        const metadataRect = rectIn(metadata, hero)
        verify(heroRect.x >= -1 && heroRect.y >= -1
               && heroRect.x + heroRect.width <= scroll.width + 1
               && heroRect.y + heroRect.height <= scroll.contentHeight + 1,
               "the phone hero content stays inside the scroll width at " + width + "x" + height)
        verify(titleRect.x >= -1 && titleRect.y >= -1
               && titleRect.x + titleRect.width <= hero.width + 1
               && titleRect.y + titleRect.height <= hero.height + 1,
               "the phone series title stays inside its hero content")
        verify(metadataRect.x >= -1 && metadataRect.y >= -1
               && metadataRect.x + metadataRect.width <= hero.width + 1
               && metadataRect.y + metadataRect.height <= hero.height + 1,
               "phone hero metadata stays inside its hero content")
        verify(!overlaps(titleRect, metadataRect),
               "phone hero title and metadata do not overlap")

        const searchRect = rectIn(search, scroll)
        const sortRect = rectIn(sortStrip, scroll)
        verify(search.visible && search.height >= 48,
               "phone release search has a visible 48dp row target")
        verify(sortStrip.visible && sortStrip.height >= 48,
               "phone release sort has a separate visible 48dp row target")
        verify(searchRect.x >= -1 && searchRect.x + searchRect.width <= scroll.width + 1
               && sortRect.x >= -1 && sortRect.x + sortRect.width <= scroll.width + 1,
               "phone search and sort stay within the scroll viewport width")
        verify(!overlaps(searchRect, sortRect),
               "phone search and sort occupy separate non-overlapping rows")
        verify(searchRect.y + searchRect.height <= sortRect.y + 1,
               "phone sort row follows the search row")
        return { scroll: scroll, hero: hero, title: title, metadata: metadata,
                 search: search, sortStrip: sortStrip }
    }

    function assertReleaseBounds(width, height) {
        setViewport(width, height, true)
        const scroll = findChild(comic, "comicSeriesScroll")
        verify(scroll !== null)
        scroll.contentY = 0
        wait(0)
        for (let i = 0; i < releaseFixtures.length; ++i) {
            const release = releaseFixtures[i]
            const row = findChild(comic, "comicReleaseRow_" + release.id)
            const read = findChild(comic, "comicReadAction_" + release.id)
            const download = findChild(comic, "comicDownloadAction_" + release.id)
            verify(row && read && download,
                   "release row, Read, and download controls exist for " + release.id)
            const rowRect = rectIn(row, scroll)
            verify(rowRect.x >= -1 && rowRect.x + rowRect.width <= scroll.width + 1,
                   release.name + " row stays within the phone scroll width")
            const readRect = rectIn(read, row)
            const downloadRect = rectIn(download, row)
            verify(readRect.x >= -1 && readRect.y >= -1
                   && readRect.x + readRect.width <= row.width + 1
                   && readRect.y + readRect.height <= row.height + 1,
                   release.name + " Read action stays inside its row: " + JSON.stringify(readRect))
            verify(downloadRect.x >= -1 && downloadRect.y >= -1
                   && downloadRect.x + downloadRect.width <= row.width + 1
                   && downloadRect.y + downloadRect.height <= row.height + 1,
                   release.name + " download action stays inside its row: " + JSON.stringify(downloadRect))
            verify(read.height >= 48 && download.width >= 48 && download.height >= 48,
                   release.name + " phone Read and download actions meet 48dp targets")
            const title = textIn(row, release.name)
            verify(title && title.visible, release.name + " release text is visible")
            const titleRect = rectIn(title, row)
            verify(titleRect.x >= -1 && titleRect.y >= -1
                   && titleRect.x + titleRect.width <= row.width + 1
                   && titleRect.y + titleRect.height <= row.height + 1,
                   release.name + " text stays inside its release row")
            verify(!overlaps(readRect, downloadRect),
                   release.name + " Read and download actions have separate hit areas")
        }
    }

    function test_android_host_layout_gate_is_writable() {
        compare(comic.androidHost, true)
        comic.androidHost = false
        compare(comic.androidHost, false, "the Android-host gate is writable")
        comic.androidHost = true
        compare(comic.androidHost, true, "the Android-host gate can be enabled for phone layout")
    }

    function test_phone_portrait_and_landscape_hero_and_release_bounds() {
        assertPhoneHeroAndControls(354, 792)
        assertReleaseBounds(354, 792)
        assertPhoneHeroAndControls(792, 354)
        assertReleaseBounds(792, 354)
    }

    function test_phone_search_and_sort_use_real_touch_without_acquire_actions() {
        const layout = assertPhoneHeroAndControls(354, 792)
        const search = layout.search
        const sortStrip = layout.sortStrip
        const oldest = findChild(sortStrip, "comicSeriesSort_old")
        verify(oldest !== null, "the real Oldest sort option is present")
        verify(oldest.height >= 48, "the real Oldest sort touch target is at least 48dp tall")

        host.requestActivate()
        wait(50)
        touchItem(search)
        verify(search.activeFocus,
               "touch places focus in the actual release search field (focus=" + search.focus
               + ", host.active=" + host.active + ", activeFocusItem=" + host.activeFocusItem + ")")
        search.text = "Issue #2"
        compare(comic.filter, "Issue #2")
        compare(comic.shown.length, 1, "search filters the baked release list locally")
        compare(comic.shown[0].id, "fixture-issue-2")

        search.text = ""
        compare(comic.filter, "")
        touchItem(oldest)
        tryCompare(comic, "sortMode", "old")
        compare(comic.shown[0].id, "fixture-issue-1",
                "touching Oldest sorts the visible fixture releases locally")
        compare(comic.openChapterId, "", "search and sort do not open the reader")
        compare(comic.pendingReadActive, false, "search and sort do not start a Read intent")
    }

    function test_phone_focused_search_reveals_below_fixed_back_through_rotation() {
        comic.bakedReleases = [releaseFixtures[0]]
        wait(40)
        compare(comic.shown.length, 1, "a single local release keeps this a short-content case")

        let layout = assertPhoneHeroAndControls(354, 792)
        const search = layout.search
        const scroll = layout.scroll
        const back = findDescendant(comic, function(item) {
            return item && item.variant === "plain" && item.label === "Back"
        })
        verify(back !== null, "the real fixed Back action is available for the safe-area bound")

        host.requestActivate()
        wait(50)
        touchItem(search)
        verify(search.activeFocus,
               "genuine phone touch focuses search before reveal (host.active=" + host.active
               + ", activeFocusItem=" + host.activeFocusItem + ")")

        function verifySearchReveal(orientation) {
            const backRect = rectIn(back, host.contentItem)
            const searchRect = rectIn(search, host.contentItem)
            verify(scroll.contentY > 0,
                   orientation + " moves the short shelf to reveal focused search")
            verify(scroll.contentHeight - scroll.height >= scroll.contentY - 1,
                   orientation + " provides enough content extent for the reveal")
            verify(searchRect.y >= backRect.y + backRect.height + 8,
                   orientation + " keeps search below the fixed Back hit area")
            verify(searchRect.y <= 88,
                   orientation + " places search near its 80dp reveal position")
        }

        verifySearchReveal("portrait")
        setViewport(792, 354, true)
        compare(search.activeFocus, true,
                "rotation to short landscape retains focus in the real search field")
        verifySearchReveal("landscape")
    }

    function test_desktop_keeps_wide_hero_and_window_controls() {
        setViewport(1280, 800, false)
        const hero = findChild(comic, "comicSeriesHeroContent")
        const title = findChild(comic, "comicSeriesHeroTitle")
        const controls = findChild(comic, "comicSeriesDesktopWindowControls")
        verify(hero && title && controls,
               "wide desktop regions remain present in actual ComicSeries")
        compare(controls.visible, true, "desktop window controls remain visible")
        compare(comic.phoneLayout, false, "the desktop viewport retains desktop layout")
        compare(hero.columns, 2, "desktop hero retains its two-column composition")
        compare(title.font.pixelSize, 54, "desktop series title retains its existing type scale")
        const titleRect = rectIn(title, comic)
        verify(titleRect.x >= 0 && titleRect.y >= 0
               && titleRect.x + titleRect.width <= comic.width
               && titleRect.y + titleRect.height <= comic.height,
               "desktop hero title remains within the wide page")
        const controlsRect = rectIn(controls, comic)
        verify(controlsRect.x >= 0 && controlsRect.y >= 0
               && controlsRect.x + controlsRect.width <= comic.width
               && controlsRect.y + controlsRect.height <= comic.height,
               "desktop minimize/fullscreen/close controls remain inside the page")
    }

    function test_locg_fallback_page_phone_hero_bounds() {
        host.width = 354
        host.height = 792
        host.androidHost = true
        comic.visible = false
        locgComic = locgComicSeriesComponent.createObject(host.contentItem)
        verify(locgComic !== null, "the active LOCG fallback App.ComicSeriesPage instantiates")
        wait(40)

        compare(locgComic.androidHost, true)
        compare(locgComic.phoneNarrow, true)
        compare(locgComic.locgId, "", "empty LOCG id keeps this local fixture off the resolver")
        compare(locgComic.loading, false)
        compare(locgComic.dbSeries, null, "the local fixture exercises the live-flow fallback hero")

        const scroll = findDescendant(locgComic, function(item) { return item.parent === locgComic
                                                                   && item.contentWidth !== undefined
                                                                   && item.contentHeight !== undefined })
        const hero = findChild(locgComic, "locgComicHeroContent")
        const title = findChild(locgComic, "locgComicHeroTitle")
        const desktopControls = findChild(locgComic, "locgComicDesktopWindowControls")
        verify(scroll && hero && title && desktopControls,
               "the fallback page exposes its clipped viewport, hero, title, and desktop controls")
        verify(scroll.clip && hero.visible && title.visible,
               "the phone fallback viewport clips and displays its hero (clip=" + scroll.clip
               + ", hero=" + hero.visible + ", title=" + title.visible
               + ", opacity=" + scroll.opacity + ", loading=" + locgComic.loading + ")")
        compare(desktopControls.visible, false,
                "the LOCG fallback page hides desktop controls on the Android host")

        const heroRect = rectIn(hero, scroll.contentItem)
        const titleRect = rectIn(title, hero)
        verify(heroRect.x >= -1 && heroRect.y >= -1
               && heroRect.x + heroRect.width <= scroll.width + 1
               && heroRect.y + heroRect.height <= scroll.contentHeight + 1,
               "the fallback hero stays inside the phone scroll content")
        verify(titleRect.x >= -1 && titleRect.y >= -1
               && titleRect.x + titleRect.width <= hero.width + 1
               && titleRect.y + titleRect.height <= hero.height + 1,
               "the fallback title stays inside its hero")
    }

    function setLedgerViewport(width, height, androidHost) {
        host.width = width
        host.height = height
        host.androidHost = androidHost
        dbLedger.androidHost = androidHost
        wait(40)
        compare(dbLedger.width, width - 40)
        compare(dbLedger.contentWidth, width - 40,
                "the actual ledger content width follows its available width")
        compare(dbLedger.phoneNarrow, androidHost && width - 40 < 600)
    }

    function verifyLedgerHero(width, height, androidHost, expectedColumns, expectedFont) {
        setLedgerViewport(width, height, androidHost)
        const hero = findChild(dbLedger, "comicDbHeroContent")
        const title = findChild(dbLedger, "comicDbHeroTitle")
        const metadata = findChild(dbLedger, "comicDbHeroMetadata")
        verify(hero && title && metadata, "the actual DB ledger exposes its named hero items")
        compare(hero.columns, expectedColumns)
        compare(title.font.pixelSize, expectedFont)
        compare(title.text, dbLedgerSeriesFixture.title)
        verify(metadata.text.indexOf("DC Comics") >= 0
               && metadata.text.indexOf("2 collected editions") >= 0,
               "the hero metadata reflects the local publisher and edition fixtures")

        const heroRect = rectIn(hero, dbLedger)
        const titleRect = rectIn(title, hero)
        const metadataRect = rectIn(metadata, hero)
        verify(heroRect.x >= -1 && heroRect.y >= -1
               && heroRect.x + heroRect.width <= dbLedger.contentWidth + 1,
               "the DB hero stays inside ledger content width")
        verify(titleRect.x >= -1 && titleRect.y >= -1
               && titleRect.x + titleRect.width <= hero.width + 1
               && titleRect.y + titleRect.height <= hero.height + 1,
               "the DB hero title stays within its card")
        verify(metadataRect.x >= -1 && metadataRect.y >= -1
               && metadataRect.x + metadataRect.width <= hero.width + 1
               && metadataRect.y + metadataRect.height <= hero.height + 1,
               "the DB hero metadata stays within its card")
        verify(!overlaps(titleRect, metadataRect),
               "the DB hero title and metadata do not overlap")

        if (androidHost && width === 354) {
            compare(hero.columns, 1, "phone portrait stacks the cover and title column")
            const cover = findDescendant(hero, function(item) {
                return item.width === 128 && item.height === 192 && item.radius === 9
            })
            verify(cover !== null, "the local hero cover remains present")
            const coverRect = rectIn(cover, hero)
            verify(titleRect.y >= coverRect.y + coverRect.height,
                   "phone portrait places title content below the cover")
        }
    }

    function verifyLedgerEditions(width, height, androidHost) {
        setLedgerViewport(width, height, androidHost)
        for (let i = 0; i < dbLedgerSeriesFixture.editions.length; ++i) {
            const fixture = dbLedgerSeriesFixture.editions[i]
            const id = fixture.locg_comic_id
            const edition = findChild(dbLedger, "comicDbEdition_" + id)
            const textColumn = findChild(dbLedger, "comicDbEditionText_" + id)
            const state = findChild(dbLedger, "comicDbEditionState_" + id)
            const alternate = findChild(dbLedger, "comicDbEditionAlternate_" + id)
            verify(edition && textColumn && state && alternate,
                   "the local DB fixture exposes edition, text, state, and alternate regions for " + id)
            compare(edition.canAcquire, false,
                    "unavailable local fixture does not advertise a download action")
            compare(edition.canAlternate, true,
                    "an idle catalogue record retains its separate alternate-source affordance")
            verify(alternate.visible, "alternate-source target is visible for " + id)

            const editionRect = rectIn(edition, dbLedger)
            const textRect = rectIn(textColumn, edition)
            const stateRect = rectIn(state, edition)
            const alternateRect = rectIn(alternate, edition)
            verify(editionRect.x >= -1 && editionRect.x + editionRect.width <= dbLedger.contentWidth + 1,
                   "edition row stays within the ledger content width for " + id)
            verify(textRect.x >= -1 && textRect.y >= -1
                   && textRect.x + textRect.width <= edition.width + 1
                   && textRect.y + textRect.height <= edition.height + 1,
                   "edition text stays inside its card for " + id)
            if (androidHost) {
                verify(state.width >= 48 && state.height >= 48
                       && alternate.width >= 48 && alternate.height >= 48,
                       "phone state and alternate targets meet 48dp for " + id)
            }
            verify(stateRect.x >= -1 && stateRect.y >= -1
                   && stateRect.x + stateRect.width <= edition.width + 1
                   && stateRect.y + stateRect.height <= edition.height + 1,
                   "edition state stays inside its card for " + id)
            verify(alternateRect.x >= -1 && alternateRect.y >= -1
                   && alternateRect.x + alternateRect.width <= edition.width + 1
                   && alternateRect.y + alternateRect.height <= edition.height + 1,
                   "alternate target stays inside its card for " + id)
            verify(!overlaps(stateRect, alternateRect),
                   "edition state and alternate target do not overlap for " + id)

            const displayName = fixture.display_title.slice(dbLedgerSeriesFixture.title.length)
                .replace(/^[\s:,./–—\-]+/, "")
            const displayText = textIn(textColumn, displayName)
            verify(displayText && displayText.visible,
                   "edition display title is visible from the local metadata for " + id)
            const displayRect = rectIn(displayText, edition)
            verify(displayRect.x >= -1 && displayRect.y >= -1
                   && displayRect.x + displayRect.width <= edition.width + 1
                   && displayRect.y + displayRect.height <= edition.height + 1,
                   "edition title text stays inside its card for " + id)

            if (fixture.sources.length > 0) {
                const source = textIn(edition, "Community Archive · " + fixture.sources[0].title)
                verify(source && source.visible, "long community source metadata is shown as catalogue context")
                const sourceRect = rectIn(source, edition)
                verify(sourceRect.x >= -1 && sourceRect.y >= stateRect.y + stateRect.height + 8
                       && sourceRect.x + sourceRect.width <= edition.width + 1
                       && sourceRect.y + sourceRect.height <= edition.height + 1,
                       "long source metadata remains below actions and inside its edition card")
            }
        }
    }

    function test_actual_comic_db_ledger_phone_and_wide_geometry() {
        host.width = 354
        host.height = 792
        host.androidHost = true
        comic.visible = false
        dbLedger = dbLedgerComponent.createObject(host.contentItem)
        verify(dbLedger !== null, "the actual App.ComicDbLedger instantiates from local presentation data")
        wait(50)
        compare(dbLedger.objectName, "comicDbLedger")
        compare(dbLedger.dbSeries.editions.length, 2)
        verify(dbLedger.contentWidth === dbLedger.width,
               "the ledger starts with content width bound to its available width")

        verifyLedgerHero(354, 792, true, 1, 32)
        verifyLedgerEditions(354, 792, true)
        verifyLedgerHero(792, 354, true, 2, 50)
        verifyLedgerEditions(792, 354, true)
        verifyLedgerHero(1280, 800, false, 2, 50)
        verifyLedgerEditions(1280, 800, false)
    }
}
