import QtQuick
import QtTest
import "../../qml" as App
import "../../qml/SagaApi.js" as Saga

// The shelves universe page (Arc 58): the layout rules a screenshot review found broken on
// 2026-10-04 — the gap between tiles, which side each row arrow sits on, the wheel, the
// keyboard following a work into a later run, and where a tab lands its section.
Item {
    width: 1280; height: 720
    App.ShelvesUniversePage { id: shelves }
    SignalSpy { id: tankoban; target: shelves; signalName: "seriesRequested" }
    SignalSpy { id: edition; target: shelves; signalName: "seriesEntryRequested" }
    SignalSpy { id: theatre; target: shelves; signalName: "watchRequested" }

    SignalSpy { id: biblio; target: shelves; signalName: "bookRequested" }
    SignalSpy { id: comics; target: shelves; signalName: "comicsArchiveRequested" }

    function works(prefix, medium, n, from) {
        const out = []
        for (let i = 0; i < n; ++i)
            out.push({ title: prefix + " " + (i + 1), medium: medium, canonIndex: i, cover: "",
                       key: prefix + ":" + (from || 0) + ":" + i, id: prefix + i,
                       type: medium === "series" ? "series" : (medium === "movie" ? "movie" : medium),
                       query: prefix + " " + (i + 1), mal: medium === "manga" ? 100 + i : 0,
                       by: "Author " + (i + 1), from: 1990 + i, to: i % 2 ? 2000 + i : 0, resolved: true })
        return out
    }
    // the page's data shape: a list of shelves, each with labelled runs (SagaApi.loadShelves)
    function shelf(title, kind, runs, more) {
        const out = { title: title, kind: kind, key: title.toLowerCase(), tab: "", pill: "", form: "", order: "", runs: runs }
        for (const k in (more || {})) out[k] = more[k]
        return out
    }
    function animeUni(nManga, nAnime, eras) {
        const manga = works("Manga", "manga", nManga)
        return { name: "Dragon Ball", category: "anime", blurb: "", blurbSource: "", banner: "", wallpaper: "", color: "#222",
            shelves: [
                shelf("Manga", "manga", [ { label: "The series", items: manga.slice(0, Math.min(2, nManga > 3 ? 2 : 1)) },
                                          { label: "Spin-offs", items: manga.slice(Math.min(2, nManga > 3 ? 2 : 1)) } ]),
                shelf("Anime", "series", [ { label: "", items: works("Anime", "series", nAnime) } ]),
                shelf("Films", "movie", eras) ] }
    }

    TestCase {
        name: "ShelvesTemplate"
        when: windowShown
        function init() {
            shelves.previewProgress = null
            shelves.uni = animeUni(12, 3, [ { label: "First era", items: works("Early film", "movie", 9) },
                                            { label: "Second era", items: works("Late film", "movie", 9) } ])
            tryVerify(function() { return findChild(shelves, "shelvesScroll").contentHeight > 1200 })
            findChild(shelves, "shelvesScroll").contentY = 0
            findChild(shelves, "shelvesRow_manga").contentX = 0
            findChild(shelves, "shelvesRow_films").contentX = 0
            tankoban.clear(); edition.clear(); theatre.clear(); biblio.clear(); comics.clear()
        }
        function page() { return findChild(shelves, "shelvesScroll") }

        function test_tilesKeepTheirGap() {
            compare(shelves.tileGap, 16)
            const row = findChild(shelves, "shelvesRow_manga")
            // 12 tiles in two runs: 2 + 10, a 28px seam between the runs, a 24px inset after
            compare(row.contentWidth, 12 * 164 + (1 + 9) * 16 + 28 + 24)
        }
        function test_arrowsSitOnTheSideTheyPointTo() {
            const row = findChild(shelves, "shelvesRow_manga")
            const prev = findChild(shelves, "shelvesPrev_manga"), next = findChild(shelves, "shelvesNext_manga")
            verify(!prev.visible, "nothing is before the start of a row")
            verify(next.visible, "the row runs on to the right")
            compare(next.x + next.width, next.parent.width)
            row.contentX = row.contentWidth - row.width
            tryVerify(function() { return prev.visible && !next.visible })
            compare(prev.x, 0)
        }
        function test_shortRowHasNoArrows() {
            verify(!findChild(shelves, "shelvesPrev_anime").visible)
            verify(!findChild(shelves, "shelvesNext_anime").visible)
            verify(!findChild(shelves, "shelvesRow_anime").interactive)
        }
        function test_wheelOverARowScrollsThePage() {
            const flick = page(), row = findChild(shelves, "shelvesRow_manga")
            flick.contentY = 300
            const before = flick.contentY
            const at = row.mapToItem(shelves, 300, 100)
            mouseWheel(shelves, at.x, at.y, 0, -240)
            tryVerify(function() { return flick.contentY > before }, 2000, "a vertical wheel over a row must scroll the page")
            compare(row.contentX, 0)
        }
        function test_keyboardFollowsIntoALaterRun() {
            const row = findChild(shelves, "shelvesRow_films"), focus = findChild(shelves, "shelvesRowFocus_films")
            focus.forceActiveFocus()
            for (let i = 0; i < 13; ++i) keyClick(Qt.Key_Right)
            compare(focus.currentIndex, 13)
            // work 13 is the 5th film of the second era: it must be inside the row's window
            const left = 9 * 164 + 8 * 16 + 28 + 4 * (164 + 16)
            verify(left >= row.contentX - 1 && left + 164 <= row.contentX + row.width + 1,
                   "the selected film is off screen: x " + left + ", row shows " + row.contentX + " to " + (row.contentX + row.width))
            keyClick(Qt.Key_Return)
            compare(theatre.count, 1)
            compare(theatre.signalArguments[0][0].title, "Late film 5")
        }
        function test_tabLandsItsPanelUnderTheChromeWithTabsDocked() {
            const flick = page(), tabs = findChild(shelves, "shelvesTabs")
            const section = findChild(shelves, "shelvesMedium_manga")
            shelves.revealSection(section)
            // the panel (44px below the section's top) sits just under the chrome band
            compare(section.mapToItem(shelves, 0, 44).y, 100)
            verify(tabs.docked, "the tabs must be docked once a tab has been used")
            compare(tabs.y, 22)
            compare(tabs.x, shelves.gutter + 118)
        }
        function test_tabsAreBesideBackBeforeTheyReachIt() {
            const flick = page(), tabs = findChild(shelves, "shelvesTabs")
            flick.contentY = shelves.heroHeight - 70      // the tabs' top is 70px down: Back ends at 62
            compare(tabs.x, shelves.gutter + 118)
            flick.contentY = 0
            compare(tabs.x, shelves.gutter)
        }
        function test_pinnedComicOpensExistingGetComicsRoute() {
            const u = animeUni(1, 1, [])
            u.shelves.push(shelf("Comics", "comic", [ { label: "Age of Rebellion", items: [
                { title: "Star Wars (2015)", medium: "comic", id: "gcd:85861",
                  seriesId: "gcd:85861", gcdId: 85861, posts: [26442],
                  provider: "getcomics", cover: "../assets/universes/marks/star-wars.png",
                  key: "comics:0", resolved: true }
            ] } ]))
            shelves.uni = u
            const focus = findChild(shelves, "shelvesRowFocus_comics")
            verify(focus !== null, "Comics must use the existing standard shelf component")
            focus.forceActiveFocus()
            keyClick(Qt.Key_Return)
            compare(comics.count, 1)
            const entry = comics.signalArguments[0][0]
            compare(entry.gcdId, 85861)
            compare(entry.posts[0], 26442)
            compare(entry.seriesId, "gcd:85861")
            compare(tankoban.count, 0)
            compare(theatre.count, 0)
            compare(biblio.count, 0)
        }
        function test_mangaOpensTankoban() {
            const focus = findChild(shelves, "shelvesRowFocus_manga")
            focus.forceActiveFocus()
            keyClick(Qt.Key_Return)
            compare(tankoban.count, 1)
            compare(tankoban.signalArguments[0][0], "Manga 1")
            compare(tankoban.signalArguments[0][1], 100, "a manga opens Tankoban by its pinned MAL id")
            compare(edition.count, 0, "an ordinary manga must not take the edition route")
        }

        // ── edition manga routing (2026-10-05 catalogue correction): a provider/seriesId
        //    manga is a whole entry of its own — it must never collapse onto the ordinary
        //    "title + mal" route and merge with the base series ──
        function test_anEditionMangaOpensItsOwnEntry() {
            // a colour-edition-shaped universe: the second tile of "The series" carries the
            // Tankoban profile (provider, malId, seriesId — and deliberately NO generic mal)
            const u = animeUni(3, 1, [])
            u.shelves[0].runs[0].items.push({ title: "One Piece (Color)", medium: "manga", canonIndex: 1,
                                              cover: "", key: "manga:0:1", query: "One Piece (Color)",
                                              mal: 0, provider: "tankoban", id: "one-piece-color",
                                              malId: "13", seriesId: "mal:13:color", resolved: true })
            shelves.uni = u
            const focus = findChild(shelves, "shelvesRowFocus_manga")
            focus.forceActiveFocus()
            keyClick(Qt.Key_Right)
            keyClick(Qt.Key_Return)
            compare(edition.count, 1)
            const entry = edition.signalArguments[0][0]
            compare(entry.seriesId, "mal:13:color")
            compare(entry.provider, "tankoban")
            compare(entry.malId, "13")
            compare(tankoban.count, 0, "an edition manga must not emit the ordinary seriesRequested")
        }

        // ── curated screen art (Fish-Man Island Log's poster override): the mapping rule
        //    is generic — a curated poster/cover wins over the generated metahub art ──
        function test_aCuratedPosterOverridesTheGeneratedCover() {
            const curated = Saga.shelfItem({ t: "Log", id: "tt33992229", year: "2024",
                                             poster: "https://img.example/fishman-poster.jpg" },
                                           "series", "s", 0)
            compare(curated.cover, "https://img.example/fishman-poster.jpg")
            const generated = Saga.shelfItem({ t: "Log", id: "tt33992229", year: "2024" }, "series", "s", 0)
            compare(generated.cover, "https://live.metahub.space/poster/medium/tt33992229/img",
                    "without curation the metahub poster is generated from the id")
            const backdrop = Saga.shelfItem({ t: "Log", id: "tt33992229", year: "2024",
                                              art: "https://img.example/fishman-art.jpg" },
                                            "series", "s", 0)
            compare(backdrop.art, "https://img.example/fishman-art.jpg")
            compare(backdrop.cover, "https://live.metahub.space/poster/medium/tt33992229/img",
                    "an art override alone does not touch the poster")
        }

        // ── panel lines (2026-10-05): a short row's panel hugs its row; consecutive
        //    short panels share a line; a long row keeps the full width ──
        function shortUniverse() {
            // Attack-on-Titan-shaped: 3 manga (1 series + 2 spin-offs), 1 anime, many films
            shelves.uni = animeUni(3, 1, [ { label: "Only era", items: works("Film", "movie", 18) } ])
            tryVerify(function() { const m = findChild(shelves, "shelvesMedium_manga"); return m && m.width > 0 })
        }
        function test_shortPanelHugsItsRow() {
            shortUniverse()
            const manga = findChild(shelves, "shelvesMedium_manga"), anime = findChild(shelves, "shelvesMedium_anime")
            const contentWidth = shelves.width - shelves.gutter * 2
            // 1 series tile + a 28px seam + 2 spin-off tiles, inset both ends
            compare(manga.width, shelves.rowInset * 2 + (164 + 28 + 2 * 164 + 16))
            verify(manga.width < contentWidth, "a 3-tile panel must not take the full width")
            compare(anime.width, shelves.rowInset * 2 + 164)
        }
        function test_shortPanelsShareALine() {
            shortUniverse()
            const manga = findChild(shelves, "shelvesMedium_manga"), anime = findChild(shelves, "shelvesMedium_anime")
            compare(anime.y, manga.y, "two panels that fit share the line's y")
            compare(anime.x, manga.x + manga.width + shelves.panelSeam, "24px between panels on a line")
            compare(anime.height, manga.height, "panels on one line share the tallest height")
            const films = findChild(shelves, "shelvesMedium_films")
            verify(films.y > anime.y, "the full-width films panel takes its own line below")
            compare(films.width, shelves.width - shelves.gutter * 2, "a long row's panel is the full content width")
        }
        function test_firstOfALineIsTheActiveTab() {
            shortUniverse()
            // the shared line: revealing EITHER of its sections lands the line under the
            // chrome, and the FIRST of the two (Manga) is the active tab
            shelves.revealSection(findChild(shelves, "shelvesMedium_anime"))
            compare(shelves.activeTab, 0)
            shelves.revealSection(findChild(shelves, "shelvesMedium_manga"))
            compare(shelves.activeTab, 0)
        }

        // ── any universe (2026-10-05, Arc 58-E): the shelves are DATA ──
        function generalUniverse() {
            shelves.uni = { name: "James Bond", category: "eras", blurb: "", blurbSource: "", banner: "", wallpaper: "", color: "#222",
                shelves: [
                    shelf("Films", "movie", [ { label: "First actor", items: works("A film", "movie", 6, 0) },
                                              { label: "Second actor", items: works("B film", "movie", 6, 1) },
                                              { label: "Third actor", items: works("C film", "movie", 6, 2) } ]),
                    shelf("Series", "series", [ { label: "One", items: works("S", "series", 1, 0) },
                                                { label: "Two", items: works("T", "series", 1, 1) },
                                                { label: "Three", items: works("U", "series", 1, 2) } ]),
                    shelf("Books", "book", [ { label: "", items: works("Book", "book", 3) } ]) ] }
            tryVerify(function() { const m = findChild(shelves, "shelvesMedium_books"); return m && m.width > 0 })
        }
        function test_aDegradedCatalogueMarksItsLimitedCoverage() {
            const u = animeUni(1, 1, [])
            u.catalogueNotice = "Partial catalogue — the archive could not be loaded"
            shelves.uni = u
            const note = findChild(shelves, "shelvesCatalogueNotice")
            verify(note.visible)
            compare(note.text, u.catalogueNotice)
            shelves.uni = animeUni(1, 1, [])
            verify(!note.visible, "full catalogue pages must not display a fallback warning")
        }
        function test_shelvesTabsAndKickerComeFromData() {
            generalUniverse()
            compare(shelves.tabs.map(function(t) { return t.title }).join("|"), "Films|Series|Books|About")
            compare(shelves.facts.join("|"), "18 Films|3 Series|3 Books")
            compare(shelves.kicker, "UNIVERSE")
            verify(findChild(shelves, "shelvesReadButton").visible && findChild(shelves, "shelvesWatchButton").visible)
            shelves.uni = animeUni(3, 1, [])
            compare(shelves.kicker, "UNIVERSE", "every standard page is a Universe")
            compare(shelves.tabs.map(function(t) { return t.title }).join("|"), "Manga|Anime|About", "an empty shelf draws no tab")
        }
        function test_unlabelledPanelKeepsTheLabelBandOfItsLine() {
            // Manga has two labelled runs, Anime has none, and they share a line: both
            // rows of tiles must start at the same height (in-app walk, 2026-10-05)
            shelves.uni = animeUni(3, 1, [])
            const manga = findChild(shelves, "shelvesRow_manga"), anime = findChild(shelves, "shelvesRow_anime")
            tryCompare(findChild(shelves, "shelvesMedium_anime"), "bandOffset", shelves.runBand)
            compare(findChild(shelves, "shelvesMedium_manga").bandOffset, 0)
            compare(anime.mapToItem(shelves, 0, 0).y, manga.mapToItem(shelves, 0, 0).y + shelves.runBand)
            verify(!findChild(shelves, "shelvesJumps_manga"), "the run jumps are gone")
        }
        function test_aBookOpensBiblio() {
            generalUniverse()
            findChild(shelves, "shelvesRowFocus_books").forceActiveFocus()
            keyClick(Qt.Key_Return)
            tryCompare(biblio, "count", 1)
            compare(biblio.signalArguments[0][0].title, "Book 1")
            compare(tankoban.count, 0); compare(theatre.count, 0)
        }

        // ── a magazine's lineup is a numbered TABLE (Weekly Shonen Jump) ──
        function magazine() {
            shelves.uni = { name: "Weekly Shonen Jump", category: "magazine", blurb: "", blurbSource: "", banner: "", wallpaper: "", color: "#222",
                shelves: [
                    shelf("In the magazine now", "manga", [ { label: "", items: works("Now", "manga", 20) } ],
                          { key: "now", tab: "Now", pill: "running now", form: "table", order: "Longest-running first" }),
                    shelf("The classics", "manga", [ { label: "", items: works("Classic", "manga", 20) } ],
                          { key: "classics", tab: "Classics", pill: "classics", form: "table", order: "In order of first issue" }) ] }
            tryVerify(function() { const t = findChild(shelves, "shelvesTablePanel_now"); return t && t.width > 0 })
        }
        function test_tableIsFullWidthAndNumberedDownThenAcross() {
            magazine()
            compare(shelves.kicker, "UNIVERSE")
            compare(shelves.tabs.map(function(t) { return t.title }).join("|"), "Now|Classics|About")
            compare(shelves.facts.join("|"), "20 running now|20 classics")
            verify(findChild(shelves, "shelvesReadButton").visible && !findChild(shelves, "shelvesWatchButton").visible)
            const now = findChild(shelves, "shelvesTablePanel_now"), classics = findChild(shelves, "shelvesTablePanel_classics")
            compare(now.width, shelves.width - shelves.gutter * 2)
            verify(classics.y > now.y, "each table takes its own line")
            const r1 = findChild(shelves, "shelvesTableRow_now_0"), r2 = findChild(shelves, "shelvesTableRow_now_1")
            const r10 = findChild(shelves, "shelvesTableRow_now_9"), r11 = findChild(shelves, "shelvesTableRow_now_10")
            compare(r2.x, r1.x); compare(r2.y, r1.y + 78)
            compare(r10.y, r1.y + 9 * 78)
            compare(r11.y, r1.y, "row 11 starts the second column")
            verify(r11.x > r1.x + r1.width)
        }
        function test_tableKeyboardWalksRowsAndColumns() {
            magazine()
            const table = findChild(shelves, "shelvesTablePanel_now")
            findChild(shelves, "shelvesTableFocus_now").forceActiveFocus()
            keyClick(Qt.Key_Down); keyClick(Qt.Key_Down)
            compare(table.currentIndex, 2)
            keyClick(Qt.Key_Right)
            compare(table.currentIndex, 12)
            keyClick(Qt.Key_Left); keyClick(Qt.Key_Up)
            compare(table.currentIndex, 1)
            keyClick(Qt.Key_Return)
            compare(tankoban.count, 1)
            compare(tankoban.signalArguments[0][0], "Now 2")
            compare(tankoban.signalArguments[0][1], 101)
        }
    }
}
