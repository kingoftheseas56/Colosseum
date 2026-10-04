import QtQuick
import QtTest
import "../../qml" as App

// The shelves universe page (Arc 58): the layout rules a screenshot review found broken on
// 2026-10-04 — the gap between tiles, which side each row arrow sits on, the wheel, the
// keyboard following a work into a later run, and where a tab lands its section.
Item {
    width: 1280; height: 720
    App.ShelvesUniversePage { id: shelves }
    SignalSpy { id: tankoban; target: shelves; signalName: "seriesRequested" }
    SignalSpy { id: theatre; target: shelves; signalName: "watchRequested" }

    function works(prefix, medium, n) {
        const out = []
        for (let i = 0; i < n; ++i)
            out.push({ title: prefix + " " + (i + 1), medium: medium, canonIndex: i, cover: "",
                       id: prefix + i, type: medium === "series" ? "series" : "movie",
                       query: prefix + " " + (i + 1), main: i < 2, resolved: true })
        return out
    }

    TestCase {
        name: "ShelvesTemplate"
        when: windowShown
        function init() {
            shelves.previewProgress = null
            shelves.uni = { name: "Dragon Ball", blurb: "", blurbSource: "", banner: "", wallpaper: "", color: "#222",
                manga: works("Manga", "manga", 12), shows: works("Anime", "series", 3),
                filmEras: [ { era: "First era", films: works("Early film", "film", 9) },
                            { era: "Second era", films: works("Late film", "film", 9) } ] }
            tryVerify(function() { return findChild(shelves, "shelvesScroll").contentHeight > 1200 })
            findChild(shelves, "shelvesScroll").contentY = 0
            findChild(shelves, "shelvesRow_manga").contentX = 0
            findChild(shelves, "shelvesRow_films").contentX = 0
            tankoban.clear(); theatre.clear()
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
            const section = findChild(shelves, "shelvesManga")
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
        function test_mangaOpensTankoban() {
            const focus = findChild(shelves, "shelvesRowFocus_manga")
            focus.forceActiveFocus()
            keyClick(Qt.Key_Return)
            compare(tankoban.count, 1)
            compare(tankoban.signalArguments[0][0], "Manga 1")
        }

        // ── panel lines (2026-10-05): a short row's panel hugs its row; consecutive
        //    short panels share a line; a long row keeps the full width ──
        function shortUniverse() {
            // Attack-on-Titan-shaped: 3 manga (1 series + 2 spin-offs), 1 anime, many films
            shelves.uni = { name: "Short", blurb: "", blurbSource: "", banner: "", wallpaper: "", color: "#222",
                manga: works("Manga", "manga", 3), shows: works("Anime", "series", 1),
                filmEras: [ { era: "Only era", films: works("Film", "film", 18) } ] }
            tryVerify(function() { return findChild(shelves, "shelvesManga").width > 0 })
        }
        function test_shortPanelHugsItsRow() {
            shortUniverse()
            const manga = findChild(shelves, "shelvesManga"), anime = findChild(shelves, "shelvesAnime")
            const contentWidth = shelves.width - shelves.gutter * 2
            // 1 series tile + a 28px seam + 2 spin-off tiles, inset both ends
            compare(manga.width, shelves.rowInset * 2 + (164 + 28 + 2 * 164 + 16))
            verify(manga.width < contentWidth, "a 3-tile panel must not take the full width")
            compare(anime.width, shelves.rowInset * 2 + 164)
        }
        function test_shortPanelsShareALine() {
            shortUniverse()
            const manga = findChild(shelves, "shelvesManga"), anime = findChild(shelves, "shelvesAnime")
            compare(anime.y, manga.y, "two panels that fit share the line's y")
            compare(anime.x, manga.x + manga.width + shelves.panelSeam, "24px between panels on a line")
            compare(anime.height, manga.height, "panels on one line share the tallest height")
            const films = findChild(shelves, "shelvesFilms")
            verify(films.y > anime.y, "the full-width films panel takes its own line below")
            compare(films.width, shelves.width - shelves.gutter * 2, "a long row's panel is the full content width")
        }
        function test_firstOfALineIsTheActiveTab() {
            shortUniverse()
            // the shared line: revealing EITHER of its sections lands the line under the
            // chrome, and the FIRST of the two (Manga) is the active tab
            shelves.revealSection(findChild(shelves, "shelvesAnime"))
            compare(shelves.activeTab, 0)
            shelves.revealSection(findChild(shelves, "shelvesManga"))
            compare(shelves.activeTab, 0)
        }
    }
}
