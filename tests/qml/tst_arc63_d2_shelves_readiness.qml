import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.3
import "../../qml" as App

TestCase {
    id: tc
    name: "Arc63D2ShelvesReadiness"
    width: 1280
    height: 720
    when: windowShown

    readonly property url readyA: Qt.resolvedUrl("../../assets/icons/lucide/chevron-right.svg")
    readonly property url readyB: Qt.resolvedUrl("../../assets/icons/lucide/chevron-left.svg")
    readonly property url badImage: Qt.resolvedUrl("../../qml/Universes.js")

    function screen(key, id, title, cover, art, shelfTitle, medium) {
        return {
            key: key, id: id, title: title, cover: String(cover), art: String(art || ""),
            medium: medium || "movie", type: medium === "series" ? "series" : "movie",
            resolved: true, shelfTitle: shelfTitle || ""
        }
    }

    function manga(key, title, cover, shelfTitle, mal) {
        return {
            key: key, title: title, cover: String(cover), medium: "manga",
            resolved: true, shelfTitle: shelfTitle || "", mal: mal || 1
        }
    }

    function shelf(title, key, kind, items, form) {
        return {
            title: title, key: key, kind: kind, form: form || "",
            runs: [{ label: "", items: items }]
        }
    }

    function simpleUni(cover, wallpaper) {
        return {
            name: "Attack on Titan", category: "anime", blurb: "", blurbSource: "",
            banner: String(wallpaper), wallpaper: String(wallpaper), color: "#221c30",
            shelves: [
                shelf("Films", "films", "movie",
                      [screen("films:0", "tt-d2-film", "Film", cover, "", "Films", "movie")])
            ]
        }
    }

    function fullUni(cover, wallpaper, art) {
        return {
            name: "Attack on Titan", category: "anime", blurb: "", blurbSource: "",
            banner: String(wallpaper), wallpaper: String(wallpaper), color: "#221c30",
            shelves: [
                shelf("Manga", "manga", "manga", [
                    manga("manga:0", "Manga 1", cover, "Manga", 101),
                    manga("manga:1", "Manga 2", cover, "Manga", 102),
                    manga("manga:2", "Manga 3", cover, "Manga", 103)
                ]),
                shelf("Anime", "anime", "series", [
                    screen("anime:0", "tt-d2-anime", "Anime", cover, art, "Anime", "series")
                ]),
                shelf("Films", "films", "movie", [
                    screen("films:0", "tt-d2-film-0", "Film 0", cover, art, "Films", "movie"),
                    screen("films:1", "tt-d2-film-1", "Film 1", cover, art, "Films", "movie"),
                    screen("films:2", "tt-d2-film-2", "Film 2", cover, art, "Films", "movie"),
                    screen("films:3", "tt-d2-film-3", "Film 3", cover, art, "Films", "movie"),
                    screen("films:4", "tt-d2-film-4", "Film 4", cover, art, "Films", "movie"),
                    screen("films:5", "tt-d2-film-5", "Film 5", cover, art, "Films", "movie"),
                    screen("films:6", "tt-d2-film-6", "Film 6", cover, art, "Films", "movie")
                ])
            ]
        }
    }

    function tableUni(cover, wallpaper) {
        return {
            name: "Attack on Titan", category: "anime", blurb: "", blurbSource: "",
            banner: String(wallpaper), wallpaper: String(wallpaper), color: "#221c30",
            shelves: [
                shelf("Magazine", "magazine", "manga", [
                    manga("magazine:0", "Entry 1", cover, "Magazine", 201),
                    manga("magazine:1", "Entry 2", cover, "Magazine", 202)
                ], "table")
            ]
        }
    }

    function createHost(windowHeight) {
        const host = hostComponent.createObject(null, { height: windowHeight || tc.height })
        verify(host !== null)
        host.visible = true
        tryVerify(function() {
            return host.page.uni && host.page.uni.name === "Attack on Titan"
                    && host.page.directorLoadedGenerationInternal === host.page.loadGeneration
        }, 2000)
        return host
    }

    function installUni(host, uni, preview) {
        host.page.previewProgress = preview || []
        host.page.uni = uni
        tryVerify(function() {
            return host.page.directorCohortSnapshotInternal.enumerable
        }, 3000)
    }

    function destroyHost(host) {
        host.visible = false
        host.destroy()
    }

    function hasKind(page, kind) {
        const members = page.directorRequiredMembersInternal || []
        for (let i = 0; i < members.length; ++i)
            if (members[i].kind === kind)
                return true
        return false
    }

    Component {
        id: hostComponent
        Window {
            id: hostWindow
            width: tc.width
            height: tc.height
            visible: false
            property alias page: shelvesPage
            App.ShelvesUniversePage {
                id: shelvesPage
                anchors.fill: parent
                universeName: "Attack on Titan"
                reducedMotion: true
            }
        }
    }

    function test_real_attack_on_titan_ends_at_films() {
        const host = createHost(tc.height)
        const page = host.page
        compare(page.activeShelves.length, 3)
        compare(page.activeShelves[0].title, "Manga")
        compare(page.activeShelves[1].title, "Anime")
        compare(page.activeShelves[2].title, "Films")
        tryCompare(page, "directorShotEndObjectName", "shelvesMedium_films", 2000)
        verify(page.directorReadinessState !== "settled")
        destroyHost(host)
    }

    function test_zero_generation_gap_never_settles() {
        const host = hostComponent.createObject(null, { visible: true })
        verify(host !== null)
        const page = host.page
        page.universeName = ""
        tryVerify(function() {
            return page.directorLoadedGenerationInternal !== page.loadGeneration
        }, 1000)
        compare(page.directorRequiredCount, 0)
        compare(page.directorReadinessState, "unavailable")
        destroyHost(host)
    }

    function test_error_is_partial() {
        const host = createHost(420)
        const page = host.page
        installUni(host, simpleUni(tc.badImage, tc.readyA), [])
        tryVerify(function() { return page.directorFailedCount >= 1 }, 5000)
        compare(page.directorReadinessState, "partial")
        verify(page.directorReadinessState !== "settled")
        verify(page.directorReadinessDiagnostics.length > 0)
        destroyHost(host)
    }

    function test_exact_small_cohort_and_frame_barrier() {
        const host = createHost(360)
        const page = host.page
        host.visible = false
        installUni(host, simpleUni(tc.readyA, tc.readyA), [])

        tryVerify(function() {
            return page.directorRequiredCount === 2
                    && page.directorReadyCount === 2
                    && page.directorFailedCount === 0
        }, 5000)
        compare(page.directorReadinessState, "unavailable")
        compare(page.directorPresentedFrameCount, 0)

        host.visible = true
        compare(page.directorReadinessState, "loading")
        verify(page.directorReadinessState !== "settled")
        tryCompare(page, "directorReadinessState", "settled", 5000)
        compare(page.directorRequiredCount, 2)
        compare(page.directorReadyCount, 2)
        compare(page.directorFailedCount, 0)
        verify(page.directorPresentedFrameCount >= 2)
        destroyHost(host)
    }

    function test_full_glide_cohort_tracks_continue_shelves_and_gallery() {
        const host = createHost(tc.height)
        const page = host.page
        installUni(host, fullUni(tc.readyA, tc.readyA, tc.readyA), [{
            id: "tt-d2-anime", kind: "video", title: "Anime",
            sub: "Episode 1", cover: String(tc.readyA), progress: 0.25, watched: false
        }])

        tryVerify(function() {
            return page.directorFailedCount === 0
                    && page.directorReadyCount === page.directorRequiredCount
                    && page.directorRequiredCount > 0
        }, 5000)
        verify(hasKind(page, "wallpaper"))
        verify(hasKind(page, "continue"))
        verify(hasKind(page, "shelf"))
        verify(hasKind(page, "gallery"))
        tryCompare(page, "directorReadinessState", "settled", 5000)
        destroyHost(host)
    }

    function test_table_imagery_participates_when_final_shelf_is_table() {
        const host = createHost(tc.height)
        const page = host.page
        installUni(host, tableUni(tc.readyA, tc.readyA), [])
        tryCompare(page, "directorShotEndObjectName", "shelvesTablePanel_magazine", 2000)
        tryVerify(function() {
            return hasKind(page, "table")
                    && page.directorFailedCount === 0
                    && page.directorReadyCount === page.directorRequiredCount
        }, 5000)
        tryCompare(page, "directorReadinessState", "settled", 5000)
        destroyHost(host)
    }

    function test_source_change_rearms_epoch_and_barrier() {
        const host = createHost(360)
        const page = host.page
        installUni(host, simpleUni(tc.readyA, tc.readyA), [])
        tryCompare(page, "directorReadinessState", "settled", 5000)
        const epoch = page.directorReadinessEpoch

        page.uni = simpleUni(tc.readyB, tc.readyA)
        verify(page.directorReadinessEpoch > epoch)
        compare(page.directorPresentedFrameCount, 0)
        verify(page.directorReadinessState !== "settled")
        tryVerify(function() {
            return page.directorFailedCount === 0
                    && page.directorReadyCount === page.directorRequiredCount
        }, 5000)
        tryCompare(page, "directorReadinessState", "settled", 5000)
        destroyHost(host)
    }

    function test_generation_change_cancels_old_settlement() {
        const host = createHost(360)
        const page = host.page
        installUni(host, simpleUni(tc.readyA, tc.readyA), [])
        tryCompare(page, "directorReadinessState", "settled", 5000)
        const generation = page.loadGeneration
        const epoch = page.directorReadinessEpoch

        page.universeName = "Dragon Ball"
        verify(page.loadGeneration > generation)
        verify(page.directorReadinessEpoch > epoch)
        verify(page.directorReadinessState !== "settled")
        destroyHost(host)
    }
}
