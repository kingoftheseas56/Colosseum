import QtQuick
import QtQuick.Window
import QtTest
import "../../qml" as Colosseum
import "../../qml/OnePieceCatalogApi.js" as CatalogApi
import "../../qml/OnePieceParadiseData.js" as Paradise
import "../../qml/OnePieceParadiseAdapter.js" as ParadiseAdapter
import "../../qml/OnePieceNewWorldData.js" as NewWorld
import "../../qml/OnePieceNewWorldAdapter.js" as NewWorldAdapter
import "../../qml/OnePieceFinalSeaData.js" as FinalSea
import "../../qml/OnePieceFinalSeaAdapter.js" as FinalSeaAdapter

TestCase {
    id: tests
    name: "OnePieceFourSeaCatalogue"
    when: windowShown
    Window { id: testWindow; width: 1280; height: 720; visible: true }
    Component { id: arcComponent; Colosseum.OnePieceArcPage { reducedMotion: true } }
    property var arcPage: null
    property int requests: 0
    SignalSpy { id: seriesSpy; target: tests.arcPage; signalName: "seriesRequested" }
    SignalSpy { id: watchSpy; target: tests.arcPage; signalName: "watchRequested" }

    function init() {
        requests = 0
        CatalogApi.resetCache()
        CatalogApi.setRequestAdapter(function(url, done) {
            tests.requests++
            if (url.indexOf("kitsu.io") >= 0) {
                var rows = []
                for (var n = 1; n <= 1180; ++n)
                    rows.push({ attributes: { number: n, canonicalTitle: "Episode " + n } })
                done({ data: rows })
            } else if (url.indexOf("pp_onepace") >= 0) {
                done({ meta: { videos: [
                    { id: "WH_1", season: 9, episode: 1 },
                    { id: "FI_1", season: 26, episode: 1 },
                    { id: "EH_1", season: 33, episode: 1 }
                ] } })
            } else done({ meta: { videos: [] } })
        })
    }
    function cleanup() {
        if (arcPage) arcPage.destroy()
        arcPage = null
        CatalogApi.resetRequestAdapter()
        CatalogApi.resetCache()
    }

    function test_catalogue_routes_and_labels_data() {
        return [
            { tag: "Paradise", arc: ParadiseAdapter.cataloguePayload(Paradise.arc("whisky-peak")), pace: "WH_1" },
            { tag: "New World", arc: NewWorldAdapter.cataloguePayload(NewWorld.arc("fish-man-island")), pace: "FI_1" },
            { tag: "Egghead", arc: FinalSeaAdapter.cataloguePayload(FinalSea.arc("egghead")), pace: "EH_1" },
            { tag: "Elbaf", arc: FinalSeaAdapter.cataloguePayload(FinalSea.arc("elbaf")), pace: "" }
        ]
    }
    function test_catalogue_routes_and_labels(data) {
        arcPage = arcComponent.createObject(testWindow.contentItem, {
            arc: data.arc,
            installedExtensions: [{ id: "com.onepace.fedew", enabled: true, transportUrl: "https://fixture.invalid/manifest.json" }]
        })
        verify(arcPage !== null)
        var catalogue = findChild(arcPage, "onePieceArcCatalogue")
        verify(catalogue !== null)
        compare(catalogue.seaLabel, data.arc.seaLabel)
        compare(catalogue.sagaLabel, data.arc.sagaLabel)
        compare(arcPage.heroSourceForArc(), data.arc.heroSource)
        compare(arcPage.relatedItemsForArc().length, data.arc.relatedMedia.length)
        verify(catalogue.animeItems.length > 0)
        verify(catalogue.volumeItems.length > 0)
        compare(catalogue.onePaceItems.length, data.pace ? 1 : 0)

        var episode = catalogue.episodeRoute(catalogue.animeItems[0], "anime")
        compare(episode.id, "kitsu:12")
        compare(episode.requestedArc.id, data.arc.id)
        if (data.pace) {
            var pace = catalogue.episodeRoute(catalogue.onePaceItems[0], "pace")
            compare(pace.id, "pp_onepace")
            compare(pace.requestedProvider, "onepace")
            compare(pace.requestedSeason, data.tag === "Paradise" ? 9 : data.tag === "New World" ? 26 : 33)
        }

        arcPage.payload = { sections: [{ id: "manga", entries: [
            { id: "30013", provider: "tankoban", title: "One Piece", malId: "13" },
            { id: "one-piece-color", provider: "tankoban", title: "One Piece (Color)", malId: "13", seriesId: "mal:13:color", sourceRequiredMarkers: ["colored"] }
        ] }] }
        seriesSpy.clear()
        var volume = String(catalogue.volumeItems[0].number)
        catalogue.mangaVolumeRequested(false, volume)
        catalogue.mangaVolumeRequested(true, volume)
        compare(seriesSpy.count, 2)
        var normal = seriesSpy.signalArguments[0][0]
        var colored = seriesSpy.signalArguments[1][0]
        compare(normal.requestedArc.id, data.arc.id)
        compare(normal.requestedVolumeNumber, volume)
        compare(colored.colorEdition, true)
        compare(colored.provider, "tankoban")
        compare(colored.seriesId, "mal:13:color")
        compare(colored.sourceRequiredMarkers[0], "colored")
        watchSpy.clear()
        catalogue.episodeRequested(episode)
        compare(watchSpy.count, 1)
        compare(watchSpy.signalArguments[0][0].requestedArc.id, data.arc.id)
        var requestsBeforeClose = requests
        arcPage.arc = null
        compare(requests, requestsBeforeClose, "clearing an arc must not start more provider requests")
        compare(catalogue.animeItems.length, 0)
    }
}
