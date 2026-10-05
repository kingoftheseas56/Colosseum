import QtQuick
import QtTest
import "../../qml" as App

// The Universes world (Arc 58, portal layout 2026-10-05): a Tribute carousel, then every
// universe with a standard page as a 178px portal banner under one glass facet strip.
Item {
    width: 1280; height: 720
    App.UniversesWorld {
        id: world
        anchors.fill: parent
        installed: [
            { extensionId: "com.colosseum.universe.onepiece", name: "One Piece", banner: "", logo: "" },
            { extensionId: "com.colosseum.universe.cosmere", name: "Cosmere", banner: "", logo: "" },
            { extensionId: "com.colosseum.universe.dcau", name: "DC Animated Universe", banner: "", logo: "" },
            { extensionId: "com.colosseum.universe.starwars", name: "Star Wars", banner: "", logo: "" } ]
    }
    SignalSpy { id: opened; target: world; signalName: "universeRequested" }
    SignalSpy { id: tribute; target: world; signalName: "tributeRequested" }

    TestCase {
        name: "UniversesWorld"
        when: windowShown
        function init() { world.facet = ""; opened.clear(); tribute.clear() }

        function test_listIsStandardPagesOnly() {
            compare(world.shown.length, 20)
            verify(!world.shown.some(function(e) { return e.name === "Cosmere" }), "Cosmere is a Tribute only")
            compare(world.tributes.length, 4)
            compare(findChild(world, "universesCount").text, "20 universes")
        }
        function test_bannerIsAPortalWithItsMarkAndAboutCopy() {
            const ledger = findChild(world, "universesLedger")
            tryVerify(function() { return ledger.children.length > 20 })
            const banner = ledger.children[0]
            compare(banner.height, 178)
            compare(banner.entry.name, "Attack on Titan")
            const mark = findChild(banner, "universeBannerMark")
            verify(String(mark.source).indexOf("assets/universes/marks/attack-on-titan.svg") >= 0)
            tryCompare(mark, "status", Image.Ready)
            const blurb = findChild(banner, "universeBannerBlurb")
            compare(blurb.text, banner.entry.blurb)
            verify(blurb.lineCount <= 2, "the About copy is clamped to two lines")
            // the copy starts after the rail and never runs under the Explore affordance
            const copy = findChild(banner, "universeBannerCopy"), explore = findChild(banner, "universeBannerExplore")
            compare(copy.x, 30 + 98 + 26)
            verify(copy.x + copy.width <= banner.width - 28 - explore.width)
        }
        function test_everyMarkFileLoads() {
            const ledger = findChild(world, "universesLedger")
            tryVerify(function() { return ledger.children.length > 20 })
            for (let i = 0; i < 20; ++i) {
                const mark = findChild(ledger.children[i], "universeBannerMark")
                tryVerify(function() { return mark.status === Image.Ready }, 5000,
                          "mark did not load for " + ledger.children[i].entry.name + ": " + mark.source)
            }
        }
        function test_facetStripFiltersTheOneList() {
            const strip = findChild(world, "universesFilter")
            compare(strip.tabModel.map(function(t) { return t.key }).join(","), "all,books,manga,anime,films,series")
            compare(strip.currentTab, "all")
            strip.tabRequested("manga")
            compare(world.facet, "manga")
            compare(world.shown.map(function(e) { return e.name }).join("|"),
                    "Attack on Titan|Dragon Ball|Naruto|One Piece|Weekly Shonen Jump")
            compare(findChild(world, "universesCount").text, "5 universes")
            // a tab is what a universe PRIMARILY is: Avatar holds novels but is a series
            strip.tabRequested("books")
            compare(world.shown.map(function(e) { return e.name }).join("|"),
                    "Dune|Harry Potter|Lord of the Rings|Percy Jackson|Sherlock Holmes|A Song of Ice and Fire|The Witcher")
            strip.tabRequested("series")
            verify(world.shown.some(function(e) { return e.name === "Avatar: The Last Airbender" }))
            verify(world.shown.some(function(e) { return e.name === "A Song of Ice and Fire" }),
                   "a direct adaptation stands under both of its labels")
            strip.tabRequested("all")
            compare(world.shown.length, 20)
        }
        function test_bannerOpensItsStandardPage() {
            const ledger = findChild(world, "universesLedger")
            ledger.forceActiveFocus()
            keyClick(Qt.Key_Down)
            tryCompare(ledger, "currentIndex", 1)
            // Enter straight after the arrow, with no pause: a fast hand must not lose it
            keyClick(Qt.Key_Return)
            tryCompare(opened, "count", 1)
            compare(opened.signalArguments[0][0].name, "Avatar: The Last Airbender")
            compare(opened.signalArguments[0][0].page, "shelves")
        }
        function test_carouselButtonsRouteTributeAndUniverse() {
            const carousel = findChild(world, "universesTributeCarousel")
            compare(carousel.primaryLabel, "Enter tribute")
            compare(carousel.secondaryLabel, "Browse universe")
            carousel.primaryClicked(1)
            compare(tribute.count, 1)
            compare(tribute.signalArguments[0][0].name, "Cosmere")
            // One Piece has a standard page: the roster entry, with its page
            carousel.secondaryClicked(0)
            compare(opened.signalArguments[0][0].name, "One Piece")
            compare(opened.signalArguments[0][0].page, "shelves")
            // Cosmere has none: its slide offers the Tribute alone
            compare(carousel.slides[1].secondaryLabel, "")
            verify(carousel.slides[0].secondaryLabel === undefined)
        }
        function test_pointedBannerWarmsItsFirstPosters() {
            const ledger = findChild(world, "universesLedger")
            ledger.forceActiveFocus()
            keyClick(Qt.Key_Down)                       // Avatar: a shelves page
            tryCompare(world, "warming", "Avatar: The Last Airbender")
            // 3 series + 2 films + 5 books, every one a pinned cover
            compare(findChild(world, "universesWarmer").count, 10)
            ledger.currentIndex = 0
        }
        function test_arrowsWalkFromTheCarouselIntoTheListAndBack() {
            // found in the running app 2026-10-05: with Tributes installed the list starts
            // below the fold and the arrows never reached it
            const ledger = findChild(world, "universesLedger")
            ledger.currentIndex = 0
            findChild(world, "universesTributeCarouselView").forceActiveFocus()
            keyClick(Qt.Key_Down)
            compare(world.automationFocusedObject, "universesFilter")
            keyClick(Qt.Key_Down)
            verify(ledger.activeFocus, "Down from the filter strip enters the list")
            compare(ledger.currentIndex, 0)
            keyClick(Qt.Key_Down)
            compare(ledger.currentIndex, 1)
            keyClick(Qt.Key_Up); keyClick(Qt.Key_Up)
            compare(world.automationFocusedObject, "universesFilter")
            keyClick(Qt.Key_Up)
            compare(world.automationFocusedObject, "universesTributeCarouselView")
            ledger.currentIndex = 0
        }
        function test_tributeOrderIsFixed() {
            const handed = world.installed
            world.installed = handed.slice().reverse()
            compare(world.tributes.map(function(t) { return t.name }).join("|"),
                    "One Piece|Cosmere|DC Animated Universe|Star Wars")
            world.installed = handed
        }
    }
}
