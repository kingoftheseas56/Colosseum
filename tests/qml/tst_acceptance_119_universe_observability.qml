import QtQuick
import QtTest
import "../../qml" as App

// Actual component checks, deliberately separate from assembled-app Lanista results.
// The negative-art tests inject component data, never claim those injections are UI journeys.
Item {
    id: host
    width: 1280
    height: 800
    Component { id: bannerComponent; App.UniverseBanner { width: 1000 } }
    Component { id: shelvesComponent; App.ShelvesUniversePage {} }
    property var subject: null
    SignalSpy { id: activated; signalName: "activated" }
    SignalSpy { id: watched; signalName: "watchRequested" }

    TestCase {
        name: "Acceptance119UniverseObservability"
        when: windowShown
        function cleanup() {
            activated.target = null
            watched.target = null
            if (host.subject) host.subject.destroy()
            host.subject = null
        }
        function test_banner_missing_art_preserves_visible_identity_and_hit_target() {
            host.subject = bannerComponent.createObject(host, {
                objectName: "acceptanceBanner",
                entry: {
                    name: "One Piece",
                    banner: Qt.resolvedUrl("intentionally-missing-acceptance-banner.png"),
                    logo: Qt.resolvedUrl("../../assets/universes/marks/one-piece.png"),
                    blurb: "Synthetic missing-art case",
                    chips: [{t: "Manga"}]
                }
            })
            verify(host.subject !== null)
            tryCompare(host.subject, "artworkStatus", Image.Error, 5000)
            tryCompare(host.subject, "markStatus", Image.Ready, 5000)
            var title = findChild(host.subject, "acceptanceBanner_title")
            verify(title !== null && title.visible)
            compare(title.text, "One Piece")
            compare(host.subject.displayedName, "One Piece")
            activated.target = host.subject
            mouseClick(host.subject, host.subject.width / 2, host.subject.height / 2)
            compare(activated.count, 1)
        }
        function test_pinned_franchise_observations_data() {
            return [
                {tag: "Bond", name: "James Bond", works: 39, first: "films", count: 25},
                {tag: "Marvel", name: "Marvel Cinematic Universe", works: 63, first: "films", count: 40},
                {tag: "Trek", name: "Star Trek", works: 27, first: "series", count: 13},
                {tag: "Wars", name: "Star Wars", works: 28, first: "films", count: 11},
                {tag: "DCAU", name: "DC Animated Universe", works: 12, first: "series", count: 8},
                {tag: "Avatar", name: "Avatar: The Last Airbender", works: 10, first: "series", count: 3},
                {tag: "Ghibli", name: "Studio Ghibli", works: 23, first: "films", count: 23},
                {tag: "Jump", name: "Weekly Shonen Jump", works: 40, first: "in-the-magazine-now", count: 20},
                {tag: "OnePiece", name: "One Piece", works: 56, first: "manga", count: 15},
                {tag: "DragonBall", name: "Dragon Ball", works: 40, first: "manga", count: 8},
                {tag: "Naruto", name: "Naruto", works: 18, first: "manga", count: 4},
                {tag: "Titan", name: "Attack on Titan", works: 11, first: "manga", count: 3}
            ]
        }
        function test_pinned_franchise_observations(row) {
            host.subject = shelvesComponent.createObject(host, {universeName: row.name})
            verify(host.subject !== null)
            compare(host.subject.objectName, "shelvesUniversePage")
            tryCompare(host.subject, "workCount", row.works, 5000)
            compare(findChild(host.subject, "shelvesUniverseTitle").text, row.name)
            compare(findChild(host.subject, "shelvesCount_" + row.first).text, String(row.count))
            verify(findChild(host.subject, "shelvesTab_0") !== null)
            verify(findChild(host.subject, "shelvesBackAction") !== null)
        }
        function test_shelf_poster_failure_has_exact_visible_fallback_and_usable_route() {
            host.subject = shelvesComponent.createObject(host)
            verify(host.subject !== null)
            host.subject.uni = {
                name: "Acceptance fixture", category: "fixture", blurb: "", blurbSource: "",
                banner: "", wallpaper: "", color: "#222222", shelves: [{title: "Films", key: "films", kind: "movie",
                    runs: [{label: "", items: [{title: "Acceptance missing poster", key: "fixture-film:42",
                        id: "acceptance-film-42", medium: "movie", type: "movie", resolved: true,
                        cover: Qt.resolvedUrl("intentionally-missing-acceptance-poster.png")}]}]}]
            }
            tryCompare(host.subject, "workCount", 1)
            var tile = findChild(host.subject, "shelvesWork_films_0")
            verify(tile !== null)
            tryCompare(tile, "artworkStatus", Image.Error, 5000)
            compare(tile.workIdentity, "fixture-film:42")
            compare(tile.workTitle, "Acceptance missing poster")
            compare(tile.fallbackVisible, true)
            var fallback = findChild(host.subject, "shelvesWork_films_0_fallback")
            verify(fallback !== null && fallback.visible)
            compare(fallback.text, "Acceptance missing poster")
            host.subject.revealSection(findChild(host.subject, "shelvesMedium_films"))
            watched.target = host.subject
            mouseClick(tile, tile.width / 2, tile.height / 2)
            compare(watched.count, 1)
            compare(watched.signalArguments[0][0].id, "acceptance-film-42")
        }
    }
}
