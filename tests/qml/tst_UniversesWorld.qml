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
            compare(strip.tabModel.map(function(t) { return t.key }).join(","), "all,books,manga,anime,films,series,comics")
            compare(strip.currentTab, "all")
            strip.tabRequested("manga")
            compare(world.facet, "manga")
            compare(world.shown.map(function(e) { return e.name }).join("|"),
                    "Attack on Titan|Dragon Ball|Naruto|One Piece|Weekly Shonen Jump")
            compare(findChild(world, "universesCount").text, "5 universes")
            strip.tabRequested("all")
            compare(world.shown.length, 20)
        }
        function test_bannerOpensItsStandardPage() {
            const ledger = findChild(world, "universesLedger")
            ledger.forceActiveFocus()
            keyClick(Qt.Key_Down)
            tryCompare(ledger, "currentIndex", 1)
            wait(150)        // let the keyboard glide settle, as a person's next keypress would
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
            // Cosmere has none: the Tribute entry itself, which the shell opens as the Tribute
            carousel.secondaryClicked(1)
            compare(opened.signalArguments[1][0].name, "Cosmere")
            verify(!opened.signalArguments[1][0].page)
            compare(opened.signalArguments[1][0].extensionId, "com.colosseum.universe.cosmere")
        }
    }
}
